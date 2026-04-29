// ============================================================================
// WhiteoutDex Real-Time Renderer — Render Service Implementation
// ============================================================================

#include "render_service.h"
#include "render_service_internal.h"
#include "render_pass.h"
#include "debug/debug_renderer.h"
#include "constants.h"
#include "sampler_asset_manager.h"
#include "texture_asset_manager.h"
#include "replaceable_texture_manager.h"
#include "scene_manager.h"
#include "model_template.h"
#include "model_template_manager.h"
#include "compiled_shaders.h"
#include "team_glow_data.h"
#include "mdx_model_adapter.h"
#include "file_content_provider.h"
#include "viewcube_atlas.h"
#include "bls/bls_shader_cache.h"
#include "bls/bls_program.h"
#include "bls/bls_pso_builder.h"
#include "bls/bls_mat_params.h"
#include "bls/bls_cb_layout.h"
#include "bls/bls_frame.h"
#include "bls/bls_draw_helpers.h"
#include "bls/scoped_cb.h"
#include "ibl/split_sum.h"
#include "ibl/env_probe.h"
#include <whiteout/models/mdx/parser.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <cstring>

#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* s);
#else
inline void OutputDebugStringA(const char*) {}
#endif

namespace WhiteoutDex {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RenderService::RenderService()
    : ownedScene_(std::make_unique<SceneManager>()),
      scene_(ownedScene_.get()),
      debug_(std::make_unique<DebugRenderer>(*this)) {
    // Cross-model dedup query — fed to every adapter the templates manager
    // builds. Wired here because IsTextureCached lives on RenderService
    // (it queries the TextureAssetManager, which we own).
    scene_->Templates().SetTextureCacheQuery(
        [this](std::string_view k) { return IsTextureCached(k); });
}

RenderService::RenderService(SceneManager& scene)
    : scene_(&scene),
      debug_(std::make_unique<DebugRenderer>(*this)) {
    // Same wiring as the default ctor; the scene's template loader was
    // already started by the host that constructed `scene`.
    scene_->Templates().SetTextureCacheQuery(
        [this](std::string_view k) { return IsTextureCached(k); });
}

RenderService::~RenderService() = default;   // ownedScene_ (if any) joins its template loader thread

// Helpers — out-of-line because they need SceneManager's full type, which only
// the .cpp sees (the header has just the forward decl).
Actor* RenderService::focusModel() const { return scene_->FocusActor(); }
Actor* RenderService::getModel(uint32_t h) const { return scene_->Actors().Find(h); }

// ============================================================================
// Model Data Input (called from API/MaxScript thread)
// ============================================================================

void RenderService::ClearModel() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : scene_->Actors().All()) {
        mi->render.stagedClear = true;
        mi->render.stagedDirty = true;
    }
    scene_->FocusRef() = 0;
    // Drop everything on the PE2 service side too.
    particleService_.Clear();
}

void RenderService::SetAttachmentConfigs(uint32_t handle, const std::vector<AttachmentConfig>& configs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;
    mi->attachmentSlots.clear();
    // Keep ALL attachments as slots so slot index == FrameState attachment index.
    // Only slots with non-empty modelPath will have child models loaded.
    for (auto& cfg : configs) {
        Actor::AttachmentSlot slot;
        slot.config = cfg;
        slot.loaded = cfg.modelPath.empty(); // mark empty-path slots as "loaded" (nothing to load)
        mi->attachmentSlots.push_back(slot);
    }
}

void RenderService::SetPE1Configs(uint32_t handle, const std::vector<PE1EmitterConfig>& configs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;
    for (int i = 0; i < (int)configs.size(); i++)
        mi->render.pe1.AddEmitter(i, configs[i]);
}

// ============================================================================
// Attachment Model Loading
// ============================================================================

void RenderService::UpdateAttachments() {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Collect handles to avoid modifying scene_->Actors().All() during iteration
    std::vector<uint32_t> handles;
    for (auto& [h, mi] : scene_->Actors().All()) handles.push_back(h);

    for (uint32_t h : handles) {
        auto* mi = getModel(h);
        if (!mi) continue;

        for (auto& slot : mi->attachmentSlots) {
            if (slot.loaded || slot.config.modelPath.empty()) continue;

            // Try to get the template. For CASC/MPQ-resolved MDX files this
            // almost always returns nullptr on the first call (template is
            // queued for the async worker). We must NOT mark the slot loaded
            // until the template is actually in hand — otherwise we'd skip
            // this slot forever and never build the child, even after the
            // worker finishes and the manager's Tick populates the cache.
            auto tmpl = scene_->Templates().GetOrLoadAsync(slot.config.modelPath);
            if (!tmpl) continue;

            slot.loaded = true;

            uint32_t childH = scene_->NextActorIdRef()++;
            auto child = std::make_unique<Actor>();
            child->handle = childH;
            child->parent = mi->handle;
            child->isPE1Child = true;  // reuse the flag for "child model"
            child->pe1Depth = mi->pe1Depth + 1;
            child->animation.Bind(tmpl->adapter);
            child->animation.SetBirthTimeMs(scene_->GetAnimationTime());
            // Pick a random sequence
            auto seqs = tmpl->adapter->GetSequences();
            if (!seqs.empty())
                child->animation.SetActiveSequenceIndex(rand() % (int)seqs.size());

            stageModelFromTemplate(child.get(), tmpl);
            scene_->Actors().All()[childH] = std::move(child);
            slot.childModelHandle = childH;
        }
    }
}

// ============================================================================
// Model template cache — extracted to ModelTemplateManager (renderer/model_template_manager.{h,cpp}).
// Path-based load + async loader thread + drain step all live there now.
// RenderService::templates_ owns the manager; SetContentProvider /
// SetPE1BasePath forward configuration; Tick() pumps scene_->Templates().Tick().
// ============================================================================

void RenderService::stageModelFromTemplate(Actor* mi,
                                           std::shared_ptr<ModelTemplate> tmpl) {
    if (!tmpl) return;
    // Pin the template alive for as long as this instance exists. UploadStagedGeosets
    // borrows ib/unskinnedVb/tangentVb/boneVb from tmpl->sharedGeosets, so the
    // template must outlive every instance that points at it.
    mi->sourceTemplate = tmpl;

    // Bind the actor's animation source to the template's parsed adapter
    // (which is also an IAnimationSource). This means callers can do
    //   actor->animation.Evaluate(world, cam, gtime)
    // without threading the adapter through the host loop. PE1/attachment
    // children re-bind explicitly afterwards because they want a fresh
    // birth-time + random sequence; this default seed covers top-level
    // SpawnActorFromMdx / LoadActorFromMdx callers.
    if (tmpl->adapter) mi->animation.Bind(tmpl->adapter);

    // Stage textures. For sharedKey paths we drop tex.pixels — UploadStagedTextures
    // will hit the renderer-side cache and BindShared without needing a CPU copy.
    // First-instance uploads still need the pixel data to seed the cache; we
    // detect that case via IsTextureCached(sharedKey) at staging time.
    for (auto& tex : tmpl->textures) {
        StagedTexture& st = mi->render.stagedTextures[tex.textureId];
        st.width = tex.width; st.height = tex.height;
        st.mipLevels = tex.mipLevels;
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.format = tex.format;
        st.sharedKey = tex.sharedKey;
        const bool alreadyCached =
            !tex.sharedKey.empty() && IsTextureCached(tex.sharedKey);
        if (!alreadyCached) st.pixels = tex.pixels;  // first-instance seed
        if (tex.replaceableId != 0 && replaceables_)
            replaceables_->RegisterModelSlot(*mi, tex.textureId, tex.replaceableId);
    }
    // Stage materials
    for (auto& mat : tmpl->materials) {
        StagedMaterial& sm = mi->render.stagedMaterials[mat.materialId];
        sm.layers        = mat.layers;
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder     = mat.sortOrder;
    }
    // Geometry is uploaded to GPU once on the template (lazy in UploadStagedGeosets);
    // we no longer copy mesh data into per-instance stagedGeosets. UploadStagedGeosets
    // sees mi->sourceTemplate set and walks tmpl->sharedGeosets directly.
    // Skeleton — adopt the shared SkinningData built once at template-load time.
    // Per-instance state (currentMatrices_/offsetMatrices_) is sized internally
    // by SetSharedData; the heavy weight + layout tables live on the template.
    if (tmpl->skinningData && tmpl->skinningData->nodeCount > 0) {
        mi->render.skinning.SetSharedData(tmpl->skinningData);
        // Hierarchy metadata (billboardFlags, nodePivots, nodeParents) is borrowed
        // straight from tmpl->skeleton at read time; ApplyBoneMatrices picks
        // template-or-owned via mi.sourceTemplate. Leaving the per-instance
        // vectors empty saves ~few KB/instance on heavy skeletons.
        mi->render.skinDirty = true;
    }
    // PE2 particles — registered directly with the service (no legacy path).
    // The emitter's own replaceableId is resolved AT DRAW TIME against
    // the manager's global SD swatches; we deliberately do NOT bake the
    // swatch into the actor's textureId slot here, otherwise a sibling
    // emitter that points at the same textureId with replaceableId=0
    // would also see the swatch instead of the loaded BLP.
    for (int i = 0; i < (int)tmpl->pe2Configs.size(); i++) {
        const auto& pcfg = tmpl->pe2Configs[i];
        auto em = std::make_unique<particle::PlaneEmitter>();
        particle::ApplyInit(*em, particle::InitFromLegacyConfig(pcfg));
        particleService_.AddPlaneEmitter(mi->handle, i, std::move(em));
    }
    mi->render.pe2State.resize(tmpl->pe2Configs.size());
    // Ribbons
    for (int i = 0; i < (int)tmpl->ribbonConfigs.size(); i++)
        mi->render.ribbons.AddEmitter(i, tmpl->ribbonConfigs[i]);
    // PE1 (recursive, only if depth allows)
    for (int i = 0; i < (int)tmpl->pe1Configs.size(); i++)
        mi->render.pe1.AddEmitter(i, tmpl->pe1Configs[i]);

    mi->render.stagedDirty = true;
}

// ============================================================================
// PE1 Model Particle Lifecycle
// ============================================================================

void RenderService::UpdatePE1(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<uint32_t> toRemove;

    // Collect handles to iterate (avoid modifying scene_->Actors().All() during iteration)
    std::vector<uint32_t> handles;
    for (auto& [h, mi] : scene_->Actors().All()) handles.push_back(h);

    for (uint32_t h : handles) {
        auto* mi = getModel(h);
        if (!mi) continue;
        if (mi->pe1Depth >= SceneManager::kMaxPE1Depth) continue;
        if (!mi->render.pe1.HasEmitters()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent — skip sub-emitter sim

        auto result = mi->render.pe1.Simulate(dt, scene_->NextActorIdRef());

        // Birth: create child Actor from template
        for (auto& birth : result.born) {
            if (scene_->PE1InstanceCountRef() >= SceneManager::kMaxPE1Instances) continue;
            auto* cfg = mi->render.pe1.GetConfig(birth.emitterId);
            if (!cfg) continue;
            auto tmpl = scene_->Templates().GetOrLoadAsync(cfg->modelPath);
            if (!tmpl) continue;

            auto child = std::make_unique<Actor>();
            child->handle = birth.handle;
            child->parent = mi->handle;
            child->worldTransform = birth.worldTransform;
            child->isPE1Child = true;
            child->pe1Depth = mi->pe1Depth + 1;
            child->animation.Bind(tmpl->adapter);
            child->animation.SetBirthTimeMs(scene_->GetAnimationTime());

            stageModelFromTemplate(child.get(), tmpl);
            scene_->Actors().All()[birth.handle] = std::move(child);
            scene_->PE1InstanceCountRef()++;
        }

        // Death
        for (uint32_t childH : result.died) toRemove.push_back(childH);

        // Transform updates
        for (auto& [childH, tm] : result.transforms) {
            if (auto* c = getModel(childH)) c->worldTransform = tm;
        }
    }

    for (uint32_t rh : toRemove) {
        auto it = scene_->Actors().All().find(rh);
        if (it != scene_->Actors().All().end()) {
            if (replaceables_) replaceables_->UnregisterModel(*it->second);
            it->second->ReleaseGPU(*gfx_);
            scene_->Actors().All().erase(it);
            scene_->PE1InstanceCountRef()--;
        }
        // Drop any emitters this child registered with the PE2 service.
        // Otherwise they persist as ghost emitters, drawing with the default
        // texture (since getModel(dl.model) returns null on lookup).
        particleService_.RemoveModel(rh);
    }
}

void RenderService::EvaluateTopLevelActors() {
    // Sister method to EvaluatePE1Children. SceneManager::Update has already
    // walked the same actors and written the looped local time into
    // `actor.animation` via SetTimeMs, so we just snapshot the cursor + Source
    // under the lock, evaluate without it (read-only against the source), and
    // apply through the per-handle ApplyFrameState (which re-locks).
    //
    // For the Max plugin, scene.Update is never called — Max writes
    // SetTimeMs externally on TimeChanged and we still pick up the cursor
    // here every render-thread tick.
    struct ActorEval {
        uint32_t handle;
        std::shared_ptr<IAnimationSource> adapter;
        Matrix44f worldTransform;
        int seqIdx;
        int localTimeMs;
        int globalTimeMs;
    };
    std::vector<ActorEval> toEval;
    Vector3f camPos;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        camPos = scene_->Camera().GetSource();
        const int now = scene_->GetAnimationTime();
        for (auto& [h, mi] : scene_->Actors().All()) {
            if (mi->isPE1Child) continue;                 // PE1/attachment children -> EvaluatePE1Children
            if (mi->externallyDriven) continue;           // host calls EvaluateAndApply on its own thread
            if (!mi->animation.HasSource()) continue;
            const int localTime  = mi->animation.TimeMs();         // already loop-clamped by SceneManager::Update
            const int globalTime = now - mi->animation.BirthTimeMs();
            toEval.push_back({h, mi->animation.Source(), mi->worldTransform,
                              mi->animation.ActiveSequenceIndex(),
                              localTime, globalTime});
        }
    }

    for (auto& ae : toEval) {
        FrameState fs = ae.adapter->Evaluate(ae.seqIdx, ae.localTimeMs, ae.globalTimeMs,
                                             ae.worldTransform, camPos);
        ApplyFrameState(ae.handle, fs, ae.localTimeMs);
    }
}

void RenderService::EvaluatePE1Children() {
    struct ChildEval {
        uint32_t handle;
        std::shared_ptr<IAnimationSource> adapter;
        int localTimeMs;
        int seqIdx;
        int globalTimeMs;  // unclamped elapsed since birth (for global sequences)
        Matrix44f worldTransform;  // child's world placement; the adapter folds
                                   // this into emitter / attachment / light
                                   // transforms so they spawn in scene space.
    };
    std::vector<ChildEval> toEval;
    Vector3f camPos;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        camPos = scene_->Camera().GetSource();
        int timeMs = scene_->GetAnimationTime();
        for (auto& [h, mi] : scene_->Actors().All()) {
            if (!mi->isPE1Child || !mi->animation.HasSource()) continue;
            if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent — skip eval
            int localTime = timeMs - mi->animation.BirthTimeMs();
            if (localTime < 0) localTime = 0;
            int globalTime = localTime;  // unclamped wall-clock elapsed since birth
            auto seqs = mi->animation.Sequences();
            const int seqIdx = mi->animation.ActiveSequenceIndex();
            if (!seqs.empty()) {
                const int boundedSeq = seqIdx % (int)seqs.size();
                int dur = seqs[boundedSeq].endMs - seqs[boundedSeq].startMs;
                if (dur > 0) localTime = seqs[boundedSeq].startMs + (localTime % dur);
            }
            toEval.push_back({h, mi->animation.Source(), localTime, seqIdx,
                              globalTime, mi->worldTransform});
        }
    }

    for (auto& ce : toEval) {
        FrameState fs = ce.adapter->Evaluate(ce.seqIdx, ce.localTimeMs, ce.globalTimeMs,
                                             ce.worldTransform, camPos);
        ApplyFrameState(ce.handle, fs, ce.localTimeMs);
    }
}

// ============================================================================
// Update Materials (hot-reload without full model rebuild)
// ============================================================================

void RenderService::UpdateMaterials(uint32_t handle, const std::vector<MaterialData>& materials,
                               const std::vector<TextureData>& textures) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;

    for (auto& tex : textures) {
        StagedTexture& st = mi->render.stagedTextures[tex.textureId];
        st.width  = tex.width;
        st.height = tex.height;
        st.mipLevels = tex.mipLevels;
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.format = tex.format;
        st.pixels = tex.pixels;
        st.sharedKey = tex.sharedKey;
        if (tex.replaceableId != 0 && replaceables_)
            replaceables_->RegisterModelSlot(*mi, tex.textureId, tex.replaceableId);
    }

    for (auto& mat : materials) {
        StagedMaterial& sm = mi->render.stagedMaterials[mat.materialId];
        sm.layers        = mat.layers;
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder     = mat.sortOrder;
    }

    mi->render.stagedDirty = true;
}

void RenderService::UpdateMaterials(const std::vector<MaterialData>& materials,
                               const std::vector<TextureData>& textures) {
    UpdateMaterials(scene_->FocusRef(), materials, textures);
}

// ============================================================================
// Multi-Model Loading API
// ============================================================================

uint32_t RenderService::AddModel(const std::vector<MeshData>& meshes,
                            const std::vector<TextureData>& textures,
                            const std::vector<MaterialData>& materials,
                            const SkeletonData& skeleton,
                            const std::vector<SkinWeightData>& skinWeights,
                            const std::vector<ParticleEmitterConfig>& particleConfigs,
                            const std::vector<RibbonEmitterConfig>& ribbonConfigs,
                            const std::vector<CollisionShapeData>& collisions) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    uint32_t handle = scene_->NextActorIdRef()++;
    auto mi = std::make_unique<Actor>();
    mi->handle = handle;

    // Textures → staged
    for (auto& tex : textures) {
        StagedTexture& st = mi->render.stagedTextures[tex.textureId];
        st.width  = tex.width;
        st.height = tex.height;
        st.mipLevels = tex.mipLevels;
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.format = tex.format;
        st.pixels = tex.pixels;
        st.sharedKey = tex.sharedKey;
        // Track replaceable textures for team-color / tileset updates.
        if (tex.replaceableId != 0 && replaceables_)
            replaceables_->RegisterModelSlot(*mi, tex.textureId, tex.replaceableId);
    }

    // Materials → staged
    for (auto& mat : materials) {
        StagedMaterial& sm = mi->render.stagedMaterials[mat.materialId];
        sm.layers        = mat.layers;
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder     = mat.sortOrder;
    }

    // Meshes → staged
    for (auto& mesh : meshes) {
        StagedGeoset& sg = mi->render.stagedGeosets[mesh.geosetId];
        sg.materialId = mesh.materialId;
        sg.lod        = mesh.lod;
        int vc = (int)mesh.positions.size();
        sg.vertices.resize(vc);
        for (int i = 0; i < vc; i++) {
            sg.vertices[i].position = mesh.positions[i];
            sg.vertices[i].normal   = (i < (int)mesh.normals.size()) ? mesh.normals[i] : Vector3f{0,0,1};
            sg.vertices[i].uv       = (i < (int)mesh.uvs.size()) ? mesh.uvs[i] : Vector2f{0,0};
            sg.vertices[i].color    = {1.0f, 1.0f, 1.0f, 1.0f};
        }
        if ((int)mesh.tangents.size() == vc) sg.tangents = mesh.tangents;
        sg.indices = mesh.indices;
    }

    // Skeleton
    if (skeleton.nodeCount > 0) {
        // Convert Matrix44f array to flat float array for existing SkinningSystem
        std::vector<float> invBindFlat(skeleton.nodeCount * 16);
        for (int i = 0; i < skeleton.nodeCount; i++) {
            memcpy(&invBindFlat[i * 16], &skeleton.inverseBindMatrices[i].data[0][0], 64);
        }
        mi->render.skinning.SetSkeleton(skeleton.nodeCount, invBindFlat.data());
        mi->render.billboardFlags = skeleton.billboardFlags;
        mi->render.nodePivots         = skeleton.nodePivots;
        mi->render.nodeParents        = skeleton.nodeParents;
        mi->render.skinDirty = true;
    }

    // Skin weights (per-geoset: boneIdx are LOCAL palette slots).
    for (auto& sw : skinWeights) {
        int vc = (int)sw.influences.size();
        std::vector<int>   boneIdx(vc * 4);
        std::vector<float> weights(vc * 4);
        for (int v = 0; v < vc; v++) {
            for (int j = 0; j < 4; j++) {
                boneIdx[v * 4 + j] = sw.influences[v].boneIdx[j];
                weights[v * 4 + j] = sw.influences[v].weight[j];
            }
        }
        mi->render.skinning.SetGeosetWeights(sw.geosetId, vc, boneIdx.data(), weights.data());
        GeosetPaletteLayout layout;
        layout.subsetNodeIndices = sw.subsetNodeIndices;
        layout.groupAverages     = sw.groupAverages;
        mi->render.skinning.SetGeosetLayout(sw.geosetId, std::move(layout));
    }
    if (!skinWeights.empty()) mi->render.skinDirty = true;

    // Particles — register with the PE2 service. The legacy ParticleEmitterConfig
    // boundary type stays (external API), but it's translated to the service's
    // PlaneEmitterInit on the way in. All downstream simulation/render is service.
    // PE2 emitter replaceableId is resolved at draw time against the
    // manager's global SD swatches — see the matching note + draw code
    // in stageModelFromTemplate / RenderParticlesBls.
    for (size_t i = 0; i < particleConfigs.size(); i++) {
        const auto& pcfg = particleConfigs[i];
        auto em = std::make_unique<particle::PlaneEmitter>();
        particle::ApplyInit(*em, particle::InitFromLegacyConfig(pcfg));
        particleService_.AddPlaneEmitter(handle, (int)i, std::move(em));
    }
    mi->render.pe2State.resize(particleConfigs.size());

    // Ribbons
    for (size_t i = 0; i < ribbonConfigs.size(); i++) {
        mi->render.ribbons.AddEmitter((int)i, ribbonConfigs[i]);
    }

    // Collision shapes
    for (auto& cs : collisions) {
        CollisionShape shape;
        shape.type   = cs.type;
        shape.vmin   = cs.vertices[0];
        shape.vmax   = cs.vertices[1];
        shape.radius = cs.radius;
        shape.pivot  = cs.pivot;
        mi->render.collisionShapes.push_back(shape);
    }

    mi->render.stagedDirty = true;
    if (scene_->FocusRef() == 0) scene_->FocusRef() = handle;
    scene_->Actors().All()[handle] = std::move(mi);
    return handle;
}

// ============================================================================
// Path-based load — borrows everything cacheable through ModelTemplate
// (parsed adapter, GPU geometry, skinning data, textures via TextureAssetManager).
// Subsequent calls with the same path skip parse + decode + GPU upload entirely.
// ============================================================================

uint32_t RenderService::AddModelByPath(const std::string& mdxPath) {
    // Manager handles cache lookup + cancels any pending async request +
    // synchronous parse on miss. Caches failures (nullptr) too.
    auto tmpl = scene_->Templates().GetOrLoadSync(mdxPath);
    if (!tmpl) return 0;

    // 3. Build the instance under dataMutex_. Collision shapes are populated
    // here (vs in stageModelFromTemplate) because PE1 children + attachment
    // children deliberately don't get them — only top-level path-based loads do.
    uint32_t handle;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        handle = scene_->NextActorIdRef()++;
        auto mi = std::make_unique<Actor>();
        mi->handle = handle;
        stageModelFromTemplate(mi.get(), tmpl);
        for (auto& cs : tmpl->collisionConfigs) {
            CollisionShape shape;
            shape.type   = cs.type;
            shape.vmin   = cs.vertices[0];
            shape.vmax   = cs.vertices[1];
            shape.radius = cs.radius;
            shape.pivot  = cs.pivot;
            mi->render.collisionShapes.push_back(shape);
        }
        scene_->Actors().All()[handle] = std::move(mi);
    }

    // 4. Register attachment configs from the template. SetAttachmentConfigs
    // takes dataMutex_ itself — call outside the section above.
    if (!tmpl->attachmentConfigs.empty())
        SetAttachmentConfigs(handle, tmpl->attachmentConfigs);

    return handle;
}

uint32_t RenderService::LoadModelByPath(const std::string& mdxPath) {
    ClearModel();
    uint32_t h = AddModelByPath(mdxPath);
    if (h == 0) return 0;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        scene_->FocusRef() = h;
    }

    // HD auto-detect (mirrors LoadModel). Pull the cached template back out
    // for its material list — cheap hashmap lookup.
    auto tmpl = scene_->Templates().Lookup(mdxPath);
    if (tmpl) {
        bool anyNonSd = false;
        for (auto& mat : tmpl->materials) {
            for (auto& layer : mat.layers) {
                if (layer.shaderId != 0) { anyNonSd = true; break; }
            }
            if (anyNonSd) break;
        }
        const RenderMode desired = anyNonSd ? RenderMode::HD : RenderMode::SD;
        if (renderMode_ != desired) {
            renderMode_ = desired;
            renderModeDirty_ = true;
        }
    }

    return h;
}

// ============================================================================
// High-level Actor spawn — preferred over the granular AddModel/LoadModel API.
// Returns Actor* directly; the legacy uint32_t-handle methods stay as
// thin wrappers used by the Max plugin's lock-step lifecycle.
// ============================================================================

Actor* RenderService::SpawnActorFromMdx(const std::string& mdxPath) {
    const uint32_t h = AddModelByPath(mdxPath);
    if (h == 0) return nullptr;
    return scene_->Actors().Find(h);
}

Actor* RenderService::LoadActorFromMdx(const std::string& mdxPath) {
    const uint32_t h = LoadModelByPath(mdxPath);
    if (h == 0) return nullptr;
    return scene_->Actors().Find(h);
}

Actor* RenderService::SpawnActorFromLiveSource(std::shared_ptr<IModelSource> source) {
    if (!source) return nullptr;
    // Cross-model dedup query — adapters skip BLP/CASC decode when the
    // texture is already in our shared cache.
    source->SetTextureCacheQuery(
        [this](std::string_view k) { return IsTextureCached(k); });

    // Pull the static snapshot once; downstream goes through the existing
    // staging machinery the same way the granular AddModel path does.
    ModelData data = source->Build();
    const uint32_t h = AddModel(data.meshes, data.textures, data.materials,
                                data.skeleton, data.skinWeights,
                                data.pe2Configs, data.ribbonConfigs,
                                data.collisionConfigs);
    Actor* actor = scene_->Actors().Find(h);
    if (!actor) return nullptr;

    // Bind the live IAnimationSource so per-frame Evaluate() reads from the
    // adapter (Max scene state, etc.) every tick.
    actor->animation.Bind(source);

    // Live sources (e.g. Max plugin's MaxSceneAdapter) read mutable
    // host-thread-only state during Evaluate(). Mark the actor so the
    // render-thread auto-eval skips it; the host calls EvaluateAndApply
    // explicitly from the right thread.
    actor->externallyDriven = true;

    // Fold attachment + PE1 configs into the spawn so callers don't have to
    // chase the actor handle just to set them.
    if (!data.attachmentConfigs.empty())
        SetAttachmentConfigs(h, data.attachmentConfigs);
    if (!data.pe1Configs.empty())
        SetPE1Configs(h, data.pe1Configs);

    // Auto-activate HD when any material layer ships a non-SD shader
    // (Layer::ShaderType: 0=SD, 1=HD, 2=SDOnHD, 24=Crystal). Matches the
    // path-based LoadModelByPath. Without this the Max plugin's HD models
    // render through the SD pipeline — HD swatch (t4 team colour) is never
    // bound, IBL/PBR is skipped, and HD-only material slots are ignored.
    bool anyNonSd = false;
    for (auto& mat : data.materials) {
        for (auto& layer : mat.layers) {
            if (layer.shaderId != 0) { anyNonSd = true; break; }
        }
        if (anyNonSd) break;
    }
    const RenderMode desired = anyNonSd ? RenderMode::HD : RenderMode::SD;
    if (renderMode_ != desired) {
        renderMode_ = desired;
        renderModeDirty_ = true;
    }

    return actor;
}

// ============================================================================
// Host-thread eval+apply for externally-driven actors. Reads the actor's
// IAnimationSource (which may touch host-only state — Max scene graph, etc.)
// then funnels the FrameState through the per-handle ApplyFrameState.
// ============================================================================
void RenderService::EvaluateAndApply(Actor& actor) {
    if (!actor.animation.HasSource()) return;
    Vector3f camPos;
    int      globalTime;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        camPos     = scene_->Camera().GetSource();
        globalTime = scene_->GetAnimationTime() - actor.animation.BirthTimeMs();
    }
    const int localTime = actor.animation.TimeMs();
    FrameState fs = actor.animation.Source()->Evaluate(
        actor.animation.ActiveSequenceIndex(), localTime, globalTime,
        actor.worldTransform, camPos);
    ApplyFrameState(actor.handle, fs, localTime);
}

// ============================================================================
// ApplyFrameState helpers
// ============================================================================

void RenderService::ApplyBoneMatrices(Actor& mi, const FrameState& state) {
    if (state.boneWorldMatrices.empty()) return;

    int bc = (int)state.boneWorldMatrices.size();
    Vector3f camPos = scene_->Camera().GetSource();

    // Hierarchy metadata: borrowed from the template's skeleton when the
    // instance was staged from one (saves a per-instance copy on heavy models),
    // else read from the per-instance owned vectors populated by AddModel.
    const std::vector<uint32_t>& billboardFlags = mi.sourceTemplate
        ? mi.sourceTemplate->skeleton.billboardFlags : mi.render.billboardFlags;
    const std::vector<Vector3f>& nodePivots = mi.sourceTemplate
        ? mi.sourceTemplate->skeleton.nodePivots : mi.render.nodePivots;
    const std::vector<int>& nodeParents = mi.sourceTemplate
        ? mi.sourceTemplate->skeleton.nodeParents : mi.render.nodeParents;

    std::vector<float> worldFlat(bc * 16);
    for (int i = 0; i < bc; i++) {
        Matrix44f boneM = state.boneWorldMatrices[i];

        uint32_t bbFlags = (i < (int)billboardFlags.size()) ? billboardFlags[i] : 0;
        if (bbFlags != 0) {
            // Match Previewd TransformObjectView @0x140534ee0. Two independent
            // pieces: CameraAnchored (0x80 in file) moves the pivot along the
            // parent→camera ray, then the Full/LockX/LockY/LockZ switch writes
            // a replacement rotation basis around that (modified) pivot.

            Vector3f pivF = (i < (int)nodePivots.size())
                              ? nodePivots[i] : Vector3f{0, 0, 0};

            // Target world-pivot starts as the authored one from boneM. We may
            // overwrite it below for CameraAnchored.
            Vector3f pivWorld = whiteout::transform_point(pivF, boneM);

            if (bbFlags & BONE_BILLBOARD_CAMERA_ANCHORED) {
                // PositionAnchor @0x140534950: place the node on the line
                // from parent-world to camera, at the node's rest distance
                // from parent. Rotation/scale of the stack are then reset
                // — we achieve the same by rebuilding boneM below.
                int parentIdx = (i < (int)nodeParents.size()) ? nodeParents[i] : -1;
                Vector3f parentWorld = {0, 0, 0};
                if (parentIdx >= 0 && parentIdx < (int)state.boneWorldMatrices.size()) {
                    Vector3f parentPivF = (parentIdx < (int)nodePivots.size())
                                            ? nodePivots[parentIdx] : Vector3f{0, 0, 0};
                    parentWorld = whiteout::transform_point(
                        parentPivF, state.boneWorldMatrices[parentIdx]);
                }
                Vector3f toCamDir = camPos - parentWorld;
                float camLen = toCamDir.length();
                if (camLen > kBillboardDistThreshold) {
                    toCamDir = toCamDir.normalized();
                    float restDist = (pivWorld - parentWorld).length();
                    pivWorld = parentWorld + Vector3f{toCamDir.x * restDist,
                                                      toCamDir.y * restDist,
                                                      toCamDir.z * restDist};
                }
            }

            Vector3f toCamera = camPos - pivWorld;
            float dist = toCamera.length();
            if (dist > kBillboardDistThreshold) {
                Vector3f toCam = toCamera.normalized();
                Vector3f worldUp = {0, 0, 1};

                // Capture the authored X/Y axes BEFORE we replace the rotation
                // — Previewd's LockX/LockY read WorldMatrixGetRow(0/1) AFTER
                // the node's authored rotation is applied, which is exactly
                // `boneM.data[0]` / `boneM.data[1]` in our row-vector layout.
                auto rowToVec = [](const Matrix44f& m, int r) {
                    return Vector3f{m.data[r][0], m.data[r][1], m.data[r][2]};
                };

                // Extract per-axis scale from the row magnitudes BEFORE any
                // normalization. boneM in our row-vector convention is
                // `S * R * T` so row magnitudes carry the authored scale —
                // a freshly-built orthonormal billboard basis would drop it,
                // cancelling any animated bone scale on a billboarded node.
                const float sX = rowToVec(boneM, 0).length();
                const float sY = rowToVec(boneM, 1).length();
                const float sZ = rowToVec(boneM, 2).length();

                Matrix44f bbRot = Matrix44f::identity();
                bool haveRot = false;

                // One-hot priority (Previewd's GetObjectFlags collapses
                // multiple file bits to one engine bit; our adapter already
                // enforces this, but we keep an else-if chain so stray bit
                // combinations still pick exactly one branch).
                if (bbFlags & BONE_BILLBOARD_FULL) {
                    // RotateViewBillboarded @0x14052ddb0 + FaceDirection @0x14052d870:
                    //   xprime = toCam                             (+X forward)
                    //   yprime = normalize(-xp.y, xp.x, 0)         (perpendicular in XY; "left")
                    //   zprime = cross(xprime, yprime)             (up)
                    // Free-roll billboard — no world-up pinning, but Z falls
                    // out of cross(x,y) so nearly-vertical cameras are stable.
                    Vector3f xp = toCam;
                    Vector3f yp = {-xp.y, xp.x, 0.0f};
                    float yLen = yp.length();
                    if (yLen < kBillboardDistThreshold) yp = {0, 1, 0};
                    else                                yp = yp.normalized();
                    Vector3f zp = whiteout::cross(xp, yp);
                    bbRot = {};
                    bbRot.data[0][0] = xp.x; bbRot.data[0][1] = xp.y; bbRot.data[0][2] = xp.z;
                    bbRot.data[1][0] = yp.x; bbRot.data[1][1] = yp.y; bbRot.data[1][2] = yp.z;
                    bbRot.data[2][0] = zp.x; bbRot.data[2][1] = zp.y; bbRot.data[2][2] = zp.z;
                    bbRot.data[3][3] = 1.0f;
                    haveRot = true;
                } else if (bbFlags & BONE_BILLBOARD_LOCK_X) {
                    // RotateViewXAxisBillboarded @0x14052e2e0: lock the node's
                    // authored X axis; rebuild Y,Z to face the camera.
                    //   zprime = normalize(cross(toCam, xprime))
                    //   yprime = cross(xprime, zprime)
                    Vector3f xp = rowToVec(boneM, 0);
                    float xLen = xp.length();
                    if (xLen < kBillboardDistThreshold) xp = {1, 0, 0};
                    else                                xp = xp.normalized();
                    Vector3f zp = whiteout::cross(toCam, xp);
                    float zLen = zp.length();
                    if (zLen < kBillboardDistThreshold) zp = {0, 0, 1};
                    else                                zp = zp.normalized();
                    Vector3f yp = whiteout::cross(xp, zp);
                    bbRot = {};
                    bbRot.data[0][0] = xp.x; bbRot.data[0][1] = xp.y; bbRot.data[0][2] = xp.z;
                    bbRot.data[1][0] = yp.x; bbRot.data[1][1] = yp.y; bbRot.data[1][2] = yp.z;
                    bbRot.data[2][0] = zp.x; bbRot.data[2][1] = zp.y; bbRot.data[2][2] = zp.z;
                    bbRot.data[3][3] = 1.0f;
                    haveRot = true;
                } else if (bbFlags & BONE_BILLBOARD_LOCK_Y) {
                    // RotateViewYAxisBillboarded @0x14052e0b0: lock authored Y.
                    //   zprime = normalize(cross(toCam, yprime))
                    //   xprime = cross(yprime, zprime)
                    Vector3f yp = rowToVec(boneM, 1);
                    float yLen = yp.length();
                    if (yLen < kBillboardDistThreshold) yp = {0, 1, 0};
                    else                                yp = yp.normalized();
                    Vector3f zp = whiteout::cross(toCam, yp);
                    float zLen = zp.length();
                    if (zLen < kBillboardDistThreshold) zp = {0, 0, 1};
                    else                                zp = zp.normalized();
                    Vector3f xp = whiteout::cross(yp, zp);
                    bbRot = {};
                    bbRot.data[0][0] = xp.x; bbRot.data[0][1] = xp.y; bbRot.data[0][2] = xp.z;
                    bbRot.data[1][0] = yp.x; bbRot.data[1][1] = yp.y; bbRot.data[1][2] = yp.z;
                    bbRot.data[2][0] = zp.x; bbRot.data[2][1] = zp.y; bbRot.data[2][2] = zp.z;
                    bbRot.data[3][3] = 1.0f;
                    haveRot = true;
                } else if (bbFlags & BONE_BILLBOARD_LOCK_Z) {
                    // RotateViewZAxisBillboarded @0x14052de70: Z is HARDCODED
                    // to world +Z (not the authored Z row). Matches Previewd.
                    //   yprime = normalize(cross(zprime, toCam))
                    //   xprime = cross(yprime, zprime)
                    Vector3f zp = worldUp;
                    Vector3f yp = whiteout::cross(zp, toCam);
                    float yLen = yp.length();
                    if (yLen < kBillboardDistThreshold) yp = {0, 1, 0};
                    else                                yp = yp.normalized();
                    Vector3f xp = whiteout::cross(yp, zp);
                    bbRot = {};
                    bbRot.data[0][0] = xp.x; bbRot.data[0][1] = xp.y; bbRot.data[0][2] = xp.z;
                    bbRot.data[1][0] = yp.x; bbRot.data[1][1] = yp.y; bbRot.data[1][2] = yp.z;
                    bbRot.data[2][0] = zp.x; bbRot.data[2][1] = zp.y; bbRot.data[2][2] = zp.z;
                    bbRot.data[3][3] = 1.0f;
                    haveRot = true;
                }

                if (haveRot) {
                    // Re-bake the captured per-axis scale into the orthonormal
                    // billboard basis. In row-vector layout this is `S * bbRot`
                    // expanded as `bbRot.row[i] *= s[i]`, so a v*boneM still
                    // sees v scaled by the bone's animated S before rotation.
                    bbRot.data[0][0] *= sX; bbRot.data[0][1] *= sX; bbRot.data[0][2] *= sX;
                    bbRot.data[1][0] *= sY; bbRot.data[1][1] *= sY; bbRot.data[1][2] *= sY;
                    bbRot.data[2][0] *= sZ; bbRot.data[2][1] *= sZ; bbRot.data[2][2] *= sZ;
                    Matrix44f T_negRest = Matrix44f::translation({-pivF.x, -pivF.y, -pivF.z});
                    Matrix44f T_world   = Matrix44f::translation({pivWorld.x, pivWorld.y, pivWorld.z});
                    boneM = T_negRest * bbRot * T_world;
                } else if (bbFlags & BONE_BILLBOARD_CAMERA_ANCHORED) {
                    // CameraAnchored without a billboard rotation flag: still
                    // need to re-center boneM so the pivot lands on pivWorld.
                    // Previewd's PositionAnchor strips rotation but the node's
                    // authored scale should still drive size, so we keep the
                    // diagonal S in place of identity.
                    Matrix44f S = {};
                    S.data[0][0] = sX; S.data[1][1] = sY; S.data[2][2] = sZ;
                    S.data[3][3] = 1.0f;
                    Matrix44f T_negRest = Matrix44f::translation({-pivF.x, -pivF.y, -pivF.z});
                    Matrix44f T_world   = Matrix44f::translation({pivWorld.x, pivWorld.y, pivWorld.z});
                    boneM = T_negRest * S * T_world;
                }
            }
        }

        memcpy(&worldFlat[i * 16], &boneM.data[0][0], 64);
    }
    mi.render.skinning.UpdateNodeMatrices(bc, worldFlat.data());
}

// ApplyGeosetStates / ApplyLayerStates moved to RenderModel — they're pure
// per-actor render-data updates with no scene/gfx dependencies. See
// renderer/render_model.cpp.

void RenderService::ApplyParticleFrameStates(Actor& mi, const FrameState& state) {
    // PE2 service is the single path. Per-frame: look up the registered
    // PlaneEmitter and push the evaluated animation state into it. Transform
    // is conjugated for Blizzard-space sim (not the default — see §3.8
    // retrospective in docs/PARTICLEEMITTERS2.md).
    
    for (size_t i = 0; i < state.particleStates.size(); ++i) {
        const auto& ps = state.particleStates[i];
        auto* em = particleService_.GetEmitter(mi.handle, ps.emitterId);
        if (!em) continue;

        

        em->SetEmissionRate(ps.emissionRate);
        em->SetVelocity(ps.speed);
        em->SetVelocityVariation(ps.variation);
        em->SetLatitude(ps.coneAngle);
        em->SetAcceleration(ps.gravity);
        em->SetWidth(ps.width);
        em->SetHeight(ps.length);
        // Mirrors Previewd's SetEmitter2Values @0x140531cb0:
        //   visible = isVisible[0] > 0.0 && !currobj->squirts;
        // Squirt emitters are force-hidden so they never emit continuously —
        // actual bursts are driven through SetSquirtPending below, on the
        // rising edge of emissionRate.
        em->SetVisible(ps.visibility > 0.0f && !ps.squirting);
        if (ps.squirting) {
            auto& st = mi.render.pe2State[i];
            if (st.emissionValid) {
                if (ps.emissionRate > 0.02f && st.lastEmissionRate <= 0.02f)
                    em->SetSquirtPending(true);
            }
            st.lastEmissionRate = ps.emissionRate;
            st.emissionValid = true;
        }
        
        // ps.transform arrives in the renderer-native default space. If the
        // emitter simulates in a different space, conjugate into it.
        em->SetModelToWorld(CoordinateSystem::ConvertTransform(
            CoordinateSystem::Default(), em->GetCoordSpace(), ps.transform));
    }
}

// ApplyRibbonFrameStates / ApplyPE1FrameStates moved to RenderModel —
// see renderer/render_model.cpp.

void RenderService::ApplyAttachmentStates(Actor& mi, const FrameState& state, int /*timeMs*/) {
    // BirthTimeMs for attachment children must be in the SCENE WALL-CLOCK
    // domain, not the parent's MDX-internal sequence time. The `timeMs`
    // arg propagated from EvaluateTopLevelActors comes from
    // `mi->animation.TimeMs()` which lives in [seq.startMs, seq.endMs]
    // — sequences that start at a high offset (e.g. seqStart = 30000)
    // would otherwise stamp the child's birth ahead of the wall clock,
    // and EvaluatePE1Children's `sceneTime - birth` clamps negative
    // results to 0, freezing the child's animation at frame 0 forever.
    const int sceneNow = scene_->GetAnimationTime();
    for (auto& as : state.attachmentStates) {
        if (as.attachmentIndex < 0 || as.attachmentIndex >= (int)mi.attachmentSlots.size()) continue;
        auto& slot = mi.attachmentSlots[as.attachmentIndex];
        if (slot.childModelHandle == 0) continue;
        auto* child = getModel(slot.childModelHandle);
        if (!child) continue;

        // Attachment visibility is a pure boolean gate in Previewd
        // (AnimIsAttachmentEnabled @0x1404651b0): `visibility > 0.0`. The
        // additional ancestor-bone gate is baked into `as.visibility` by
        // the adapter's nodeVisible[] sweep (matches Previewd's DFS skip
        // in PrepareObjectHierarchyViews @0x140535390), so a single float
        // check covers both.
        bool visible = (as.visibility > 0.0f);
        child->worldTransform = as.transform;

        // When becoming visible: pick a new random animation and restart from frame 0
        if (visible && !slot.wasVisible) {
            child->animation.SetBirthTimeMs(sceneNow);
            auto seqs = child->animation.Sequences();
            if (!seqs.empty())
                child->animation.SetActiveSequenceIndex(rand() % (int)seqs.size());
            slot.wasVisible = true;
        } else if (!visible) {
            slot.wasVisible = false;
        }

        // Parent-driven visibility is now strict on/off (1.0 or 0.0). The
        // downstream `<= 0.02f` skip tests and `geoAlpha * parentVisibility`
        // math still work with these two values and collapse to the same
        // behaviour as a bool gate: when hidden, every pass skips; when
        // shown, geoset alpha is unmodified.
        child->parentVisibility = visible ? 1.0f : 0.0f;
    }
}

// ============================================================================
// Apply Pre-computed Frame State
// ============================================================================

void RenderService::ApplyFrameState(uint32_t handle, const FrameState& state, int timeMs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;

    ApplyBoneMatrices(*mi, state);
    mi->render.ApplyGeosetStates(state);
    mi->render.ApplyLayerStates(state);
    ApplyParticleFrameStates(*mi, state);
    mi->render.ApplyRibbonFrameStates(state);
    mi->render.ApplyPE1FrameStates(state);

    // Collision transforms (simple 1:1 copy)
    for (int i = 0; i < (int)state.collisionTransforms.size() && i < (int)mi->render.collisionShapes.size(); i++)
        mi->render.collisionShapes[i].transform = state.collisionTransforms[i];

    ApplyAttachmentStates(*mi, state, timeMs);
}

// ============================================================================
// Texture cache query (cross-model dedup) — adapters poll through their
// IModelSource::IsTextureCached helper to skip BLP/CASC decode on hit.
// ============================================================================

bool RenderService::IsTextureCached(std::string_view key) const {
    return textures_ && textures_->IsCachedShared(key);
}

// ============================================================================
// Team Color — thin façade over ReplaceableTextureManager.
// ============================================================================

void RenderService::SetTeamColor(uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (replaceables_) replaceables_->SetTeamColor(r, g, b);
}

void RenderService::SetTileset(io::Tileset ts) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (replaceables_) replaceables_->SetTileset(ts);
}

io::Tileset RenderService::GetTileset() const {
    return io::GetCurrentTileset();
}

void RenderService::SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b) {
    // Pack COLORREF-style (0x00BBGGRR) so the value can flow straight
    // into the Win32 colour picker via GetBackgroundColorRaw().
    const uint32_t packed = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    backgroundColor_.store(packed);
}

// ============================================================================
// Camera Presets
// ============================================================================

void RenderService::ActivateCameraPreset(int idx) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    const auto& presets = scene_->CameraPresets();
    if (idx < 0 || idx >= (int)presets.size()) {
        scene_->Camera().SetOrbitalMode();
        scene_->Camera().SetFovDiagonal(Camera::kDefaultFovDiagonal);
        scene_->Camera().SetClip(Camera::kDefaultNearZ, Camera::kDefaultFarZ);
        scene_->SetActiveCameraPresetIdx(-1);
        return;
    }
    const auto& p = presets[idx];
    Vector3f pos  = p.position;
    Vector3f tgt  = p.target;
    float    roll = p.staticRoll;

    // Invoke the animator immediately so the first frame shows the
    // animated pose, not the pivot-only static default (which is
    // often (0,0,0) for portrait cameras).
    if (p.animator) {
        int seqStart = 0, seqEnd = 0;
        // Camera animator follows the focus actor's currently-playing sequence;
        // the sequence range table is set externally to mirror that actor's
        // MDX timeline.
        Actor* focus  = scene_->FocusActor();
        int    seqIdx = focus ? focus->animation.ActiveSequenceIndex() : 0;
        const auto& ranges = scene_->SequenceRanges();
        if (seqIdx >= 0 && seqIdx < (int)ranges.size()) {
            seqStart = ranges[seqIdx].startMs;
            seqEnd   = ranges[seqIdx].endMs;
        }
        // Fall back to an open range when no SequenceRanges are
        // known, so FindBracket doesn't empty out and return static.
        if (seqStart == 0 && seqEnd == 0) seqEnd = 1 << 30;
        p.animator(pos, tgt, roll, scene_->GetAnimationTime(), seqStart, seqEnd);
    }

    scene_->Camera().SetDirectPose(pos, tgt, roll);

    // Apply fov/near/far verbatim per Previewd's MdlReadCameras +
    // SetupWorldProjection. fieldOfView=0 is "unset" — substitute a
    // default so the preview doesn't render blank.
    const float fov = (p.fovDiagonal > 1e-3f) ? p.fovDiagonal : Camera::kDefaultFovDiagonal;
    scene_->Camera().SetFovDiagonal(fov);
    scene_->Camera().SetClip(p.zNear, p.zFar);
    scene_->SetActiveCameraPresetIdx(idx);
}

std::optional<std::vector<CameraPreset>> RenderService::TakePendingCameraPresets() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return scene_->TakePendingCameraPresets();
}

int RenderService::GetActiveSequenceIndex() const {
    // Reads the FOCUS actor's per-actor animation cursor. Other actors run
    // independent animations through their own AnimationDriver — this method
    // is the toolbar combo's view of the currently-focused actor only.
    std::lock_guard<std::mutex> lock(dataMutex_);
    Actor* focus = scene_->FocusActor();
    return focus ? focus->animation.ActiveSequenceIndex() : 0;
}

void RenderService::SetActiveSequence(int i) {
    // UI picker write — propagates to the FOCUS actor's animation driver.
    // Other actors are unaffected (they manage their own sequences).
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (Actor* focus = scene_->FocusActor()) {
        focus->animation.SetActiveSequenceIndex(i);
    }
}

std::optional<std::vector<std::string>> RenderService::TakePendingSequences() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return scene_->TakePendingSequences();
}

// ============================================================================
// Camera Manipulation (thread-safe wrappers)
// ============================================================================

void RenderService::RotateCamera(int dx, int dy) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    scene_->Camera().Rotate(dx, dy);
}

void RenderService::PanCamera(int dx, int dy) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    scene_->Camera().Pan(dx, dy);
}

void RenderService::ZoomCamera(int delta) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    scene_->Camera().Zoom(delta);
}

void RenderService::ZoomCameraSmooth(int dy) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    scene_->Camera().ZoomSmooth((float)dy * scene_->Camera().GetDistance() / Camera::kFactorRelDist);
}

void RenderService::ResetCamera() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    scene_->Camera().Reset();
}

void RenderService::SetDisplayFlags(const DisplayFlags& flags) {
    showGrid_       = flags.showGrid;
    showParticles_  = flags.showParticles;
    showRibbons_    = flags.showRibbons;
    showCollisions_ = flags.showCollisions;
    showLights_     = flags.showLights;
    renderMode_     = flags.renderMode;
}

DisplayFlags RenderService::GetDisplayFlags() const {
    return { showGrid_, showParticles_, showRibbons_, showCollisions_, showLights_, renderMode_ };
}

// ============================================================================
// Synchronous Service API
// ============================================================================

void RenderService::Tick(float dt) {
    // Drain async template loads. Eventually the host will pump this directly
    // (Phase 5 v3 — host owns SceneManager). For now we forward into the
    // scene's own template manager from inside the render Tick.
    scene_->Templates().Tick();
    ProcessStagedData();
    UpdateAttachments();
    EvaluateTopLevelActors();
    EvaluatePE1Children();
    UpdateAnimation();
    UpdateParticles(dt);
    UpdatePE1(dt);
    UpdateRibbons(dt);
}

void RenderService::ShutdownDevice() {
    ReleaseModelGPU();
    // Templates own the cross-instance geometry buffers — instances released
    // above only dropped their refcount on those buffers via sourceTemplate.reset().
    // We must explicitly tear down the template-side GPU resources before the
    // device is destroyed, otherwise CleanupD3D pulls the rug from under any
    // template entry that still holds buffer handles.
    scene_->Templates().ReleaseAllGPU(*gfx_);
    scene_->Templates().Clear();
    CleanupD3D();
}

void RenderService::GetFrameStats(int& geosets, int& textures, int& nodes,
                              int& particles, int& segments) const {
    geosets = textures = nodes = particles = segments = 0;
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : scene_->Actors().All()) {
        geosets  += (int)mi->render.gpuGeosets.size();
        textures += mi->render.textures ? (int)mi->render.textures->Size() : 0;
        nodes    += mi->render.skinning.NodeCount();
        segments += mi->render.ribbons.GetTotalSegmentCount();
    }
    particles += particleService_.TotalParticleCount();
}

// ============================================================================
// Staged → GPU Resource Upload (render thread only)
// ============================================================================

void RenderService::UploadStagedTextures(Actor& mi) {
    if (!mi.render.textures) mi.render.textures = textures_->CreateModelScope();
    for (auto& [id, st] : mi.render.stagedTextures) {
        // Three-way dispatch:
        //   1. sharedKey set, pixels empty → adapter saw a cache hit
        //      and skipped decode. Borrow only.
        //   2. sharedKey set, pixels present → file-backed first load.
        //      UploadShared creates+caches under the key.
        //   3. sharedKey empty → procedural / per-model owned upload.
        if (!st.sharedKey.empty() && st.pixels.empty()) {
            // Cache-hit-at-adapter-time path. BindShared returns Invalid
            // on the rare race where the entry was evicted between the
            // adapter's check and now; in that case Get(id) yields
            // Invalid and bind sites fall through to defaultTex.
            if (mi.render.textures->BindShared(id, st.sharedKey, st.wrapFlags)
                == gfx::TextureHandle::Invalid) {
                std::string msg = "[WDEX texture] eviction race for '";
                msg += st.sharedKey;
                msg += "' — using fallback\n";
                OutputDebugStringA(msg.c_str());
            }
            continue;
        }
        if (st.width <= 0 || st.height <= 0) continue;
        // Upload in the source pixel format (BC3/BC5/BC7 for normal
        // maps, sRGB variants for albedo, etc). The staged texture
        // carries the format that the DDS/BLP parser reported; bind
        // it verbatim so hd_ps.slang sees Blizzard's packed channels.
        const gfx::Format texFormat = (st.format == gfx::Format::Unknown)
                                          ? gfx::Format::R8G8B8A8_UNORM
                                          : st.format;
        const gfx::TextureDesc desc{
            .width     = st.width,
            .height    = st.height,
            .mipLevels = (std::max)(1, st.mipLevels),
            .format    = texFormat,
            .usage     = gfx::TextureUsage::ShaderResource,
        };
        if (st.sharedKey.empty()) {
            mi.render.textures->Upload(id, desc, st.pixels.data(), st.wrapFlags);
        } else {
            mi.render.textures->UploadShared(id, st.sharedKey, desc,
                                      st.pixels.data(), st.wrapFlags);
        }
    }
    mi.render.stagedTextures.clear();
}

void RenderService::uploadTemplateGpu(ModelTemplate& tmpl) {
    if (tmpl.gpuUploaded) return;
    tmpl.sharedGeosets.clear();
    tmpl.sharedGeosets.reserve(tmpl.meshes.size());

    // Build a geosetId → SkinWeightData* map so we can pair each mesh with
    // its weights without a nested O(N*M) scan.
    std::unordered_map<int, const SkinWeightData*> weightsByGeoset;
    weightsByGeoset.reserve(tmpl.skinWeights.size());
    for (const auto& sw : tmpl.skinWeights) weightsByGeoset[sw.geosetId] = &sw;

    for (const auto& mesh : tmpl.meshes) {
        ModelTemplate::SharedGeoset sg;
        sg.geosetId    = mesh.geosetId;
        sg.materialId  = mesh.materialId;
        sg.lod         = mesh.lod;
        sg.vertexCount = (int)mesh.positions.size();
        sg.indexCount  = (int)mesh.indices.size();

        std::vector<Vertex> vertices(sg.vertexCount);
        for (int i = 0; i < sg.vertexCount; i++) {
            vertices[i].position = mesh.positions[i];
            vertices[i].normal   = (i < (int)mesh.normals.size()) ? mesh.normals[i] : Vector3f{0,0,1};
            vertices[i].uv       = (i < (int)mesh.uvs.size())     ? mesh.uvs[i]     : Vector2f{0,0};
            vertices[i].color    = {1.0f, 1.0f, 1.0f, 1.0f};
        }
        sg.unskinnedVb = gfx_->CreateBuffer({
            .size  = (uint32_t)(sizeof(Vertex) * sg.vertexCount),
            .usage = gfx::BufferUsage::Vertex,
        }, vertices.data());

        sg.ib = gfx_->CreateBuffer({
            .size  = (uint32_t)(sizeof(uint32_t) * sg.indexCount),
            .usage = gfx::BufferUsage::Index,
        }, mesh.indices.data());

        if ((int)mesh.tangents.size() == sg.vertexCount) {
            sg.tangentVb = gfx_->CreateBuffer({
                .size  = (uint32_t)(sizeof(Vector4f) * sg.vertexCount),
                .usage = gfx::BufferUsage::Vertex,
            }, mesh.tangents.data());
        }

        // BoneVertex (ATTR5/ATTR6) — built from skinWeights for this geoset.
        // Same packing as the legacy path so SD/HD shaders bind the same.
        auto wIt = weightsByGeoset.find(mesh.geosetId);
        if (wIt != weightsByGeoset.end()
            && (int)wIt->second->influences.size() == sg.vertexCount) {
            const auto& sw = *wIt->second;
            std::vector<BoneVertex> bv(sg.vertexCount);
            for (int v = 0; v < sg.vertexCount; v++) {
                const auto& inf = sw.influences[v];
                int   idxArr[4] = { inf.boneIdx[0], inf.boneIdx[1], inf.boneIdx[2], inf.boneIdx[3] };
                float wtArr[4]  = { inf.weight[0],  inf.weight[1],  inf.weight[2],  inf.weight[3]  };
                bls::PackBoneVertex(bv[v], idxArr, wtArr);
            }
            sg.boneVb = gfx_->CreateBuffer({
                .size  = (uint32_t)(sizeof(BoneVertex) * sg.vertexCount),
                .usage = gfx::BufferUsage::Vertex,
            }, bv.data());
        }

        tmpl.sharedGeosets.push_back(sg);
    }
    tmpl.gpuUploaded = true;
}

void RenderService::UploadStagedGeosets(Actor& mi) {
    if (mi.sourceTemplate) {
        // Borrow path: every GPUGeoset shares ib/unskinnedVb/tangentVb/boneVb
        // with the template. Per-frame state (bonePaletteCb, world, alpha,
        // color, priorityPlane) stays on the per-instance GPUGeoset.
        auto& tmpl = *mi.sourceTemplate;
        uploadTemplateGpu(tmpl);
        if (mi.render.gpuGeosets.empty()) {
            // First-time upload: build one GPUGeoset per shared template entry.
            for (const auto& shared : tmpl.sharedGeosets) {
                GPUGeoset gg;
                gg.geosetId    = shared.geosetId;
                gg.materialId  = shared.materialId;
                gg.lod         = shared.lod;
                gg.ib          = shared.ib;
                gg.unskinnedVb = shared.unskinnedVb;
                gg.tangentVb   = shared.tangentVb;
                gg.boneVb      = shared.boneVb;
                gg.indexCount  = shared.indexCount;
                gg.vertexCount = shared.vertexCount;
                gg.hasSkinning = true;
                if (shared.materialId >= 0 && shared.materialId < (int)mi.render.gpuMaterials.size())
                    gg.priorityPlane = mi.render.gpuMaterials[shared.materialId].cpu.priorityPlane;
                mi.render.gpuGeosets.push_back(gg);
            }
        } else {
            // Re-stage trigger (texture refresh, material hot-reload). The
            // template's geometry is immutable so we must not push duplicates;
            // refresh the per-geoset cached priorityPlane in case the material
            // changed underneath us.
            for (auto& gg : mi.render.gpuGeosets) {
                if (gg.materialId >= 0 && gg.materialId < (int)mi.render.gpuMaterials.size())
                    gg.priorityPlane = mi.render.gpuMaterials[gg.materialId].cpu.priorityPlane;
            }
        }
        mi.render.stagedGeosets.clear();
    } else {
        // Legacy per-instance buffer creation path (top-level Max/test load).
        for (auto& [id, sg] : mi.render.stagedGeosets) {
            GPUGeoset gg;
            gg.geosetId    = id;
            gg.materialId  = sg.materialId;
            gg.lod         = sg.lod;
            gg.indexCount   = (int)sg.indices.size();
            gg.vertexCount  = (int)sg.vertices.size();
            gg.hasSkinning  = true; // all MDX geosets are skinned (v1200 weights or v800 vertex groups)

            // Copy priorityPlane from material for render sorting
            if (sg.materialId >= 0 && sg.materialId < (int)mi.render.gpuMaterials.size())
                gg.priorityPlane = mi.render.gpuMaterials[sg.materialId].cpu.priorityPlane;

            const uint32_t vbBytes = (uint32_t)(sizeof(Vertex) * sg.vertices.size());
            const GeosetSkinInfo* skinInfo = mi.render.skinning.GetGeosetWeights(id);

            gg.unskinnedVb = gfx_->CreateBuffer({
                .size  = vbBytes,
                .usage = gfx::BufferUsage::Vertex,
            }, sg.vertices.data());

            gg.ib = gfx_->CreateBuffer({
                .size  = (uint32_t)(sizeof(uint32_t) * sg.indices.size()),
                .usage = gfx::BufferUsage::Index,
            }, sg.indices.data());

            if ((int)sg.tangents.size() == gg.vertexCount) {
                gg.tangentVb = gfx_->CreateBuffer({
                    .size  = (uint32_t)(sizeof(Vector4f) * sg.tangents.size()),
                    .usage = gfx::BufferUsage::Vertex,
                }, sg.tangents.data());
            }

            if (skinInfo && (int)skinInfo->vertices.size() == gg.vertexCount) {
                std::vector<BoneVertex> bv(gg.vertexCount);
                for (int v = 0; v < gg.vertexCount; v++) {
                    const auto& inf = skinInfo->vertices[v];
                    int   idxArr[4] = { inf.boneIdx[0], inf.boneIdx[1], inf.boneIdx[2], inf.boneIdx[3] };
                    float wtArr[4]  = { inf.weight[0],  inf.weight[1],  inf.weight[2],  inf.weight[3]  };
                    bls::PackBoneVertex(bv[v], idxArr, wtArr);
                }
                gg.boneVb = gfx_->CreateBuffer({
                    .size  = (uint32_t)(sizeof(BoneVertex) * gg.vertexCount),
                    .usage = gfx::BufferUsage::Vertex,
                }, bv.data());
            }

            mi.render.gpuGeosets.push_back(gg);
        }
        mi.render.stagedGeosets.clear();
    }

    // Detect whether this model has a real LOD chain. Mirrors Previewd's
    // HasLODs @0x1401b6670 intent: if any geoset carries a non-zero LOD
    // (and not the 0xFFFFFFFF always-render sentinel), there are multiple
    // LOD levels to select between. Classic / pre-v900 models leave all
    // lods at 0 and we pin selectedLOD to 0 so nothing gets filtered out.
    mi.render.hasLods = false;
    for (const auto& g : mi.render.gpuGeosets) {
        if (g.lod != 0 && g.lod != 0xFFFFFFFFu) { mi.render.hasLods = true; break; }
    }
}

void RenderService::CreateNodePalette(Actor& mi) {
    // Each skinned geoset gets its OWN bone palette CB (vsCB3), sized for
    // kMaxBones=256 entries even when the geoset only uses a few slots —
    // matches the BLS cb_structs.slang ConstantBuffer layout. Per-geoset
    // subsets keep the uint8 ATTR6 indices in range for models with >256
    // bones total (e.g. nightelf_exp with 650+).
    for (auto& geo : mi.render.gpuGeosets) {
        if (geo.boneVb == gfx::BufferHandle::Invalid) continue;
        if (geo.bonePaletteCb != gfx::BufferHandle::Invalid) continue;
        if (mi.render.skinning.GeosetPaletteSize(geo.geosetId) <= 0) continue;
        geo.bonePaletteCb = gfx_->CreateBuffer({
            .size  = sizeof(bls::BonePaletteCb),
            .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
        });
        geo.hasSkinning = true;
    }
}

void RenderService::ProcessStagedData() {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Remove models marked for clear. Also drop their emitters from the PE2
    // service so stale entries don't accumulate across reloads.
    for (auto it = scene_->Actors().All().begin(); it != scene_->Actors().All().end(); ) {
        if (it->second->render.stagedClear) {
            const uint32_t clearedHandle = it->first;
            if (replaceables_) replaceables_->UnregisterModel(*it->second);
            it->second->ReleaseGPU(*gfx_);
            it = scene_->Actors().All().erase(it);
            particleService_.RemoveModel(clearedHandle);
        } else {
            ++it;
        }
    }

    for (auto& [h, miPtr] : scene_->Actors().All()) {
        auto* mi = miPtr.get();
        if (!mi->render.stagedDirty && !mi->render.skinDirty) continue;

        if (mi->render.stagedDirty) {
            UploadStagedTextures(*mi);

            // Copy materials (CPU data for render logic)
            for (auto& [id, sm] : mi->render.stagedMaterials) {
                if ((int)mi->render.gpuMaterials.size() <= id) mi->render.gpuMaterials.resize(id + 1);
                mi->render.gpuMaterials[id].cpu = sm;
            }
            mi->render.stagedMaterials.clear();

            UploadStagedGeosets(*mi);
            mi->render.stagedDirty = false;
        }

        if (mi->render.skinDirty) {
            CreateNodePalette(*mi);
            mi->render.skinDirty = false;
        }
    }
}

void RenderService::ReleaseModelGPU() {
    for (auto& [h, miPtr] : scene_->Actors().All())
        miPtr->ReleaseGPU(*gfx_);
}

// Build a packed Texture2DArray for a material.
// All layer textures are CPU-resized (nearest-neighbor) to the max (w,h) found
// ============================================================================
// Animation Update (render thread)
// ============================================================================

void RenderService::UpdateAnimation() {
    // Evaluate skinning on CPU once per frame. Each geoset owns its own
    // palette CB (vsCB3) holding a compact subset of bones — we fill it
    // from SkinningSystem's global offsetMatrices through the geoset's
    // subsetNodeIndices + groupAverages layout.
    std::lock_guard<std::mutex> lock(dataMutex_);

    for (auto& [h, miPtr] : scene_->Actors().All()) {
        auto* mi = miPtr.get();
        if (!mi->render.skinning.HasSkeleton() || !mi->render.skinning.IsReady()) continue;
        if (mi->parentVisibility <= 0.02f) continue;

        mi->render.skinning.ComputeOffsetMatrices();

        for (auto& geo : mi->render.gpuGeosets) {
            if (geo.bonePaletteCb == gfx::BufferHandle::Invalid) continue;
            if (auto bp = bls::ScopedCb<bls::BonePaletteCb>(gfx_.get(), geo.bonePaletteCb)) {
                // Fill the geoset's compact palette: subset bones by local
                // slot, group averages at their pseudo slots, rest identity.
                // `bp->bones` is an array of ShaderBone; we write via a
                // staging array of Matrix44f and then pack to shader format.
                constexpr int kSlots = bls::kMaxBones;
                static thread_local Matrix44f staging[kSlots];
                mi->render.skinning.ComputeGeosetPalette(geo.geosetId, staging, kSlots);
                bls::BuildBonePalette(*bp, staging, kSlots);
            }
        }
    }
}

// ============================================================================
// Particle Simulation + Rendering (render thread)
// ============================================================================

void RenderService::UpdateParticles(float dt) {
    // Parent-visibility gating happens via SetVisible() in
    // ApplyParticleFrameStates — an emitter whose parent is hidden gets
    // ps.visibility <= 0 and never emits anything during Simulate.
    particleService_.Simulate(dt);
}

bool RenderService::RenderParticlesBls() {
    // Route PE2 particles through the stock Blizzard SD VS + SD PS. Returns
    // true if the BLS path ran (success or benignly skipped); false only
    // if the program failed to load (InitDevice bails on that path now so
    // this effectively always returns true once a device is alive).
    if (!blsSdProgram_ || !blsPsoBuilder_) return false;

    auto* cmd = gfx_->GetImmediateContext();

    std::vector<Vertex> verts;
    std::vector<particle::EmitterDrawList> drawLists;
    Matrix44f viewMat;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (particleService_.EmitterCount() == 0) return true;
        viewMat = scene_->Camera().GetViewMatrix();
        particleService_.BuildGeometry(viewMat, verts, drawLists);
    }
    if (verts.empty()) return true;

    const int vertCount = (int)verts.size();

    // Grow + upload the shared particle VB.
    if (particleServiceVB_ == gfx::BufferHandle::Invalid || vertCount > particleServiceVBSize_) {
        gfx_->Destroy(particleServiceVB_);
        int newSize = (std::max)(vertCount, 4096);
        gfx::BufferDesc bd;
        bd.size  = (uint32_t)(sizeof(Vertex) * newSize);
        bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
        particleServiceVB_     = gfx_->CreateBuffer(bd);
        particleServiceVBSize_ = newSize;
    }
    if (void* mapped = gfx_->MapBuffer(particleServiceVB_)) {
        memcpy(mapped, verts.data(), sizeof(Vertex) * vertCount);
        gfx_->UnmapBuffer(particleServiceVB_);
    }

    cmd->BindVertexBuffer(0, particleServiceVB_, sizeof(Vertex));

    // Particle geometry is emitted in world space by BuildEmitterGeometry
    // (corners = worldPos + right*sx + up*sy), so feed world=identity and let
    // view+projection take them to clip space.
    bls::FrameInputs frame;
    frame.world      = Matrix44f::identity();
    frame.view       = viewMat;
    const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    frame.projection = scene_->Camera().ProjectionRH(aspect);
    frame.effectTime = scene_->GetAnimationTime() * 0.001f;
    frame.numLights  = 0;  // particles are unlit via MatParams.disables bit 0
    frame.viewportRect = { (float)width_, (float)height_, 0.0f, 0.0f };

    int drawOffset = 0;
    for (const auto& dl : drawLists) {
        if (dl.vertexCount <= 0) continue;

        // Build MatParams exactly like ILoadParticleEmitters2 would. Particles
        // are always unshaded in SD mode per MDX tradition, so force the
        // lighting disable bit too.
        bls::MatParams mp = bls::FromParticleDesc(dl.material, bls::GxShaderID::SD);
        mp.disables |= bls::kDisableLighting;

        // Feed the diffuse color from the emitter's per-draw material. The
        // legacy path already pre-multiplied the per-vertex colors, so diffuse
        // stays white -- the Modulate special-case happens inside the VS/PS.
        mp.diffuseColor = {1, 1, 1, 1};

        // Permute: weightIndex=0 (no bones), numColors=1 (PNCT0),
        // numTexCoords=1, numLights=0 (unshaded).
        bls::RenderState rs;
        rs.shaderId       = bls::GxShaderID::SD;
        rs.alphaMode      = static_cast<uint8_t>(mp.alpha);
        rs.numColors      = 1;
        rs.numTexCoords   = 1;
        rs.numWeights     = 0;
        rs.numLights      = 0;
        rs.fogEnabled     = false;
        rs.depthWrite     = mp.DepthWriteEnabled();
        rs.lightingEnabled= false;
        auto perm = bls::SelectPermutes(rs);

        // Build + bind PSO.
        const auto req = bls::MakePsoRequest(blsSdProgram_,
                                             bls::VertexLayoutKind::ParticleSD,
                                             mp, perm);
        auto pso = blsPsoBuilder_->GetOrBuild(req);
        if (pso == gfx::PipelineHandle::Invalid) { drawOffset += dl.vertexCount; continue; }
        cmd->BindPipeline(pso);

        // VS CB (208 B fixed + 64 B per light; unlit → 208 B).
        // PS CB (48 B: alphaRef + fog).
        if (auto vs = bls::ScopedCb<bls::SdVsCbA>(gfx_.get(), blsSdVsCb_)) {
            bls::BuildSdVsCbA(*vs, frame, mp);
        }
        if (auto ps = bls::ScopedCb<bls::SdPsCbA>(gfx_.get(), blsSdPsCb_)) {
            bls::BuildSdPsCbA(*ps, frame, mp);
        }
        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, blsSdVsCb_);
        cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, blsSdPsCb_);

        // PE2 draws always sample with wrap addressing — the cell-
        // animation UV formula can produce out-of-range values when the
        // authored range exceeds rows*cols, and clamp-edge would pick
        // zero-alpha edge texels and discard every fragment.
        const uint32_t wrapFlags = 0x3;
        // Resolve PE2 sample source per emitter. The emitter's own
        // `replaceableId` (1=TeamColor, 2=TeamGlow) overrides the
        // textureId binding to the manager's global swatch — that way
        // a sibling emitter that shares the same textureId but
        // declares replaceableId=0 still samples the loaded BLP from
        // its actor's texture cache. Pre-fix used to bake the swatch
        // into mi.render.stagedTextures[textureId], which destroyed
        // the original BLP for every other reference.
        gfx::TextureHandle peTex = gfx::TextureHandle::Invalid;
        if (dl.material.replaceableId == 1 && replaceables_) {
            peTex = replaceables_->GetSdTeamColorTexture();
        } else if (dl.material.replaceableId == 2 && replaceables_) {
            peTex = replaceables_->GetSdTeamGlowTexture();
        } else {
            std::lock_guard<std::mutex> lock(dataMutex_);
            Actor* owner = getModel(dl.model);
            if (owner && owner->render.textures && dl.material.textureId >= 0)
                peTex = owner->render.textures->Get(dl.material.textureId);
        }
        if (peTex != gfx::TextureHandle::Invalid)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, peTex);
        else
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, textures_->GetDefaults().White);
        cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplers_->WrapVariant(wrapFlags));

        cmd->Draw(dl.vertexCount, drawOffset);
        drawOffset += dl.vertexCount;
    }
    return true;
}

// ============================================================================
// Ribbon Simulation + Rendering (render thread)
// ============================================================================

void RenderService::UpdateRibbons(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : scene_->Actors().All()) {
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        mi->render.ribbons.Simulate(dt);
    }
}

void RenderService::RenderRibbons() {
    if (!blsSdProgram_ || !blsPsoBuilder_) return;
    auto* cmd = gfx_->GetImmediateContext();

    // Shared FrameInputs skeleton -- ribbon vertices are emitted in world
    // space, so world stays identity and the VS takes them straight to
    // clip space through view*proj. numLights=0 pairs with the kDisable-
    // Lighting flag we set on MatParams below (SD VS picks the 0-lights
    // permute).
    bls::FrameInputs frame;
    frame.world        = Matrix44f::identity();
    const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    frame.numLights    = 0;
    frame.viewportRect = { (float)width_, (float)height_, 0.0f, 0.0f };
    frame.effectTime   = scene_->GetAnimationTime() * 0.001f;

    for (auto& [_mh, _mi] : scene_->Actors().All()) {
    auto* mi = _mi.get();
    Matrix44f viewMat;
    RibbonSystem::StripResult stripResult;
    std::vector<RibbonEmitterConfig> configs;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!mi->render.ribbons.HasEmitters()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        viewMat = scene_->Camera().GetViewMatrix();
        stripResult = mi->render.ribbons.BuildStrips();
        for (int eid : stripResult.emitterIds) {
            auto* c = mi->render.ribbons.GetConfig(eid);
            configs.push_back(c ? *c : RibbonEmitterConfig{});
        }
    }

    auto& verts = stripResult.vertices;
    auto& emitterIds = stripResult.emitterIds;
    if (verts.empty()) continue;
    int vertCount = (int)verts.size();

    // Grow ribbon VB if needed
    if (mi->render.ribbonVB == gfx::BufferHandle::Invalid || vertCount > mi->render.ribbonVBSize) {
        gfx_->Destroy(mi->render.ribbonVB);
        int newSize = (std::max)(vertCount, 512);
        gfx::BufferDesc bd;
        bd.size  = (uint32_t)(sizeof(Vertex) * newSize);
        bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
        mi->render.ribbonVB = gfx_->CreateBuffer(bd);
        mi->render.ribbonVBSize = newSize;
    }

    // Upload vertex data
    void* mapped = gfx_->MapBuffer(mi->render.ribbonVB);
    if (!mapped) continue;
    memcpy(mapped, verts.data(), sizeof(Vertex) * vertCount);
    gfx_->UnmapBuffer(mi->render.ribbonVB);

    // Bind ribbon VB
    cmd->BindVertexBuffer(0, mi->render.ribbonVB, sizeof(Vertex));

    frame.view       = viewMat;
    frame.projection = scene_->Camera().ProjectionRH(aspect);

    int drawOffset = 0;
    std::vector<int> vertCounts;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (int eid : emitterIds)
            vertCounts.push_back(mi->render.ribbons.GetEmitterVertCount(eid));
    }

    for (int ei = 0; ei < (int)emitterIds.size(); ei++) {
        auto& cfg = configs[ei];
        int count = vertCounts[ei];
        if (count <= 0) { continue; }

        // MatParams from filter mode + cfg flags. Force lighting off —
        // ribbons are always unshaded in classic MDX — so the VS picks
        // the 0-light permute regardless of scene lights.
        int matFlags = 0;
        if (cfg.twoSided) matFlags |= MAT_TWO_SIDED;
        if (cfg.unshaded) matFlags |= MAT_UNSHADED;
        bls::MatParams mp = bls::FromMdxLayer(cfg.filterMode, matFlags, bls::GxShaderID::SD);
        mp.disables |= bls::kDisableLighting;
        mp.diffuseColor = {1, 1, 1, 1};

        bls::RenderState rs = bls::MakeSdMeshRenderState(mp, 0, /*unlit*/true, /*hasBones*/false);
        auto perm = bls::SelectPermutes(rs);
        const auto req = bls::MakePsoRequest(blsSdProgram_,
                                             bls::VertexLayoutKind::ParticleSD,
                                             mp, perm);
        auto pso = blsPsoBuilder_->GetOrBuild(req);
        if (pso == gfx::PipelineHandle::Invalid) { drawOffset += count; continue; }
        cmd->BindPipeline(pso);

        if (auto vs = bls::ScopedCb<bls::SdVsCbA>(gfx_.get(), blsSdVsCb_)) {
            bls::BuildSdVsCbA(*vs, frame, mp);
        }
        if (auto ps = bls::ScopedCb<bls::SdPsCbA>(gfx_.get(), blsSdPsCb_)) {
            bls::BuildSdPsCbA(*ps, frame, mp);
        }
        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, blsSdVsCb_);
        cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, blsSdPsCb_);

        render_detail::BindLayerAlbedo(cmd, mi->render.textures.get(), cfg.textureId,
                                       textures_->GetDefaults().White, *samplers_);

        cmd->Draw(count, drawOffset);
        drawOffset += count;
    }
    } // end for each model
}

// ============================================================================
// LOD selection — port of Previewd's CalculateLOD @0x140305ae0.
//
// Reforged picks a LOD level by measuring the projected size of a 1-unit
// feature on screen: `screenPixels = proj.m11 / viewDist * halfHeight`,
// then `deviation = 5.0 / screenPixels`. It walks the thresholds
//   deviations = [0.0, 1.0, 2.0, 4.0]   (data @0x1454d09b0)
// starting at LOD 3 and dropping one LOD per threshold cleared. The
// result is clamped to [s_minLOD, s_maxLOD] (both default 0 in Previewd,
// so the game clamps all LODs down to 0 unless ModelSetMinLOD/MaxLOD is
// called at startup — which it is in the real client).
// ============================================================================

int RenderService::ComputeSelectedLod() const {
    int ov = lodOverride_.load();
    if (ov >= 0) return std::clamp(ov, 0, 3);

    // Approximate viewDist as camera-to-origin (models sit near origin in
    // this viewer). Previewd uses the model-to-world translation column
    // transformed by view — at origin they collapse to the same scalar.
    Vector3f camPos = scene_->Camera().GetSource();
    float viewDist = std::sqrt(camPos.x*camPos.x + camPos.y*camPos.y + camPos.z*camPos.z);
    if (viewDist < 1.0f) return 0;

    const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    Matrix44f proj = scene_->Camera().ProjectionLH(aspect);
    // Previewd reads proj.b1 = m11 (row-major[1][1]) — the vertical scale.
    float projM11 = proj.data[1][1];
    float screenPixels = projM11 / viewDist * (float)height_ * 0.5f;
    if (screenPixels <= 0.001f) return 3;
    float deviation = 5.0f / screenPixels;

    static constexpr float kDeviations[4] = {0.0f, 1.0f, 2.0f, 4.0f};
    int selectedLOD = 4;
    do { --selectedLOD; }
    while (selectedLOD > 0 && deviation <= kDeviations[selectedLOD]);
    return std::clamp(selectedLOD, 0, 3);
}

// ============================================================================
// Device initialization
// ============================================================================

bool RenderService::InitDevice(gfx::GfxApi api) {
    // Create GFX device (handles API-specific device + context creation internally)
    gfx_ = gfx::CreateDevice(api);
    if (!gfx_) return false;

    // SamplerAssetManager owns every gfx::SamplerHandle. The four wrap-flag
    // variants and the linear-wrap sampler are created lazily on first use.
    samplers_ = std::make_unique<SamplerAssetManager>(*gfx_);

    // Constant buffer (via GFX)
    cbPerFrame_ = gfx_->CreateBuffer({
        .size  = sizeof(CBPerFrame),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });

    // TextureAssetManager owns every gfx::TextureHandle in the renderer.
    // Its constructor allocates the 5 default 1x1 fallbacks (white / black /
    // flat normal / neutral ORM / missing-magenta) — see GetDefaults().
    textures_     = std::make_unique<TextureAssetManager>(*gfx_);
    // ReplaceableTextureManager owns the team-colour state and the live
    // 1x1 HD swatch GPU texture bound at t4. Allocated lazily on first
    // GetHdSwatchTexture(); per-model slots arrive via RegisterModelSlot.
    replaceables_ = std::make_unique<ReplaceableTextureManager>(*gfx_, *textures_);

    // Shaders + pipelines + default geometry (grid, view cube) are part of device init
    if (!CreateShaders())          { CleanupD3D(); return false; }
    if (!CreatePipelines())        { CleanupD3D(); return false; }
    if (!CreateDefaultResources()) { CleanupD3D(); return false; }

    // BLS programs are mandatory -- SD, HD, and SD_on_HD all drive through
    // BLS now that the legacy Slang mesh/skin family is gone. Bail with a
    // clean false if any of the three fails to load so callers see a
    // proper error instead of a silently broken scene.
    if (!InitBlsShaders()) { CleanupD3D(); return false; }

    return true;
}

bool RenderService::InitBlsShaders() {
    if (!gfx_ || !scene_->ActiveContentProvider()) return false;

    // ReplaceableTextureManager needs the same provider to load
    // canonical replaceable assets (cliff/tree BLPs) for ids 11..36
    // — wired here so it's available before the first model stage.
    if (replaceables_) replaceables_->SetContentProvider(scene_->ActiveContentProvider());

    blsShaderCache_ = std::make_unique<bls::BlsShaderCache>(gfx_.get(), scene_->ActiveContentProvider());
    blsPrograms_    = std::make_unique<bls::BlsProgramCatalog>(blsShaderCache_.get());
    blsPsoBuilder_  = std::make_unique<bls::BlsPsoBuilder>(gfx_.get());

    blsSdProgram_ = blsPrograms_->Load({
        bls::GxShaderID::SD, "SD_HighSpec", "SD"
    });
    blsSdOnHdProgram_ = blsPrograms_->Load({
        bls::GxShaderID::SD_on_HD, "SD_on_HD", "SD_on_HD"
    });
    blsHdProgram_ = blsPrograms_->Load({
        bls::GxShaderID::HD, "HD", "HD"
    });

    // Tonemap pair (engine slot 14 = Sprite VS + Tonemap PS). Both come
    // through the BLS shader cache. The catalog's Load() expects matched
    // mat-shader names so we acquire each stage directly here.
    blsSpriteVs_  = blsShaderCache_->Acquire(gfx::ShaderStage::Vertex, "sprite");
    blsTonemapPs_ = blsShaderCache_->Acquire(gfx::ShaderStage::Pixel,  "tonemap");

    // Tonemap PSO. Sprite VS is a passthrough — `o0.xyz = v0.xyz; o0.w
    // = 1`, copies TEXCOORD0 — so the VB has to ship vertices already
    // in clip space. Three of them form a screen-covering triangle (the
    // standard "fullscreen triangle" trick — bigger than the screen,
    // avoids the diagonal seam of a two-tri quad). Sprite VS uses
    // semantic name "ATTR" with index 0 for position and index 3 for
    // texcoord, matching the engine's PNCT0 vertex layout in
    // GxuFullscreenRenderHelper::Render @0x7ff609adf100 (we just don't
    // bother shipping NORMAL/COLOR slots that the VS ignores).
    if (blsSpriteVs_ && !blsSpriteVs_->permuteHandles.empty()
        && blsTonemapPs_ && !blsTonemapPs_->permuteHandles.empty())
    {
        struct TonemapVertex {
            float x, y, z;     // ATTR0 — clip-space position
            float u, v;        // ATTR3 — sample uv
        };
        // Fullscreen triangle. Vertices outside [-1,1] get clipped, the
        // visible portion covers the entire viewport with UV in [0,1]
        // for the inscribed area.
        static const TonemapVertex kTonemapVerts[3] = {
            // pos=(x, y, z=0)            uv=(u, v)
            { -1.0f,  1.0f, 0.0f,         0.0f, 0.0f },
            {  3.0f,  1.0f, 0.0f,         2.0f, 0.0f },
            { -1.0f, -3.0f, 0.0f,         0.0f, 2.0f },
        };
        tonemapVB_ = gfx_->CreateBuffer({
            .size  = sizeof(kTonemapVerts),
            .usage = gfx::BufferUsage::Vertex,
        }, kTonemapVerts);

        const gfx::InputElement spriteInput[] = {
            {"ATTR", 0, gfx::Format::R32G32B32_FLOAT, 0},
            {"ATTR", 3, gfx::Format::R32G32_FLOAT,    12},
        };
        gfx::GraphicsPipelineDesc tm;
        tm.vs              = blsSpriteVs_->permuteHandles[0];
        tm.ps              = blsTonemapPs_->permuteHandles[0];
        tm.inputLayout     = spriteInput;
        tm.topology        = gfx::PrimitiveTopology::TriangleList;
        tm.blend.enable    = false;
        tm.depthStencil.depthTest  = false;
        tm.depthStencil.depthWrite = false;
        tm.rasterizer.cull     = gfx::CullMode::None;
        tm.rasterizer.frontCCW = true;
        tm.rtvFormat       = gfx::Format::R8G8B8A8_UNORM_SRGB;
        tm.dsvFormat       = gfx::Format::D24_UNORM_S8_UINT;
        tonemapPSO_ = gfx_->CreateGraphicsPipeline(tm);

        // PS-side resources. b1 = exposure (16 B), s0 = linear-clamp
        // sampler bound at draw time. The HDR texture (t0) is per-target
        // and bound inside RunTonemapPass.
        tonemapPsCb_ = gfx_->CreateBuffer({
            .size  = 16,
            .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
        });
        gfx::SamplerDesc sd;
        sd.minFilter = gfx::Filter::Linear;
        sd.magFilter = gfx::Filter::Linear;
        sd.addressU  = gfx::AddressMode::Clamp;
        sd.addressV  = gfx::AddressMode::Clamp;
        sd.addressW  = gfx::AddressMode::Clamp;
        tonemapSampler_ = gfx_->CreateSampler(sd);
    }

    // Allocate per-draw dynamic CBs sized for worst-case 8-light payloads.
    // Path A (SD): 208+48*8 = 592 B for VS, 48 B for PS.
    // Path B (HD/SD_on_HD): 288 B for VS, 336+64*8 = 848 B for PS.
    blsSdVsCb_ = gfx_->CreateBuffer({
        .size  = sizeof(bls::SdVsCbA),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });
    // SD classic PS CB — 48 B at register b0 (alphaRef + fog).
    blsSdPsCb_ = gfx_->CreateBuffer({
        .size  = sizeof(bls::SdPsCbA),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });
    blsHdVsCb_ = gfx_->CreateBuffer({
        .size  = sizeof(bls::HdVsCb),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });
    blsHdPsCb_ = gfx_->CreateBuffer({
        .size  = sizeof(bls::HdPsCb),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });
    blsSdOnHdPsCb_ = gfx_->CreateBuffer({
        .size  = sizeof(bls::SdOnHdPsCb),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });
    blsHdDebugVisCb_ = gfx_->CreateBuffer({
        .size  = sizeof(bls::DebugVisCb),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });

    // Generate the 128x128 split-sum BRDF LUT once at device init. Matches
    // Previewd's UpdateSplitSumTexture (0x14036daf0) lazy-init pattern
    // (we just do it eagerly -- at 1024 samples the CPU cost is ~15 ms
    // single-threaded, trivial compared to the first-frame shader compile).
    textures_->RegisterOwned(kIblSplitSumLutName, ibl::CreateSplitSumLutTexture(*gfx_));

    // Default to the portrait-tuned probe — softer, horizontally
    // isotropic, no sharp sun/horizon features. Avoids the "dark
    // patches in concavities" look that Day_IBL's directional
    // content produces on close-range preview subjects. SetEnvProbe()
    // swaps at runtime via the UI combo.
    SetEnvProbe(ibl::kPortraitIblPath);

    // All four BLS chains are required:
    //   * SD mesh draws route through blsSdProgram_;
    //   * HD-mode mesh draws pick blsHdProgram_ (pure HD/Crystal) or
    //     blsSdOnHdProgram_ (legacy SD under HD mode);
    //   * tonemap.bls is the LDR resolve at the end of every frame —
    //     all 3D PSOs target RGBA16F, so without the tonemap pass the
    //     back-buffer never gets written and the user sees nothing.
    return blsSdProgram_     != nullptr
        && blsSdOnHdProgram_ != nullptr
        && blsHdProgram_     != nullptr
        && blsSpriteVs_      != nullptr
        && blsTonemapPs_     != nullptr
        && tonemapPSO_       != gfx::PipelineHandle::Invalid;
}

void RenderService::SetEnvProbe(const std::string& relPath) {
    if (!gfx_ || !textures_) return;

    // Drop the previous probe pair. Aliasing convention: only the "from"
    // probe is registered; the bind site reuses it for t14 when "to" is
    // unregistered, so a single ReleaseOwned suffices here.
    textures_->ReleaseOwned(kIblFromProbeName);
    textures_->ReleaseOwned(kIblToProbeName);

    gfx::TextureHandle fromHandle = gfx::TextureHandle::Invalid;
    int mips = 0;
    if (!relPath.empty() && scene_->ActiveContentProvider()) {
        auto probe = ibl::LoadEnvProbe(*gfx_, *scene_->ActiveContentProvider(), relPath);
        if (probe.handle != gfx::TextureHandle::Invalid) {
            fromHandle = probe.handle;
            mips       = probe.mipCount;
        }
    }
    if (fromHandle == gfx::TextureHandle::Invalid) {
        OutputDebugStringA("[WDEX IBL] probe load failed — using procedural debug probe\n");
        fromHandle = ibl::CreateDebugFacesEnvProbe(*gfx_);
        mips       = ibl::kEnvProbeMipLevels;
    }
    textures_->RegisterOwned(kIblFromProbeName, fromHandle);
    iblProbeMipEnd_ = static_cast<float>(mips - 1);
}

void RenderService::ShutdownBlsShaders() {
    blsSdProgram_     = nullptr;
    blsSdOnHdProgram_ = nullptr;
    blsHdProgram_     = nullptr;
    // Sprite VS + tonemap PS are reference-counted by the BLS shader
    // cache; the ReleaseAll() below tears down their handles. Clear
    // the borrowed pointers so a subsequent InitBlsShaders() doesn't
    // read a dangling entry.
    blsSpriteVs_      = nullptr;
    blsTonemapPs_     = nullptr;
    if (gfx_) {
        gfx_->Destroy(blsSdVsCb_);     blsSdVsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsSdPsCb_);     blsSdPsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsHdVsCb_);     blsHdVsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsHdPsCb_);     blsHdPsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsSdOnHdPsCb_); blsSdOnHdPsCb_ = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsHdDebugVisCb_); blsHdDebugVisCb_ = gfx::BufferHandle::Invalid;
        // IBL probes + split-sum LUT are owned by TextureAssetManager;
        // release them so a subsequent InitBlsShaders() can re-register
        // freshly without stale handles lingering in the manager.
        if (textures_) {
            textures_->ReleaseOwned(kIblFromProbeName);
            textures_->ReleaseOwned(kIblToProbeName);
            textures_->ReleaseOwned(kIblSplitSumLutName);
        }
    }
    if (blsPsoBuilder_)  blsPsoBuilder_->Clear();
    if (blsPrograms_)    blsPrograms_->Clear();
    if (blsShaderCache_) blsShaderCache_->ReleaseAll();
    blsPsoBuilder_.reset();
    blsPrograms_.reset();
    blsShaderCache_.reset();
}

RenderTargetId RenderService::CreateSwapChainTarget(void* nativeWindowHandle, int w, int h) {
    if (!gfx_) return 0;

    RenderTarget target;
    target.id   = nextTargetId_++;
    target.swap  = gfx_->CreateSwapChain(nativeWindowHandle, w, h);
    if (target.swap == gfx::SwapChainHandle::Invalid) return 0;

    target.color    = gfx_->GetSwapChainBackBuffer(target.swap);
    target.hdrColor = gfx_->CreateColorTarget(w, h, kHdrSceneFormat);
    target.depth    = gfx_->CreateDepthTarget(w, h, gfx::Format::D24_UNORM_S8_UINT);
    target.width    = w;
    target.height   = h;

    RenderTargetId id = target.id;
    targets_[id] = target;
    return id;
}

RenderTargetId RenderService::CreateOffscreenTarget(int w, int h) {
    if (!gfx_) return 0;

    RenderTarget target;
    target.id       = nextTargetId_++;
    target.color    = gfx_->CreateColorTarget(w, h, gfx::Format::R8G8B8A8_UNORM);
    target.hdrColor = gfx_->CreateColorTarget(w, h, kHdrSceneFormat);
    target.depth    = gfx_->CreateDepthTarget(w, h, gfx::Format::D24_UNORM_S8_UINT);
    target.width    = w;
    target.height   = h;

    if (target.color == gfx::TextureHandle::Invalid) return 0;

    RenderTargetId id = target.id;
    targets_[id] = target;
    return id;
}

void RenderService::DestroyRenderTarget(RenderTargetId id) {
    auto it = targets_.find(id);
    if (it == targets_.end()) return;
    auto& t = it->second;
    if (t.swap != gfx::SwapChainHandle::Invalid) {
        gfx_->DestroySwapChain(t.swap);
    } else {
        gfx_->Destroy(t.color);
    }
    gfx_->Destroy(t.hdrColor);
    gfx_->Destroy(t.depth);
    targets_.erase(it);
}

void RenderService::ResizeRenderTarget(RenderTargetId id, int w, int h) {
    auto it = targets_.find(id);
    if (it == targets_.end() || !gfx_) return;
    auto& t = it->second;

    // Destroy old depth + HDR scene target. The HDR target is owned for
    // every render target (swap-chain back-buffer and offscreen alike) so
    // it always needs explicit re-creation on resize.
    gfx_->Destroy(t.depth);
    gfx_->Destroy(t.hdrColor);

    if (t.swap != gfx::SwapChainHandle::Invalid) {
        gfx_->ResizeSwapChain(t.swap, w, h);
        t.color = gfx_->GetSwapChainBackBuffer(t.swap);
    } else {
        gfx_->Destroy(t.color);
        t.color = gfx_->CreateColorTarget(w, h, gfx::Format::R8G8B8A8_UNORM);
    }

    t.hdrColor = gfx_->CreateColorTarget(w, h, kHdrSceneFormat);
    t.depth    = gfx_->CreateDepthTarget(w, h, gfx::Format::D24_UNORM_S8_UINT);
    t.width    = w;
    t.height   = h;
}

void RenderService::ResizePrimaryTarget(int w, int h) {
    auto* target = primaryTarget();
    if (!target || !gfx_) return;
    ResizeRenderTarget(target->id, w, h);
    width_ = w;
    height_ = h;
}

void RenderService::CleanupD3D() {
    // Release BLS resources first — they hold gfx handles that would otherwise
    // leak past gfx_.reset().
    ShutdownBlsShaders();

    // Destroy GFX resources
    if (gfx_) {
        // Shaders + pipelines. line.slang lives here; the BLS stack
        // owns its own shader cache (released in ShutdownBlsShaders
        // above) and DebugRenderer owns viewcube.slang.
        gfx_->Destroy(lineVS_);  gfx_->Destroy(linePS_);
        gfx_->Destroy(linePSO_);
        gfx_->Destroy(tonemapPSO_);
        gfx_->Destroy(tonemapVB_);
        gfx_->Destroy(tonemapPsCb_);
        gfx_->Destroy(tonemapSampler_);
        tonemapPSO_     = gfx::PipelineHandle::Invalid;
        tonemapVB_      = gfx::BufferHandle::Invalid;
        tonemapPsCb_    = gfx::BufferHandle::Invalid;
        tonemapSampler_ = gfx::SamplerHandle::Invalid;

        // Resources
        gfx_->Destroy(cbPerFrame_);
        // Asset managers destroy their owned handles on reset.
        // ReplaceableTextureManager runs first so it can drop the live
        // swatch handle through gfx_ before the device-owning managers go.
        if (replaceables_) replaceables_->Shutdown();
        replaceables_.reset();
        samplers_.reset();
        textures_.reset();

        // Grid + ViewCube (owned by DebugRenderer)
        debug_->DestroyResources();

        // PE2 service VB
        gfx_->Destroy(particleServiceVB_);
        particleServiceVB_ = gfx::BufferHandle::Invalid;
        particleServiceVBSize_ = 0;

        // Render targets
        for (auto& [id, t] : targets_) {
            if (t.swap != gfx::SwapChainHandle::Invalid)
                gfx_->DestroySwapChain(t.swap);
            else
                gfx_->Destroy(t.color);
            gfx_->Destroy(t.hdrColor);
            gfx_->Destroy(t.depth);
        }
    }
    targets_.clear();
    primaryTargetId_ = 0;

    gfx_.reset();
}

// ============================================================================
// Shaders — loaded from precompiled DXBC bytecode (Slang → slangc → DXBC)
// ============================================================================

bool RenderService::CreateShaders() {
    using namespace WhiteoutDex::Shaders;

    // Slang-compiled assets on the RenderService side: line.slang (debug
    // overlay only). viewcube.slang is loaded by DebugRenderer; the
    // Sprite VS for the tonemap pass comes from sprite.bls via the BLS
    // shader cache (matches engine slot 14 = Sprite VS + Tonemap PS).
    lineVS_ = gfx_->CreateShader(gfx::ShaderStage::Vertex, kLineVS, sizeof(kLineVS));
    linePS_ = gfx_->CreateShader(gfx::ShaderStage::Pixel,  kLinePS, sizeof(kLinePS));

    return lineVS_ != gfx::ShaderHandle::Invalid
        && linePS_ != gfx::ShaderHandle::Invalid;
}

// ============================================================================
// Pipeline (PSO) Creation
// ============================================================================

bool RenderService::CreatePipelines() {
    using namespace gfx;

    InputElement lineInput[] = {
        {"POSITION", 0, Format::R32G32B32_FLOAT,    0},
        {"COLOR",    0, Format::R32G32B32A32_FLOAT, 12},
    };

    // Line PSO (grid, collision wireframes, light markers, ViewCube edges).
    // Drawn into the HDR scene target alongside the mesh passes, so
    // `rtvFormat` matches `kHdrSceneFormat` — the line color is just a
    // float4 the PS writes verbatim, so ACES on the tonemap pass leaves
    // it visually identical.
    GraphicsPipelineDesc desc;
    desc.vs          = lineVS_;
    desc.ps          = linePS_;
    desc.inputLayout = lineInput;
    desc.topology    = PrimitiveTopology::LineList;
    desc.blend.enable = false;
    desc.depthStencil = {};  // defaults: test+write, LessEqual
    desc.rasterizer.cull     = CullMode::None;
    desc.rasterizer.frontCCW = true;
    desc.rtvFormat   = kHdrSceneFormat;
    linePSO_ = gfx_->CreateGraphicsPipeline(desc);

    // The tonemap PSO is built in InitBlsShaders — it can't be built
    // here because its PS comes from the BLS shader cache, and
    // InitBlsShaders runs *after* CreatePipelines in InitDevice.
    return linePSO_ != PipelineHandle::Invalid;
}

// ============================================================================
// Default Resources (Grid)
// ============================================================================

bool RenderService::CreateDefaultResources() {
    return debug_->CreateResources();
}

// ============================================================================
// Frame Rendering
// ============================================================================

void RenderService::RenderFrame(RenderTargetId targetId) {
    auto it = targets_.find(targetId);
    if (it == targets_.end()) return;
    auto& target = it->second;
    if (target.color    == gfx::TextureHandle::Invalid || !gfx_) return;
    if (target.hdrColor == gfx::TextureHandle::Invalid)         return;

    // Re-sample the active MDX camera animator every frame.
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        const int activeIdx = scene_->ActiveCameraPresetIdx();
        const auto& presets = scene_->CameraPresets();
        if (activeIdx >= 0 && activeIdx < (int)presets.size()) {
            const auto& preset = presets[activeIdx];
            if (preset.animator) {
                int seqStart = 0, seqEnd = 0;
                Actor* focus = scene_->FocusActor();
                int idx = focus ? focus->animation.ActiveSequenceIndex() : 0;
                const auto& ranges = scene_->SequenceRanges();
                if (idx >= 0 && idx < (int)ranges.size()) {
                    seqStart = ranges[idx].startMs;
                    seqEnd   = ranges[idx].endMs;
                }
                if (seqStart == 0 && seqEnd == 0) {
                    seqEnd = 1 << 30;
                }
                Vector3f pos  = preset.position;
                Vector3f tgt  = preset.target;
                float    roll = preset.staticRoll;
                preset.animator(pos, tgt, roll,
                                scene_->GetAnimationTime(), seqStart, seqEnd);
                scene_->Camera().SetDirectPose(pos, tgt, roll);
            }
        }
    }

    auto* cmd = gfx_->GetImmediateContext();
    // 3D pass: render every mesh / particle / ribbon / debug overlay into
    // the linear-HDR scene target. The tonemap pass at the end of this
    // function samples it and writes the LDR back-buffer. The BLS PSOs
    // are baked for RGBA16F so the HDR target is the only valid sink for
    // 3D draws — if tonemap.bls failed to load we'd have already bailed
    // out of InitBlsShaders.
    // Scene clear written into the R11G11B10F HDR target, then ACES +
    // sRGB-encoded at tonemap time. The user picks an sRGB colour via
    // the toolbar background-color picker (default ≈ dark cool gray);
    // we convert sRGB→linear here so the post-tonemap pixel matches
    // the picked swatch closely (small sRGB values fall in the linear
    // ramp and survive ACES with minimal compression).
    auto srgbByteToLinear = [](uint8_t b) {
        // Standard piecewise sRGB EOTF.
        const float f = b / 255.0f;
        return (f <= 0.04045f) ? (f / 12.92f)
                               : std::pow((f + 0.055f) / 1.055f, 2.4f);
    };
    const uint32_t bg = backgroundColor_.load();
    float clearColor[4] = {
        srgbByteToLinear((uint8_t)(bg        & 0xFF)),  // R
        srgbByteToLinear((uint8_t)((bg >> 8) & 0xFF)),  // G
        srgbByteToLinear((uint8_t)((bg >> 16) & 0xFF)), // B
        1.0f,
    };
    cmd->BeginRenderPass(target.hdrColor, target.depth, clearColor, 1.0f, 0);
    cmd->SetViewport({0, 0, (float)target.width, (float)target.height, 0, 1});

    Matrix44f view, proj;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        view = scene_->Camera().GetViewMatrix();
    }
    float aspect = (target.height > 0) ? (float)target.width / (float)target.height : 1.0f;
    proj = scene_->Camera().ProjectionRH(aspect);

    // Update constant buffer (via GFX MapBuffer)
    {
        render_detail::CbPerFrameDesc d;
        d.view         = view;
        d.projection   = proj;
        d.lightDir     = render_detail::NormalizedLightDir4(kDefaultLightDir);
        d.lightColor   = kGeosetLightColor;
        d.ambientColor = {kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z, 0.0f};
        render_detail::WriteCbPerFrame(gfx_.get(), cbPerFrame_, d);
    }

    if (showGrid_) debug_->RenderGrid();
    RenderGeosets();
    if (showParticles_) RenderParticlesBls();
    if (showRibbons_) RenderRibbons();
    if (showCollisions_) debug_->RenderCollisions();
    if (showLights_)     debug_->RenderLightMarkers();
    debug_->RenderViewCube();
    cmd->EndRenderPass();

    // Tonemap pass: HDR scene → LDR back-buffer.
    RunTonemapPass(target);
}

void RenderService::RunTonemapPass(const RenderTarget& target) {
    if (tonemapPSO_ == gfx::PipelineHandle::Invalid) return;
    auto* cmd = gfx_->GetImmediateContext();

    // No depth — the tonemap pass is a single covering triangle and we
    // disabled depth in the PSO. The clear color is a formality: the
    // fullscreen triangle writes every pixel before Present. We pick
    // black so the rare case of a degenerate viewport / invalid HDR
    // sample doesn't flash a stale frame.
    const float clearLdr[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    cmd->BeginRenderPass(target.color, gfx::TextureHandle::Invalid,
                         clearLdr, 1.0f, 0);
    cmd->SetViewport({0, 0, (float)target.width, (float)target.height, 0, 1});

    // Upload exposure (b1, 16 B). Single-float payload padded to a float4
    // to match TonemapPSPerDraw on the shader side.
    if (tonemapPsCb_ != gfx::BufferHandle::Invalid) {
        if (void* mapped = gfx_->MapBuffer(tonemapPsCb_)) {
            float cb[4] = {tonemapExposure_, 0.0f, 0.0f, 0.0f};
            std::memcpy(mapped, cb, sizeof(cb));
            gfx_->UnmapBuffer(tonemapPsCb_);
        }
    }

    cmd->BindPipeline(tonemapPSO_);
    cmd->BindVertexBuffer(0, tonemapVB_, sizeof(float) * 5);  // pos float3 + uv float2 = 20 B
    cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, target.hdrColor);
    cmd->BindSampler       (gfx::ShaderStage::Pixel, 0, tonemapSampler_);
    cmd->BindConstantBuffer(gfx::ShaderStage::Pixel, 1, tonemapPsCb_);
    cmd->Draw(3, 0);  // fullscreen triangle, 3 verts from tonemapVB_
    cmd->EndRenderPass();
}

void RenderService::Present(RenderTargetId targetId) {
    auto it = targets_.find(targetId);
    if (it != targets_.end() && it->second.swap != gfx::SwapChainHandle::Invalid)
        gfx_->Present(it->second.swap);
}


// ============================================================================
// Geoset Rendering (4-bucket sort by FilterMode, like Magos Model::Render)
// ============================================================================

// ============================================================================
// GeosetPassBls — SD-path mesh draw. Reuses Blizzard's SD VS + SD PS with
// compute-skinned geometry (geo.vb already carries animated vertices, so
// the VS picks the numWeights=0 permute). Legacy Slang mesh shader is the
// fallback when blsSdProgram_ failed to load.
// ============================================================================
class GeosetPassBls : public BlsGeosetPass<GeosetPassBls> {
public:
    using BlsGeosetPass::BlsGeosetPass;

    bool IsAvailable() const {
        return rs_.blsSdProgram_ && rs_.blsPsoBuilder_;
    }

    void ComputeViewProj(Matrix44f& view, Matrix44f& proj) const {
        std::lock_guard<std::mutex> lock(rs_.dataMutex_);
        view = rs_.scene_->Camera().GetViewMatrix();
        const float aspect = (rs_.height_ > 0)
            ? static_cast<float>(rs_.width_) / static_cast<float>(rs_.height_) : 1.0f;
        proj = rs_.scene_->Camera().ProjectionRH(aspect);
    }

    void BindPassResources(gfx::IGFXCommandList*, bls::FrameInputs&) const {
        // SD path uses only the base t0 sampler the CRTP driver already bound.
    }

    bls::BaselineLights Baseline() const {
        // RH view: forward = -Z, so direction-to-source for a camera-attached
        // headlight is a constant +Z in view space (no per-pose transform).
        return {
            /*ambient*/       { kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z },
            /*diffuse*/       { kGeosetLightColor.x,   kGeosetLightColor.y,   kGeosetLightColor.z },
            /*dirToSourceVS*/ { 0.0f, 0.0f, 1.0f },
        };
    }

    void DrawGeoset(const render_detail::GeosetRef& ref,
                    bls::FrameInputs&               frame,
                    const Matrix44f&                /*view*/,
                    gfx::IGFXCommandList*           cmd,
                    int                             lightCountForGeoset) {
        const auto& view_ = *ref.view;
        const auto& geo   = (*view_.geosets)[ref.idx];

        const GPUMaterial* mat = nullptr;
        const int matId = geo.materialId;
        if (matId >= 0 && matId < (int)view_.materials->size())
            mat = &(*view_.materials)[matId];

        const float geoAlpha = geo.geosetAlpha * view_.parentVisibility;
        // Previewd's geoset-hidden gate is the byte `flags & 1` flag, which
        // is set iff `ftol(clamp(animatedAlpha,0,1) * proceduralAlpha)` is
        // non-zero (CalcGeosetColor @0x140199970). Any alpha that quantizes
        // to byte 0 — i.e. < 0.5/255 ≈ 0.00196 — would be skipped; anything
        // else is drawn at the corresponding byte alpha. Using <= 0 keeps
        // that semantic without forcing the quantization math here.
        if (geoAlpha <= 0.0f) return;

        int numLayers = mat ? (int)mat->cpu.layers.size() : 0;
        if (numLayers <= 0) numLayers = 1;

        // Bind slot 0 = unskinnedVb + index buffer, plus the bone stream
        // and palette CB when this geoset has skinning data. The returned
        // hasBones flag drives the permute + input layout below so the VS
        // picks FourBoneSkinning vs the pass-through permute consistently.
        const bool hasBones = render_detail::BindSdMeshGeometry(cmd, geo);

        const auto layout = hasBones
            ? bls::VertexLayoutKind::ParticleSDSkinned
            : bls::VertexLayoutKind::ParticleSD;

        // Per-layer state we need to derive twice (once for the prepass
        // sweep, once for the color sweep). Compute once up front to
        // keep the two passes consistent and to avoid double-tracing
        // FromMdxLayer + UnpackLayer.
        struct LayerJob {
            render_detail::UnpackedLayer layer;
            bls::MatParams               mp;
            int                          activeN = 0;
            bool                         unlit   = false;
            bool                         isOpaqueFading = false;
            bool                         valid   = false;
        };
        std::vector<LayerJob> jobs(numLayers);

        for (int li = 0; li < numLayers; ++li) {
            jobs[li].layer = render_detail::UnpackLayer(mat, li);
            const auto& layer = jobs[li].layer;
            const float combinedAlpha = geoAlpha * layer.alpha;
            if (combinedAlpha < 0.004f) continue;
            // isOpaqueFading mirror (RenderGeosetLayers @0x7ff609afcf70):
            // an authored opaque/AlphaKey layer that's currently fading
            // promotes to Blend and triggers a depth-prepass clone.
            const bool isOpaqueFading =
                combinedAlpha < 0.99f && layer.filterMode <= FILTER_TRANSPARENT;
            int effectiveFilter = layer.filterMode;
            if (isOpaqueFading)
                effectiveFilter = FILTER_BLEND;

            bls::MatParams mp = bls::FromMdxLayer(effectiveFilter, layer.flags, bls::GxShaderID::SD);
            if (mp.alpha == bls::GxMatAlpha::Modulate) {
                mp.diffuseColor = {combinedAlpha, 1, 1, 1};
            } else {
                mp.diffuseColor = {geo.geosetColor.x, geo.geosetColor.y, geo.geosetColor.z, combinedAlpha};
            }
            const bool unlit = (mp.disables & bls::kDisableLighting) != 0;
            const int  activeN = unlit ? 0 : lightCountForGeoset;

            jobs[li].mp = mp;
            jobs[li].activeN = activeN;
            jobs[li].unlit   = unlit;
            jobs[li].isOpaqueFading = isOpaqueFading;
            jobs[li].valid   = true;
        }

        auto issueDraw = [&](const LayerJob& job, const bls::MatParams& matParams) {
            frame.numLights = job.activeN;
            render_detail::ApplyTexAnimPaletteToFrame(frame, view_.texAnimPalette, job.layer.textureAnimationId);

            const auto rsLocal   = bls::MakeSdMeshRenderState(matParams, job.activeN, job.unlit, hasBones);
            const auto permLocal = bls::SelectPermutes(rsLocal);
            const auto reqLocal  = bls::MakePsoRequest(rs_.blsSdProgram_,
                                                       layout,
                                                       matParams, permLocal);
            auto pso = rs_.blsPsoBuilder_->GetOrBuild(reqLocal);
            if (pso == gfx::PipelineHandle::Invalid) return;
            cmd->BindPipeline(pso);

            frame.world = view_.worldTransform;

            if (auto vs = bls::ScopedCb<bls::SdVsCbA>(rs_.gfx_.get(), rs_.blsSdVsCb_)) {
                bls::BuildSdVsCbA(*vs, frame, matParams);
            }
            if (auto ps = bls::ScopedCb<bls::SdPsCbA>(rs_.gfx_.get(), rs_.blsSdPsCb_)) {
                bls::BuildSdPsCbA(*ps, frame, matParams);
            }
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, rs_.blsSdVsCb_);
            cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, rs_.blsSdPsCb_);

            render_detail::BindLayerAlbedo(cmd, view_.textures, job.layer.textureId,
                                           rs_.textures_->GetDefaults().White,
                                           *rs_.samplers_);

            cmd->DrawIndexed(geo.indexCount);
        };

        // Pass 1 — depth prepass sweep. Mirrors the engine's separately-
        // sorted DEPTHFILL_DEPTH clone (RenderGeosetLayers @0x7ff609afcf70
        // skips non-fading layers in the DEPTH branch). Writing z first,
        // before any layer's color, is what keeps a fading-opaque layer
        // from clobbering an earlier BLEND layer's expected ordering: the
        // BLEND layer's color pass in pass 2 simply tests against the
        // already-locked z. SelectModelMaterial DEPTHFILL_DEPTH state:
        //   diffuseColor = (1,1,1,1)
        //   m_alpha      = Blend           (alphaRef = 4/255)
        //   m_disables  &= ~kDisableDepthWrite
        //   m_disables  |=  kDisableBit8   (color writes off)
        for (int li = 0; li < numLayers; ++li) {
            if (!jobs[li].valid || !jobs[li].isOpaqueFading) continue;
            bls::MatParams prepass = jobs[li].mp;
            prepass.diffuseColor = {1.0f, 1.0f, 1.0f, 1.0f};
            prepass.disables &= ~bls::kDisableDepthWrite;
            prepass.disables |=  bls::kDisableBit8;
            issueDraw(jobs[li], prepass);
        }

        // Pass 2 — color sweep. All layers in source (stack) order.
        for (int li = 0; li < numLayers; ++li) {
            if (!jobs[li].valid) continue;
            issueDraw(jobs[li], jobs[li].mp);
        }
    }
};

bool RenderService::RenderGeosetsBls() {
    return GeosetPassBls{*this}.Run();
}

// HD-path mesh draw. Runs only when renderMode_ == HD. For Phase 2 this is a
// deliberate pass-through: HD VS permute with hasTangent=0, numTexCoords=1,
// no skinning (compute pass already baked world-space verts into geo.vb); HD
// PS permute with HAS_IBL=0, lights=0, no shadows, no prepass -- flat albedo
// modulated by vertex color, no BRDF, no IBL. Later phases light this up.
//
// Routing: pure HD/Crystal materials use blsHdProgram_; SD/SD_on_HD
// materials route through blsSdOnHdProgram_ once that program's CB build is
// in place (Phase 4). Until then we also draw those via the HD program;
// visual parity with SD mode will come in later phases.
// ============================================================================
// GeosetPassHd — HD-path mesh draw. Runs only when renderMode_ == HD.
// Drives the HD / SD_on_HD BLS programs with LH view+proj, the full PBR
// texture stack (albedo/normal/ORM/emissive/team-color), IBL env probes +
// split-sum LUT, and the optional debug-vis CB. Falls through to SD when
// the HD program failed to load.
// ============================================================================
class GeosetPassHd : public BlsGeosetPass<GeosetPassHd> {
public:
    using BlsGeosetPass::BlsGeosetPass;

    bool IsAvailable() const {
        return rs_.blsHdProgram_ && rs_.blsPsoBuilder_;
    }

    void ComputeViewProj(Matrix44f& view, Matrix44f& proj) const {
        // HD / SD_on_HD require the LH view + diagonal-FOV projection —
        // see docs/CAMERA_PLAN.md.
        const float aspect = (rs_.height_ > 0)
            ? static_cast<float>(rs_.width_) / static_cast<float>(rs_.height_) : 1.0f;
        std::lock_guard<std::mutex> lock(rs_.dataMutex_);
        view = rs_.scene_->Camera().ViewLH();
        proj = rs_.scene_->Camera().ProjectionLH(aspect);
    }

    void BindPassResources(gfx::IGFXCommandList* cmd, bls::FrameInputs& frame) {
        // Refresh the team-colour swatch once per pass (matches the UI picker).
        // ReplaceableTextureManager re-uploads the 1×1 texture only when the
        // colour actually changed since the last call.
        rs_.replaceables_->GetHdSwatchTexture();

        // Day at t13 / Night at t14 — envTransitionT=0.75 picks mostly day.
        // envMipEnd clamps the roughness→mip remap; nonzero keeps sampleIBL
        // off the "probe disabled" fast-out path.
        frame.envFromMipEnd  = rs_.iblProbeMipEnd_;
        frame.envToMipEnd    = rs_.iblProbeMipEnd_;
        frame.envTransitionT = 0.75f;

        // Dynamic sampler table covers s0..s3. Per-layer BindSampler(0, ...)
        // overrides s0 with the wrap-appropriate variant at draw time.
        const gfx::SamplerHandle linWrap = rs_.samplers_->LinearWrap();
        cmd->BindSampler(gfx::ShaderStage::Pixel, 1, linWrap);
        cmd->BindSampler(gfx::ShaderStage::Pixel, 2, linWrap);
        cmd->BindSampler(gfx::ShaderStage::Pixel, 3, linWrap);

        // s13..s15 are STATIC samplers baked into the root signature, so
        // we only bind the SRVs here. Binding them via the dynamic heap
        // would overflow D3D12's 2048-entry sampler cap and TDR.
        // Aliasing convention: when the "to" probe load failed only "from"
        // is registered — reuse its handle for t14 so the PS's two-sample
        // blend still has a valid SRV.
        const gfx::TextureHandle from = rs_.textures_->GetOwned(
            RenderService::kIblFromProbeName);
        gfx::TextureHandle to = rs_.textures_->GetOwned(
            RenderService::kIblToProbeName);
        if (to == gfx::TextureHandle::Invalid) to = from;
        if (from != gfx::TextureHandle::Invalid)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 13, from);
        if (to   != gfx::TextureHandle::Invalid)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 14, to);
        const gfx::TextureHandle lut = rs_.textures_->GetOwned(
            RenderService::kIblSplitSumLutName);
        if (lut != gfx::TextureHandle::Invalid)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 15, lut);
    }

    bls::BaselineLights Baseline() const {
        // HD baseline key (used ONLY when the model has no authored MDX
        // lights). Camera-attached headlight: LH view has forward = +Z so
        // direction-to-source is -Z in view space regardless of camera pose.
        return {
            /*ambient*/       { kHdBaselineAmbientColor.x, kHdBaselineAmbientColor.y, kHdBaselineAmbientColor.z },
            /*diffuse*/       { kHdBaselineLightColor.x,   kHdBaselineLightColor.y,   kHdBaselineLightColor.z },
            /*dirToSourceVS*/ { 0.0f, 0.0f, -1.0f },
        };
    }

    void DrawGeoset(const render_detail::GeosetRef& ref,
                    bls::FrameInputs&               frame,
                    const Matrix44f&                /*view*/,
                    gfx::IGFXCommandList*           cmd,
                    int                             lightCountForGeoset) {
        const auto& view_ = *ref.view;
        const auto& geo   = (*view_.geosets)[ref.idx];

        const GPUMaterial* mat = nullptr;
        const int matId = geo.materialId;
        if (matId >= 0 && matId < (int)view_.materials->size())
            mat = &(*view_.materials)[matId];

        const float geoAlpha = geo.geosetAlpha * view_.parentVisibility;
        // Previewd's geoset-hidden gate is the byte `flags & 1` flag, which
        // is set iff `ftol(clamp(animatedAlpha,0,1) * proceduralAlpha)` is
        // non-zero (CalcGeosetColor @0x140199970). Any alpha that quantizes
        // to byte 0 — i.e. < 0.5/255 ≈ 0.00196 — would be skipped; anything
        // else is drawn at the corresponding byte alpha. Using <= 0 keeps
        // that semantic without forcing the quantization math here.
        if (geoAlpha <= 0.0f) return;

        int numLayers = mat ? (int)mat->cpu.layers.size() : 0;
        if (numLayers <= 0) numLayers = 1;

        // HD path uses vs/hd.bls with FourBoneSkinning — bind the rest-pose
        // vertex stream on slot 0. The VS blends against vsCB3's bone
        // palette when numWeights>0, or passes geometry through unchanged
        // for static geosets.
        cmd->BindVertexBuffer(0, geo.unskinnedVb, sizeof(Vertex));
        cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);

        // Tangent side-stream (ATTR7 on slot 1). Required by the HD VS
        // when the hasTangent permute is picked.
        const bool hasTangents = (geo.tangentVb != gfx::BufferHandle::Invalid);
        if (hasTangents)
            cmd->BindVertexBuffer(1, geo.tangentVb, sizeof(Vector4f));

        // Bone weights+indices feeding FourBoneSkinning (ATTR5/ATTR6) +
        // per-geoset bone palette CB (vsCB3). Bone slot collapses onto
        // slot 1 when no tangents are present.
        const bool hasBones =
            (geo.boneVb != gfx::BufferHandle::Invalid) &&
            (geo.bonePaletteCb != gfx::BufferHandle::Invalid);
        if (hasBones) {
            const uint32_t boneSlot = hasTangents ? 2u : 1u;
            cmd->BindVertexBuffer(boneSlot, geo.boneVb, sizeof(BoneVertex));
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 3, geo.bonePaletteCb);
        }

        // Per-layer state we need to derive twice (prepass + color sweeps).
        // Precompute up front so prepass runs BEFORE any layer's color
        // pass — otherwise an early BLEND layer's color would be drawn
        // before a later fading-opaque layer's z-write, which can leak z
        // out and cull other transparent geosets behind it.
        struct LayerJob {
            render_detail::UnpackedLayer layer;
            bls::MatParams               mp;
            const bls::BlsProgram*       program = nullptr;
            bls::GxShaderID              programShaderId = bls::GxShaderID::SD_on_HD;
            int                          activeN = 0;
            bool                         unlit   = false;
            bool                         isOpaqueFading = false;
            bool                         valid   = false;
        };
        std::vector<LayerJob> jobs(numLayers);

        for (int li = 0; li < numLayers; ++li) {
            jobs[li].layer = render_detail::UnpackLayer(mat, li);
            const auto& layer = jobs[li].layer;
            float combinedAlpha = geoAlpha * layer.alpha;
            if (combinedAlpha < 0.004f) continue;
            const bool isOpaqueFading =
                combinedAlpha < 0.99f && layer.filterMode <= FILTER_TRANSPARENT;
            int effectiveFilter = layer.filterMode;
            if (isOpaqueFading)
                effectiveFilter = FILTER_BLEND;

            const bool isHdMaterial =
                (layer.shaderId == 1) /* HD */ || (layer.shaderId == 24) /* Crystal */;
            const bls::GxShaderID programShaderId =
                isHdMaterial ? bls::GxShaderID::HD : bls::GxShaderID::SD_on_HD;
            const bls::BlsProgram* program =
                isHdMaterial ? rs_.blsHdProgram_ : rs_.blsSdOnHdProgram_;

            bls::MatParams mp = bls::FromMdxLayer(effectiveFilter, layer.flags, programShaderId);
            if (mp.alpha == bls::GxMatAlpha::Modulate) {
                mp.diffuseColor = {combinedAlpha, 1, 1, 1};
            } else {
                mp.diffuseColor = {geo.geosetColor.x, geo.geosetColor.y, geo.geosetColor.z, combinedAlpha};
            }
            mp.emissiveGain     = layer.emissiveGain;
            mp.fresnelTeamColor = layer.fresnelTeamColor;
            mp.fresnelOpacity   = layer.fresnelOpacity;
            mp.fresnelColor     = layer.fresnelColor;

            const bool unlit   = (mp.disables & bls::kDisableLighting) != 0;
            const int  activeN = unlit ? 0 : lightCountForGeoset;

            jobs[li].mp = mp;
            jobs[li].program = program;
            jobs[li].programShaderId = programShaderId;
            jobs[li].activeN = activeN;
            jobs[li].unlit   = unlit;
            jobs[li].isOpaqueFading = isOpaqueFading;
            jobs[li].valid   = true;
        }

        auto issueHdDraw = [&](const LayerJob& job, const bls::MatParams& matParams) {
            const auto& layer = job.layer;
            const bool unlit  = job.unlit;
            const int  activeN = job.activeN;
            const bls::GxShaderID programShaderId = job.programShaderId;
            const bls::BlsProgram* program = job.program;
            frame.numLights = activeN;
            render_detail::ApplyTexAnimPaletteToFrame(frame, view_.texAnimPalette, layer.textureAnimationId);
            {
            bls::RenderState rs;
            rs.shaderId       = programShaderId;
            rs.alphaMode      = static_cast<uint8_t>(matParams.alpha);
            rs.numColors      = 0;
            rs.numTexCoords   = 1;
            // numTangents drives the VS permute's hasTangent dimension
            // (see bls_permuter.cpp: `uint32_t(s.numTangents != 0 ? 1 : 0)`).
            // Pick the tangent permute only when we actually bind a
            // tangent stream -- otherwise ATTR7 reads undefined memory.
            rs.numTangents    = hasTangents ? 1 : 0;
            // numWeights drives WeightIndex() in bls_permuter.cpp. Non-zero
            // picks FourBoneSkinning in vs/hd.bls, which consumes ATTR5
            // (weights) + ATTR6 (indices) + vsCB3 (BonePalette). 0 picks
            // the Rigid pass-through for unskinned geosets.
            rs.numWeights     = hasBones ? 4 : 0;
            rs.numLights      = static_cast<uint8_t>(activeN);
            rs.fogEnabled     = false;
            rs.depthWrite     = matParams.DepthWriteEnabled();
            rs.lightingEnabled= !unlit && activeN > 0;
            rs.prepass        = false;
            rs.shadows        = false;
            // Drives the HD PS multiLayer specialisation (TMat =
            // MultiLayerMaterial). Without this the compiler
            // dead-code-strips every `t_teamColor.Sample(...)` call in
            // ps_standard / ps_ibl, the t4 binding vanishes from the
            // PSO, and no amount of runtime binding shows up in
            // RenderDoc. Only enable when the layer actually authored
            // a TeamColor subtexture — the standard perm is what
            // every other HD draw wants.
            // teamColor permute: enable when the layer carries a
            // TeamColor slot — either a live-swatch placeholder
            // (kHdTeamColorActive) or a real authored texture
            // (id >= 0). Anything else (-1) means no slot → leave
            // the permute disabled so ps_standard / ps_ibl strips
            // the t_teamColor sample entirely.
            rs.teamColor      = (layer.teamColorMapId == kHdTeamColorActive)
                             || (layer.teamColorMapId >= 0);
            const int  dbgMode     = rs_.hdDebugMode_.load();
            const bool debugActive = (dbgMode > 0);
            rs.debugShader = debugActive;
            auto perm = bls::SelectPermutes(rs);

            bls::PsoRequest req{};
            req.program   = program;
            req.vsIndex   = perm.vs;
            req.psIndex   = perm.ps;
            req.material  = matParams;
            // Layout picks:
            //   bones + tangent  -> MeshHDSkinned        (slots 0/1/2)
            //   bones, no tangent-> MeshHDSkinnedNoTangent (slots 0/1)
            //   no bones, tangent-> MeshHDTangent        (slots 0/1)
            //   neither          -> ParticleSD           (slot 0 only)
            if (hasBones) {
                req.layout = hasTangents
                    ? bls::VertexLayoutKind::MeshHDSkinned
                    : bls::VertexLayoutKind::MeshHDSkinnedNoTangent;
            } else {
                req.layout = hasTangents
                    ? bls::VertexLayoutKind::MeshHDTangent
                    : bls::VertexLayoutKind::ParticleSD;
            }
            req.topology  = gfx::PrimitiveTopology::TriangleList;
            req.rtvFormat = RenderService::kHdrSceneFormat;
            req.dsvFormat = gfx::Format::D24_UNORM_S8_UINT;
            req.lhClipSpace = true;  // HD/SD_on_HD stack (distinct PSO hash)
            auto pso = rs_.blsPsoBuilder_->GetOrBuild(req);
            if (pso == gfx::PipelineHandle::Invalid) return;
            cmd->BindPipeline(pso);

            frame.world = view_.worldTransform;

            // VS CB layout is shared between HD and SD_on_HD programs.
            if (auto vs = bls::ScopedCb<bls::HdVsCb>(rs_.gfx_.get(), rs_.blsHdVsCb_)) {
                bls::BuildHdVsCb(*vs, frame, matParams);
            }
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 2, rs_.blsHdVsCb_);

            // PS CB layout diverges by program: hd_ps.slang reads PBR
            // fields directly (HdPsCb) while sd_on_hd_ps.slang reads a
            // padded legacy layout (SdOnHdPsCb) with invViewRow rows and
            // lightCountSlot.z bit-reinterpret.
            if (program == rs_.blsHdProgram_) {
                if (auto ps = bls::ScopedCb<bls::HdPsCb>(rs_.gfx_.get(), rs_.blsHdPsCb_)) {
                    bls::BuildHdPsCb(*ps, frame, matParams);
                }
                cmd->BindConstantBuffer(gfx::ShaderStage::Pixel, 2, rs_.blsHdPsCb_);
                // b3 DebugVisCB. Only meaningful when the HAS_DEBUG_VIS
                // permute is picked (rs.debugShader); bound
                // unconditionally so the shader slot is always live
                // even in "mode 0 = normal render" — cheap no-op bind.
                if (auto dbg = bls::ScopedCb<bls::DebugVisCb>(rs_.gfx_.get(), rs_.blsHdDebugVisCb_)) {
                    // UI combo index → shader state:
                    //   0..4: psCB3.debugMode, enabledShaders = 0
                    //   5: albedo override WHITE (shading only)
                    //   6: albedo override 50% GREY
                    //   7: albedo override BLACK (specular only)
                    uint32_t enabled   = 0;
                    int      psMode    = dbgMode;
                    Vector3f overrideA = {0, 0, 0};
                    if (dbgMode >= 5 && dbgMode <= 7) {
                        enabled = 1;  // bit 0 = albedo override
                        psMode  = 0;  // normal render with custom albedo
                        overrideA = (dbgMode == 5) ? Vector3f{1, 1, 1}
                                   : (dbgMode == 6) ? Vector3f{0.5f, 0.5f, 0.5f}
                                                    : Vector3f{0, 0, 0};
                    }
                    dbg->enabledShaders = enabled;
                    // debugMode read via asint() — bit-reinterpret.
                    const uint32_t modeBits = static_cast<uint32_t>(psMode);
                    std::memcpy(&dbg->debugMode, &modeBits, sizeof(float));
                    dbg->_p0[0] = dbg->_p0[1] = 0.0f;
                    dbg->overrideAlbedo = overrideA; dbg->_p1 = 0.0f;
                    dbg->overrideOrm    = {0, 0, 0}; dbg->_p2 = 0.0f;
                }
                cmd->BindConstantBuffer(gfx::ShaderStage::Pixel, 3, rs_.blsHdDebugVisCb_);
            } else {
                if (auto ps = bls::ScopedCb<bls::SdOnHdPsCb>(rs_.gfx_.get(), rs_.blsSdOnHdPsCb_)) {
                    bls::BuildSdOnHdPsCb(*ps, frame, matParams);
                }
                cmd->BindConstantBuffer(gfx::ShaderStage::Pixel, 2, rs_.blsSdOnHdPsCb_);
            }

            // Bind the HD PBR texture stack. Slots:
            //   t0 albedo, t1 normal, t2 ORM (occlusion/roughness/metallic/mask),
            //   t3 emissive, t4 team-color mask.
            // Reforged (v1200+) layers pack these as distinct subtextures;
            // classic layers only ship the diffuse and fall back to
            // defaultTex_ for the rest. White at t2 yields metallic=1 /
            // rough=1 which looks chrome-ish, so HD preview of classic
            // units is impressionistic, not accurate -- but at least not
            // broken.
            // Slot-specific fallback when the layer doesn't author a
            // given subtexture. Binding white everywhere (the old
            // behaviour) makes t_orm.w default to 1.0, which the team-
            // colour multiLayerBlend interprets as "100% team tint over
            // this surface" -- so classic HD layers that lack ORM got
            // their entire albedo overwritten by whatever t4 held.
            // Binding RGBA(0,0,0,0) for ORM / emissive / teamColor
            // zeroes the blend and turns the team-colour contribution
            // off until the source MDX provides the subtextures.
            auto bindMaterialTex = [&](uint32_t slot, int texId,
                                        gfx::TextureHandle fallback,
                                        uint32_t* outWrap) {
                if (texId >= 0 && view_.textures) {
                    const gfx::TextureHandle h = view_.textures->Get(texId);
                    if (h != gfx::TextureHandle::Invalid) {
                        cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, h);
                        if (outWrap) *outWrap = view_.textures->WrapFlags(texId) & kSamplerWrapBitsMask;
                        return true;
                    }
                }
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, fallback);
                return false;
            };

            const auto& defs = rs_.textures_->GetDefaults();
            uint32_t wrapFlags = kSamplerWrapBitsMask;
            bindMaterialTex(0, layer.textureId,      defs.White,      &wrapFlags); // t0 albedo (white)
            bindMaterialTex(1, layer.normalMapId,    defs.FlatNormal, nullptr);    // t1 normal (flat)
            bindMaterialTex(2, layer.ormMapId,       defs.NeutralOrm, nullptr);    // t2 ORM (occlusion=1, roughness=1, metal=0, teamBlend=0)
            bindMaterialTex(3, layer.emissiveMapId,  defs.Black,      nullptr);    // t3 emissive (no glow)
            // t4 team colour. Three cases:
            //   * teamColorMapId == kHdTeamColorActive (-2): the layer's
            //     slot is the WC3 replaceable=1 placeholder, so bind the
            //     ReplaceableTextureManager's live 1×1 swatch — the per-
            //     player tint flows through here exactly as the engine
            //     does it.
            //   * teamColorMapId >= 0: the artist authored a real
            //     texture in this slot (custom mask BLP / DDS). Bind it
            //     through the regular per-actor texture cache like the
            //     other HD slots so the user's texture actually drives
            //     the blend instead of being silently overwritten.
            //   * teamColorMapId < 0 (and not the sentinel): slot is
            //     absent. Bind black so t_orm.w-driven blends stay
            //     neutral.
            if (layer.teamColorMapId == kHdTeamColorActive) {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 4,
                                        rs_.replaceables_->GetHdSwatchTexture());
            } else if (layer.teamColorMapId >= 0) {
                bindMaterialTex(4, layer.teamColorMapId, defs.Black, nullptr);
            } else {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 4, defs.Black);
            }
            cmd->BindSampler(gfx::ShaderStage::Pixel, 0, rs_.samplers_->WrapVariant(wrapFlags));

            cmd->DrawIndexed(geo.indexCount);
            } // body block opened to keep the original local scope structure
        }; // issueHdDraw

        // Pass 1 — depth prepass sweep. Engine RenderGeosetLayers
        // @0x7ff609afcf70 schedules a separately-sorted DEPTHFILL_DEPTH
        // clone that runs the prepass for every fading-opaque layer
        // BEFORE any color pass. SelectModelMaterial DEPTHFILL_DEPTH
        // forces diffuseColor=(1,1,1,1), m_alpha=Blend, depth-write ON,
        // color writes OFF. Running this as a separate sweep avoids
        // having an early BLEND layer's color clobbered by a later
        // fading layer's prepass z-write — and prevents that z-write
        // from leaking out and culling other transparent geosets behind
        // this one when no global transparent sort is in place.
        for (int li = 0; li < numLayers; ++li) {
            if (!jobs[li].valid || !jobs[li].isOpaqueFading) continue;
            bls::MatParams prepass = jobs[li].mp;
            prepass.diffuseColor = {1.0f, 1.0f, 1.0f, 1.0f};
            prepass.disables &= ~bls::kDisableDepthWrite;
            prepass.disables |=  bls::kDisableBit8;
            issueHdDraw(jobs[li], prepass);
        }

        // Pass 2 — color sweep. All layers in source (stack) order.
        for (int li = 0; li < numLayers; ++li) {
            if (!jobs[li].valid) continue;
            issueHdDraw(jobs[li], jobs[li].mp);
        }
    }
};

bool RenderService::RenderGeosetsHd() {
    return GeosetPassHd{*this}.Run();
}

void RenderService::RenderGeosets() {
    // HD mode routes through hd.bls / sd_on_hd.bls; SD mode through sd.bls.
    // InitDevice guarantees both programs are loaded -- there is no legacy
    // Slang fallback any more, so Run() on either pass either draws or
    // returns early (empty scene). No visible failure mode here.
    if (renderMode_ == RenderMode::HD) {
        RenderGeosetsHd();
    } else {
        RenderGeosetsBls();
    }
}

void RenderService::SnapCameraToFace(int faceIndex) {
    if (faceIndex < 0 || faceIndex > 5) return;
    // Face order + outward normals in Max space. Index matches CreateViewCube.
    static constexpr Vector3f kFaceNormalsMax[6] = {
        { 0, 1, 0},   // Front
        { 0,-1, 0},   // Back
        {-1, 0, 0},   // Left
        { 1, 0, 0},   // Right
        { 0, 0, 1},   // Top
        { 0, 0,-1},   // Bottom
    };
    Vector3f n = CoordinateSystem::ConvertDirection(
        CoordSpace::Max, CoordinateSystem::Default(), kFaceNormalsMax[faceIndex]);
    // Orbital camera places source at target + distance * (cos(p)*cos(y),
    // cos(p)*sin(y), sin(p)). Clicking a face puts the camera on that face's
    // outward normal, i.e. source-target direction = n.
    constexpr float kTopBottomPitch = 1.55f;  // ~89° — avoids the pole clamp
    if (std::abs(n.z) > 0.99f) {
        // Top/Bottom: yaw is ambiguous at the pole; keep whatever yaw is
        // sensible for the default front view.
        scene_->Camera().SetYaw(Camera::kDefaultYaw);
        scene_->Camera().SetPitch(n.z > 0 ? kTopBottomPitch : -kTopBottomPitch);
    } else {
        scene_->Camera().SetYaw(std::atan2(n.y, n.x));
        scene_->Camera().SetPitch(0.0f);
    }
}

} // namespace WhiteoutDex
