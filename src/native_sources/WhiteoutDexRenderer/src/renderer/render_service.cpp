// ============================================================================
// WhiteoutDex Real-Time Renderer — Render Service Implementation
// ============================================================================

#include "render_service.h"
#include "render_service_internal.h"
#include "render_pass.h"
#include "debug/debug_renderer.h"
#include "constants.h"
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

// PE1 model template — full definition (uses MdxModelAdapter which is now fully included)
struct WhiteoutDex::RenderService::PE1ModelTemplate {
    std::shared_ptr<MdxModelAdapter> adapter;
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
    std::vector<MaterialData> materials;
    SkeletonData skeleton;
    std::vector<SkinWeightData> skinWeights;
    std::vector<ParticleEmitterConfig> pe2Configs;
    std::vector<RibbonEmitterConfig> ribbonConfigs;
    std::vector<CollisionShapeData> collisionConfigs;
    std::vector<PE1EmitterConfig> pe1Configs;
};

namespace WhiteoutDex {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RenderService::RenderService()
    : debug_(std::make_unique<DebugRenderer>(*this)) {
    activeContentProvider_ = &contentProvider_;
    StartTemplateLoader();
}
RenderService::~RenderService() { StopTemplateLoader(); }

// ViewCube input queries forward to the DebugRenderer, which owns the cube
// geometry + hover state. Kept out-of-line so the header doesn't need the
// full DebugRenderer definition.
int  RenderService::HitTestViewCube(int mx, int my)      { return debug_->HitTestViewCube(mx, my); }
Rect RenderService::GetViewCubeRect() const              { return debug_->GetViewCubeRect(); }
void RenderService::SetViewCubeHovered(bool hovered)     { debug_->SetViewCubeHovered(hovered); }

void RenderService::SetCamera(float pitch, float yaw, float distance,
                         float tx, float ty, float tz) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    camera_.SetPitch(pitch);
    camera_.SetYaw(yaw);
    camera_.SetDistance(distance);
    camera_.SetTarget(tx, ty, tz);
}

// ============================================================================
// Model Data Input (called from API/MaxScript thread)
// ============================================================================

void RenderService::ClearModel() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : models_) {
        mi->stagedClear = true;
        mi->stagedDirty = true;
    }
    focusModelHandle_ = 0;
    // Drop everything on the PE2 service side too.
    particleService_.Clear();
}

void RenderService::RemoveModel(uint32_t handle) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = models_.find(handle);
    if (it != models_.end()) {
        it->second->ReleaseGPU(*gfx_);
        models_.erase(it);
    }
    if (focusModelHandle_ == handle) {
        focusModelHandle_ = models_.empty() ? 0 : models_.begin()->first;
    }
    // Drop any PE2-service emitters registered under this model.
    particleService_.RemoveModel(handle);
}

void RenderService::AddPlaneEmitters(uint32_t modelHandle,
                                     const std::vector<particle::PlaneEmitterInit>& inits) {
    for (size_t i = 0; i < inits.size(); ++i) {
        auto emitter = std::make_unique<particle::PlaneEmitter>();
        particle::ApplyInit(*emitter, inits[i]);
        // Match the emitter index to the legacy ParticleSystem emitter id, so
        // per-frame state (keyed on emitterId) targets the same logical emitter
        // through both paths while the legacy system is still live.
        particleService_.AddPlaneEmitter(modelHandle, static_cast<int>(i), std::move(emitter));
    }
}

void RenderService::SetAttachmentConfigs(uint32_t handle, const std::vector<AttachmentConfig>& configs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;
    mi->attachmentSlots.clear();
    // Keep ALL attachments as slots so slot index == FrameState attachment index.
    // Only slots with non-empty modelPath will have child models loaded.
    for (auto& cfg : configs) {
        ModelInstance::AttachmentSlot slot;
        slot.config = cfg;
        slot.loaded = cfg.modelPath.empty(); // mark empty-path slots as "loaded" (nothing to load)
        mi->attachmentSlots.push_back(slot);
    }
}

void RenderService::SetContentProvider(std::shared_ptr<IContentProvider> provider) {
    externalContentProvider_ = std::move(provider);
    activeContentProvider_ = externalContentProvider_
                                 ? externalContentProvider_.get()
                                 : static_cast<IContentProvider*>(&contentProvider_);
}

void RenderService::SetPE1BasePath(const std::string& basePath) {
    pe1BasePath_ = basePath;
    contentProvider_.SetBasePath(basePath);
}

void RenderService::SetPE1Configs(uint32_t handle, const std::vector<PE1EmitterConfig>& configs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;
    for (int i = 0; i < (int)configs.size(); i++)
        mi->pe1.AddEmitter(i, configs[i]);
}

// ============================================================================
// Attachment Model Loading
// ============================================================================

void RenderService::UpdateAttachments() {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Collect handles to avoid modifying models_ during iteration
    std::vector<uint32_t> handles;
    for (auto& [h, mi] : models_) handles.push_back(h);

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
            // worker finishes and DrainTemplateResults populates the cache.
            auto tmpl = getOrLoadTemplate(slot.config.modelPath);
            if (!tmpl) continue;

            slot.loaded = true;

            uint32_t childH = nextModelHandle_++;
            auto child = std::make_unique<ModelInstance>();
            child->handle = childH;
            child->isPE1Child = true;  // reuse the flag for "child model"
            child->pe1Depth = mi->pe1Depth + 1;
            child->pe1Adapter = tmpl->adapter;
            child->pe1BirthTimeMs = animationTimeMs_;
            // Pick a random sequence
            auto seqs = tmpl->adapter->GetSequences();
            if (!seqs.empty())
                child->pe1SequenceIdx = rand() % (int)seqs.size();

            stageModelFromTemplate(child.get(), *tmpl);
            models_[childH] = std::move(child);
            slot.childModelHandle = childH;
        }
    }
}

// ============================================================================
// PE1 Model Template Cache
// ============================================================================

std::shared_ptr<RenderService::PE1ModelTemplate> RenderService::getOrLoadTemplate(const std::string& modelPath) {
    // 1. Cache hit (includes cached failures as nullptr)
    auto it = pe1TemplateCache_.find(modelPath);
    if (it != pe1TemplateCache_.end()) return it->second;

    // 2. Already queued for async load — skip this frame
    {
        std::lock_guard<std::mutex> lock(templateQueueMutex_);
        if (templateLoadPending_.count(modelPath)) return nullptr;

        // 3. Queue for async load
        templateLoadPending_.insert(modelPath);
        templateLoadQueue_.push_back(modelPath);
    }
    templateQueueCV_.notify_one();
    return nullptr;
}

std::shared_ptr<RenderService::PE1ModelTemplate> RenderService::loadTemplateSync(const std::string& modelPath) {
    // Try to read the model file via the active content provider.
    auto fileData = activeContentProvider_->ReadFile(modelPath);
    if (!fileData || fileData->empty())
        return nullptr;

    // Parse MDX from memory buffer
    whiteout::mdx::Parser mdxParser;
    whiteout::mdx::Model model;
    try {
        model = mdxParser.parse(std::span<const whiteout::u8>(fileData->data(), fileData->size()));
    } catch (...) {
        return nullptr;
    }

    // basePath for texture resolution: use pe1BasePath_ (war3 data root)
    // so textures like "Textures\Footprint00.blp" resolve correctly
    namespace fs = std::filesystem;
    fs::path texBasePath = pe1BasePath_.empty() ? fs::path(modelPath).parent_path() : fs::path(pe1BasePath_);

    auto tmpl = std::make_shared<PE1ModelTemplate>();
    auto adapter = std::make_shared<MdxModelAdapter>(
        std::move(model), texBasePath, activeContentProvider_);
    tmpl->adapter = adapter;
    tmpl->meshes = adapter->GetMeshes();
    tmpl->textures = adapter->GetTextures();
    tmpl->materials = adapter->GetMaterials();
    tmpl->skeleton = adapter->GetSkeleton();
    tmpl->skinWeights = adapter->GetSkinWeights();
    tmpl->pe2Configs = adapter->GetParticleConfigs();
    tmpl->ribbonConfigs = adapter->GetRibbonConfigs();
    tmpl->collisionConfigs = adapter->GetCollisionShapes();
    tmpl->pe1Configs = adapter->GetPE1Configs();

    return tmpl;
}

// ============================================================================
// Async PE1 Template Loader
// ============================================================================

void RenderService::StartTemplateLoader() {
    templateLoaderRunning_ = true;
    templateLoaderThread_ = std::thread(&RenderService::TemplateLoaderFunc, this);
}

void RenderService::StopTemplateLoader() {
    templateLoaderRunning_ = false;
    templateQueueCV_.notify_one();
    if (templateLoaderThread_.joinable())
        templateLoaderThread_.join();
}

void RenderService::TemplateLoaderFunc() {
    while (templateLoaderRunning_) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(templateQueueMutex_);
            templateQueueCV_.wait_for(lock, std::chrono::milliseconds(50),
                [this] { return !templateLoadQueue_.empty() || !templateLoaderRunning_; });
            if (!templateLoaderRunning_) break;
            if (templateLoadQueue_.empty()) continue;
            path = std::move(templateLoadQueue_.front());
            templateLoadQueue_.pop_front();
        }

        auto tmpl = loadTemplateSync(path);

        {
            std::lock_guard<std::mutex> lock(templateResultMutex_);
            templateLoadResults_.emplace_back(std::move(path), std::move(tmpl));
        }
    }
}

void RenderService::DrainTemplateResults() {
    std::vector<std::pair<std::string, std::shared_ptr<PE1ModelTemplate>>> results;
    {
        std::lock_guard<std::mutex> lock(templateResultMutex_);
        results.swap(templateLoadResults_);
    }

    if (results.empty()) return;

    for (auto& [path, tmpl] : results) {
        pe1TemplateCache_[path] = tmpl;
        {
            std::lock_guard<std::mutex> lock(templateQueueMutex_);
            templateLoadPending_.erase(path);
        }
    }
}

void RenderService::stageModelFromTemplate(ModelInstance* mi, const PE1ModelTemplate& tmpl) {
    // Stage textures
    for (auto& tex : tmpl.textures) {
        StagedTexture& st = mi->stagedTextures[tex.textureId];
        st.width = tex.width; st.height = tex.height;
        st.mipLevels = tex.mipLevels;
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.format = tex.format;
        st.pixels = tex.pixels;
        if (tex.replaceableId == 1 || tex.replaceableId == 2)
            mi->replaceableTexMap[tex.textureId] = tex.replaceableId;
    }
    // Stage materials
    for (auto& mat : tmpl.materials) {
        StagedMaterial& sm = mi->stagedMaterials[mat.materialId];
        sm.layers.resize(mat.layers.size());
        for (size_t i = 0; i < mat.layers.size(); i++) {
            sm.layers[i].filterMode = mat.layers[i].filterMode;
            sm.layers[i].textureId  = mat.layers[i].textureId;
            sm.layers[i].alpha      = mat.layers[i].alpha;
            sm.layers[i].flags      = mat.layers[i].flags;
            sm.layers[i].textureAnimationId = mat.layers[i].textureAnimationId;
            sm.layers[i].shaderId           = mat.layers[i].shaderId;
            sm.layers[i].normalMapId        = mat.layers[i].normalMapId;
            sm.layers[i].ormMapId           = mat.layers[i].ormMapId;
            sm.layers[i].emissiveMapId      = mat.layers[i].emissiveMapId;
            sm.layers[i].teamColorMapId     = mat.layers[i].teamColorMapId;
            sm.layers[i].emissiveGain       = mat.layers[i].emissiveGain;
            sm.layers[i].fresnelOpacity     = mat.layers[i].fresnelOpacity;
            sm.layers[i].fresnelTeamColor   = mat.layers[i].fresnelTeamColor;
            sm.layers[i].fresnelColor       = mat.layers[i].fresnelColor;
        }
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder = mat.sortOrder;
    }
    // Stage meshes
    for (auto& mesh : tmpl.meshes) {
        StagedGeoset& sg = mi->stagedGeosets[mesh.geosetId];
        sg.materialId = mesh.materialId;
        sg.lod        = mesh.lod;
        int vc = (int)mesh.positions.size();
        sg.vertices.resize(vc);
        for (int i = 0; i < vc; i++) {
            sg.vertices[i].position = mesh.positions[i];
            sg.vertices[i].normal = (i < (int)mesh.normals.size()) ? mesh.normals[i] : Vector3f{0,0,1};
            sg.vertices[i].uv = (i < (int)mesh.uvs.size()) ? mesh.uvs[i] : Vector2f{0,0};
            sg.vertices[i].color = {1,1,1,1};
        }
        if ((int)mesh.tangents.size() == vc) sg.tangents = mesh.tangents;
        sg.indices = mesh.indices;
    }
    // Skeleton
    if (tmpl.skeleton.nodeCount > 0) {
        std::vector<float> invBind(tmpl.skeleton.nodeCount * 16);
        for (int i = 0; i < tmpl.skeleton.nodeCount; i++) {
            memcpy(&invBind[i * 16], &tmpl.skeleton.inverseBindMatrices[i].data[0][0], 64);
        }
        mi->skinning.SetSkeleton(tmpl.skeleton.nodeCount, invBind.data());
        mi->billboardFlags = tmpl.skeleton.billboardFlags;
        mi->nodePivots         = tmpl.skeleton.nodePivots;
        mi->nodeParents        = tmpl.skeleton.nodeParents;
        mi->skinDirty = true;
    }
    // Skin weights
    for (auto& sw : tmpl.skinWeights) {
        int vc = (int)sw.influences.size();
        std::vector<int> bIdx(vc * 4); std::vector<float> wts(vc * 4);
        for (int v = 0; v < vc; v++)
            for (int j = 0; j < 4; j++) {
                bIdx[v*4+j] = sw.influences[v].boneIdx[j];
                wts[v*4+j] = sw.influences[v].weight[j];
            }
        mi->skinning.SetGeosetWeights(sw.geosetId, vc, bIdx.data(), wts.data());
    }
    // PE2 particles — registered directly with the service (no legacy path).
    for (int i = 0; i < (int)tmpl.pe2Configs.size(); i++) {
        auto em = std::make_unique<particle::PlaneEmitter>();
        particle::ApplyInit(*em, particle::InitFromLegacyConfig(tmpl.pe2Configs[i]));
        particleService_.AddPlaneEmitter(mi->handle, i, std::move(em));
    }
    // Ribbons
    for (int i = 0; i < (int)tmpl.ribbonConfigs.size(); i++)
        mi->ribbons.AddEmitter(i, tmpl.ribbonConfigs[i]);
    // PE1 (recursive, only if depth allows)
    for (int i = 0; i < (int)tmpl.pe1Configs.size(); i++)
        mi->pe1.AddEmitter(i, tmpl.pe1Configs[i]);

    mi->stagedDirty = true;
}

// ============================================================================
// PE1 Model Particle Lifecycle
// ============================================================================

void RenderService::UpdatePE1(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<uint32_t> toRemove;

    // Collect handles to iterate (avoid modifying models_ during iteration)
    std::vector<uint32_t> handles;
    for (auto& [h, mi] : models_) handles.push_back(h);

    for (uint32_t h : handles) {
        auto* mi = getModel(h);
        if (!mi) continue;
        if (mi->pe1Depth >= kMaxPE1Depth) continue;
        if (!mi->pe1.HasEmitters()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent — skip sub-emitter sim

        auto result = mi->pe1.Simulate(dt, nextModelHandle_);

        // Birth: create child ModelInstance from template
        for (auto& birth : result.born) {
            if (pe1InstanceCount_ >= kMaxPE1Instances) continue;
            auto* cfg = mi->pe1.GetConfig(birth.emitterId);
            if (!cfg) continue;
            auto tmpl = getOrLoadTemplate(cfg->modelPath);
            if (!tmpl) continue;

            auto child = std::make_unique<ModelInstance>();
            child->handle = birth.handle;
            child->worldTransform = birth.worldTransform;
            child->isPE1Child = true;
            child->pe1Depth = mi->pe1Depth + 1;
            child->pe1Adapter = tmpl->adapter;
            child->pe1BirthTimeMs = animationTimeMs_;

            stageModelFromTemplate(child.get(), *tmpl);
            models_[birth.handle] = std::move(child);
            pe1InstanceCount_++;
        }

        // Death
        for (uint32_t childH : result.died) toRemove.push_back(childH);

        // Transform updates
        for (auto& [childH, tm] : result.transforms) {
            if (auto* c = getModel(childH)) c->worldTransform = tm;
        }
    }

    for (uint32_t rh : toRemove) {
        auto it = models_.find(rh);
        if (it != models_.end()) {
            it->second->ReleaseGPU(*gfx_);
            models_.erase(it);
            pe1InstanceCount_--;
        }
        // Drop any emitters this child registered with the PE2 service.
        // Otherwise they persist as ghost emitters, drawing with the default
        // texture (since getModel(dl.model) returns null on lookup).
        particleService_.RemoveModel(rh);
    }
}

void RenderService::EvaluatePE1Children() {
    struct ChildEval {
        uint32_t handle;
        std::shared_ptr<IModelSource> adapter;
        int localTimeMs;
        int seqIdx;
        int globalTimeMs;  // unclamped elapsed since birth (for global sequences)
    };
    std::vector<ChildEval> toEval;
    Vector3f camPos;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        camPos = camera_.GetSource();
        int timeMs = animationTimeMs_.load();
        for (auto& [h, mi] : models_) {
            if (!mi->isPE1Child || !mi->pe1Adapter) continue;
            if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent — skip eval
            int localTime = timeMs - mi->pe1BirthTimeMs;
            if (localTime < 0) localTime = 0;
            int globalTime = localTime;  // unclamped wall-clock elapsed since birth
            auto seqs = mi->pe1Adapter->GetSequences();
            if (!seqs.empty()) {
                int seqIdx = mi->pe1SequenceIdx % (int)seqs.size();
                int dur = seqs[seqIdx].endMs - seqs[seqIdx].startMs;
                if (dur > 0) localTime = seqs[seqIdx].startMs + (localTime % dur);
            }
            toEval.push_back({h, mi->pe1Adapter, localTime, mi->pe1SequenceIdx, globalTime});
        }
    }

    for (auto& ce : toEval) {
        ce.adapter->SetCameraPosition(camPos.x, camPos.y, camPos.z);
        ce.adapter->SetActiveSequence(ce.seqIdx);
        FrameState fs = ce.adapter->Evaluate(ce.localTimeMs, ce.globalTimeMs);
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
        StagedTexture& st = mi->stagedTextures[tex.textureId];
        st.width  = tex.width;
        st.height = tex.height;
        st.mipLevels = tex.mipLevels;
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.format = tex.format;
        st.pixels = tex.pixels;
        if (tex.replaceableId == 1 || tex.replaceableId == 2)
            mi->replaceableTexMap[tex.textureId] = tex.replaceableId;
    }

    for (auto& mat : materials) {
        StagedMaterial& sm = mi->stagedMaterials[mat.materialId];
        sm.layers.resize(mat.layers.size());
        for (size_t i = 0; i < mat.layers.size(); i++) {
            sm.layers[i].filterMode = mat.layers[i].filterMode;
            sm.layers[i].textureId  = mat.layers[i].textureId;
            sm.layers[i].alpha      = mat.layers[i].alpha;
            sm.layers[i].flags      = mat.layers[i].flags;
            sm.layers[i].textureAnimationId = mat.layers[i].textureAnimationId;
            sm.layers[i].shaderId           = mat.layers[i].shaderId;
            sm.layers[i].normalMapId        = mat.layers[i].normalMapId;
            sm.layers[i].ormMapId           = mat.layers[i].ormMapId;
            sm.layers[i].emissiveMapId      = mat.layers[i].emissiveMapId;
            sm.layers[i].teamColorMapId     = mat.layers[i].teamColorMapId;
            sm.layers[i].emissiveGain       = mat.layers[i].emissiveGain;
            sm.layers[i].fresnelOpacity     = mat.layers[i].fresnelOpacity;
            sm.layers[i].fresnelTeamColor   = mat.layers[i].fresnelTeamColor;
            sm.layers[i].fresnelColor       = mat.layers[i].fresnelColor;
        }
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder     = mat.sortOrder;
    }

    mi->stagedDirty = true;
}

void RenderService::UpdateMaterials(const std::vector<MaterialData>& materials,
                               const std::vector<TextureData>& textures) {
    UpdateMaterials(focusModelHandle_, materials, textures);
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
    uint32_t handle = nextModelHandle_++;
    auto mi = std::make_unique<ModelInstance>();
    mi->handle = handle;

    // Textures → staged
    for (auto& tex : textures) {
        StagedTexture& st = mi->stagedTextures[tex.textureId];
        st.width  = tex.width;
        st.height = tex.height;
        st.mipLevels = tex.mipLevels;
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.format = tex.format;
        st.pixels = tex.pixels;
        // Track replaceable textures for team color updates
        if (tex.replaceableId == 1 || tex.replaceableId == 2)
            mi->replaceableTexMap[tex.textureId] = tex.replaceableId;
    }

    // Materials → staged
    for (auto& mat : materials) {
        StagedMaterial& sm = mi->stagedMaterials[mat.materialId];
        sm.layers.resize(mat.layers.size());
        for (size_t i = 0; i < mat.layers.size(); i++) {
            sm.layers[i].filterMode = mat.layers[i].filterMode;
            sm.layers[i].textureId  = mat.layers[i].textureId;
            sm.layers[i].alpha      = mat.layers[i].alpha;
            sm.layers[i].flags      = mat.layers[i].flags;
            sm.layers[i].textureAnimationId = mat.layers[i].textureAnimationId;
            sm.layers[i].shaderId           = mat.layers[i].shaderId;
            sm.layers[i].normalMapId        = mat.layers[i].normalMapId;
            sm.layers[i].ormMapId           = mat.layers[i].ormMapId;
            sm.layers[i].emissiveMapId      = mat.layers[i].emissiveMapId;
            sm.layers[i].teamColorMapId     = mat.layers[i].teamColorMapId;
            sm.layers[i].emissiveGain       = mat.layers[i].emissiveGain;
            sm.layers[i].fresnelOpacity     = mat.layers[i].fresnelOpacity;
            sm.layers[i].fresnelTeamColor   = mat.layers[i].fresnelTeamColor;
            sm.layers[i].fresnelColor       = mat.layers[i].fresnelColor;
        }
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder     = mat.sortOrder;
    }

    // Meshes → staged
    for (auto& mesh : meshes) {
        StagedGeoset& sg = mi->stagedGeosets[mesh.geosetId];
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
        mi->skinning.SetSkeleton(skeleton.nodeCount, invBindFlat.data());
        mi->billboardFlags = skeleton.billboardFlags;
        mi->nodePivots         = skeleton.nodePivots;
        mi->nodeParents        = skeleton.nodeParents;
        mi->skinDirty = true;
    }

    // Skin weights
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
        mi->skinning.SetGeosetWeights(sw.geosetId, vc, boneIdx.data(), weights.data());
    }
    if (!skinWeights.empty()) mi->skinDirty = true;

    // Particles — register with the PE2 service. The legacy ParticleEmitterConfig
    // boundary type stays (external API), but it's translated to the service's
    // PlaneEmitterInit on the way in. All downstream simulation/render is service.
    for (size_t i = 0; i < particleConfigs.size(); i++) {
        auto em = std::make_unique<particle::PlaneEmitter>();
        particle::ApplyInit(*em, particle::InitFromLegacyConfig(particleConfigs[i]));
        particleService_.AddPlaneEmitter(handle, (int)i, std::move(em));
    }

    // Ribbons
    for (size_t i = 0; i < ribbonConfigs.size(); i++) {
        mi->ribbons.AddEmitter((int)i, ribbonConfigs[i]);
    }

    // Collision shapes
    for (auto& cs : collisions) {
        CollisionShape shape;
        shape.type   = cs.type;
        shape.vmin   = cs.vertices[0];
        shape.vmax   = cs.vertices[1];
        shape.radius = cs.radius;
        shape.pivot  = cs.pivot;
        mi->collisionShapes.push_back(shape);
    }

    mi->stagedDirty = true;
    if (focusModelHandle_ == 0) focusModelHandle_ = handle;
    models_[handle] = std::move(mi);
    return handle;
}

// Backward-compatible single-model API
void RenderService::LoadModel(const std::vector<MeshData>& meshes,
                         const std::vector<TextureData>& textures,
                         const std::vector<MaterialData>& materials,
                         const SkeletonData& skeleton,
                         const std::vector<SkinWeightData>& skinWeights,
                         const std::vector<ParticleEmitterConfig>& particleConfigs,
                         const std::vector<RibbonEmitterConfig>& ribbonConfigs,
                         const std::vector<CollisionShapeData>& collisions) {
    ClearModel();
    uint32_t h = AddModel(meshes, textures, materials, skeleton,
                          skinWeights, particleConfigs, ribbonConfigs, collisions);
    focusModelHandle_ = h;

    // Auto-activate HD when any material layer ships a non-SD shader
    // (Layer::ShaderType: 0=SD, 1=HD, 2=SDOnHD, 24=Crystal). Flip the
    // pending flag so the UI thread re-syncs the HD checkbox to match.
    bool anyNonSd = false;
    for (auto& mat : materials) {
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

// ============================================================================
// ApplyFrameState helpers
// ============================================================================

void RenderService::ApplyBoneMatrices(ModelInstance& mi, const FrameState& state) {
    if (state.boneWorldMatrices.empty()) return;

    int bc = (int)state.boneWorldMatrices.size();
    Vector3f camPos = camera_.GetSource();

    std::vector<float> worldFlat(bc * 16);
    for (int i = 0; i < bc; i++) {
        Matrix44f boneM = state.boneWorldMatrices[i];

        uint32_t bbFlags = (i < (int)mi.billboardFlags.size()) ? mi.billboardFlags[i] : 0;
        if (bbFlags != 0) {
            // Match Previewd TransformObjectView @0x140534ee0. Two independent
            // pieces: CameraAnchored (0x80 in file) moves the pivot along the
            // parent→camera ray, then the Full/LockX/LockY/LockZ switch writes
            // a replacement rotation basis around that (modified) pivot.

            Vector3f pivF = (i < (int)mi.nodePivots.size())
                              ? mi.nodePivots[i] : Vector3f{0, 0, 0};

            // Target world-pivot starts as the authored one from boneM. We may
            // overwrite it below for CameraAnchored.
            Vector3f pivWorld = whiteout::transform_point(pivF, boneM);

            if (bbFlags & BONE_BILLBOARD_CAMERA_ANCHORED) {
                // PositionAnchor @0x140534950: place the node on the line
                // from parent-world to camera, at the node's rest distance
                // from parent. Rotation/scale of the stack are then reset
                // — we achieve the same by rebuilding boneM below.
                int parentIdx = (i < (int)mi.nodeParents.size()) ? mi.nodeParents[i] : -1;
                Vector3f parentWorld = {0, 0, 0};
                if (parentIdx >= 0 && parentIdx < (int)state.boneWorldMatrices.size()) {
                    Vector3f parentPivF = (parentIdx < (int)mi.nodePivots.size())
                                            ? mi.nodePivots[parentIdx] : Vector3f{0, 0, 0};
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
                    Matrix44f T_negRest = Matrix44f::translation({-pivF.x, -pivF.y, -pivF.z});
                    Matrix44f T_world   = Matrix44f::translation({pivWorld.x, pivWorld.y, pivWorld.z});
                    boneM = T_negRest * bbRot * T_world;
                } else if (bbFlags & BONE_BILLBOARD_CAMERA_ANCHORED) {
                    // CameraAnchored without a billboard rotation flag: still
                    // need to re-center boneM so the pivot lands on pivWorld.
                    // Previewd's PositionAnchor also strips rotation/scale;
                    // replicate by rebuilding around identity rotation.
                    Matrix44f T_negRest = Matrix44f::translation({-pivF.x, -pivF.y, -pivF.z});
                    Matrix44f T_world   = Matrix44f::translation({pivWorld.x, pivWorld.y, pivWorld.z});
                    boneM = T_negRest * T_world;
                }
            }
        }

        memcpy(&worldFlat[i * 16], &boneM.data[0][0], 64);
    }
    mi.skinning.UpdateNodeMatrices(bc, worldFlat.data());
}

void RenderService::ApplyGeosetStates(ModelInstance& mi, const FrameState& state) {
    for (int i = 0; i < (int)state.geosetTransforms.size() && i < (int)mi.gpuGeosets.size(); i++)
        mi.gpuGeosets[i].worldMatrix = state.geosetTransforms[i];

    for (int i = 0; i < (int)state.geosetAlphas.size() && i < (int)mi.gpuGeosets.size(); i++)
        mi.gpuGeosets[i].geosetAlpha = state.geosetAlphas[i];

    for (int i = 0; i < (int)state.geosetColors.size() && i < (int)mi.gpuGeosets.size(); i++)
        mi.gpuGeosets[i].geosetColor = state.geosetColors[i];
}

void RenderService::ApplyLayerStates(ModelInstance& mi, const FrameState& state) {
    // Texture animations (per-layer) — clear stale entries from previous frame
    mi.matTexAnim.clear();
    for (auto& ta : state.texAnims) {
        int key = ta.materialId * 1000 + ta.layerIndex;
        mi.matTexAnim[key] = {ta.uOff, ta.vOff, ta.uTile, ta.vTile, ta.rotation};
    }

    // BLS-path palette: dense by textureAnimationId. Size it to the max id
    // we see this frame and fill unused slots with identity.
    int maxTexAnimId = -1;
    for (auto& tam : state.texAnimMatrices) {
        if (tam.textureAnimId > maxTexAnimId) maxTexAnimId = tam.textureAnimId;
    }
    mi.texAnimPalette.assign(std::max(0, maxTexAnimId + 1),
                             ModelInstance::TexAnimPaletteEntry{
                                 {1.0f, 0.0f, 0.0f, 0.0f},
                                 {0.0f, 1.0f, 0.0f, 0.0f}});
    for (auto& tam : state.texAnimMatrices) {
        if (tam.textureAnimId < 0 || tam.textureAnimId > maxTexAnimId) continue;
        auto& e = mi.texAnimPalette[tam.textureAnimId];
        for (int k = 0; k < 4; ++k) { e.row0[k] = tam.row0[k]; e.row1[k] = tam.row1[k]; }
    }

    // Per-layer alpha animation (KMTA tracks)
    for (auto& la : state.layerAlphas) {
        if (la.materialId >= 0 && la.materialId < (int)mi.gpuMaterials.size()) {
            auto& layers = mi.gpuMaterials[la.materialId].cpu.layers;
            if (la.layerIndex >= 0 && la.layerIndex < (int)layers.size())
                layers[la.layerIndex].alpha = la.alpha;
        }
    }

    // Per-layer texture ID animation (KMTF tracks)
    for (auto& lt : state.layerTextureIds) {
        if (lt.materialId >= 0 && lt.materialId < (int)mi.gpuMaterials.size()) {
            auto& layers = mi.gpuMaterials[lt.materialId].cpu.layers;
            if (lt.layerIndex >= 0 && lt.layerIndex < (int)layers.size())
                layers[lt.layerIndex].textureId = lt.textureId;
        }
    }

    // Per-layer fresnel / emissive animation — matches Previewd's
    // RenderGeosetLayers (0x14030a210), which stamps these four values
    // into layerMaterial.m_pixelParams every draw from the per-layer
    // evaluated track arrays (modelptr->m_fresnelColor etc.).
    for (auto& lf : state.layerFresnels) {
        if (lf.materialId >= 0 && lf.materialId < (int)mi.gpuMaterials.size()) {
            auto& layers = mi.gpuMaterials[lf.materialId].cpu.layers;
            if (lf.layerIndex >= 0 && lf.layerIndex < (int)layers.size()) {
                auto& L = layers[lf.layerIndex];
                L.fresnelColor     = lf.fresnelColor;
                L.fresnelOpacity   = lf.fresnelOpacity;
                L.fresnelTeamColor = lf.fresnelTeamColor;
                L.emissiveGain     = lf.emissiveGain;
            }
        }
    }

    // Cache evaluated MDX scene lights. We keep BOTH enabled and disabled
    // entries so the debug "Lights" overlay can show every authored light
    // at its world position (disabled ones drawn dim). Consumers that want
    // only the enabled set must check `L.enabled` themselves.
    mi.activeLights = state.lights;
}

void RenderService::ApplyParticleFrameStates(ModelInstance& mi, const FrameState& state) {
    // PE2 service is the single path. Per-frame: look up the registered
    // PlaneEmitter and push the evaluated animation state into it. Transform
    // is conjugated for Blizzard-space sim (not the default — see §3.8
    // retrospective in docs/PARTICLEEMITTERS2.md).
    for (const auto& ps : state.particleStates) {
        auto* em = particleService_.GetEmitter(mi.handle, ps.emitterId);
        if (!em) continue;

        em->SetEmissionRate(ps.emissionRate);
        em->SetVelocity(ps.speed);
        em->SetVelocityVariation(ps.variation);
        em->SetLatitude(ps.coneAngle);
        em->SetAcceleration(ps.gravity);
        em->SetWidth(ps.width);
        em->SetHeight(ps.length);
        em->SetVisible(ps.visibility > 0.02f);
        // ps.transform arrives in the renderer-native default space. If the
        // emitter simulates in a different space, conjugate into it.
        em->SetModelToWorld(CoordinateSystem::ConvertTransform(
            CoordinateSystem::Default(), em->GetCoordSpace(), ps.transform));
    }
}

void RenderService::ApplyRibbonFrameStates(ModelInstance& mi, const FrameState& state) {
    for (auto& rs : state.ribbonStates) {
        RibbonEmitterState st;
        st.transform   = rs.transform;
        st.above       = rs.above;
        st.below       = rs.below;
        st.alpha       = rs.alpha;
        st.color       = rs.color;
        st.visibility  = rs.visibility;
        st.slot        = rs.slot;
        mi.ribbons.UpdateEmitterState(rs.emitterId, st);
    }
}

void RenderService::ApplyPE1FrameStates(ModelInstance& mi, const FrameState& state) {
    for (auto& ps : state.pe1States) {
        PE1EmitterState st;
        st.transform    = ps.transform;
        st.emissionRate = ps.emissionRate;
        st.speed        = ps.speed;
        st.latitude     = ps.latitude;
        st.longitude    = ps.longitude;
        st.gravity      = ps.gravity;
        st.visibility   = ps.visibility;
        mi.pe1.UpdateEmitterState(ps.emitterId, st);
    }
}

void RenderService::ApplyAttachmentStates(ModelInstance& mi, const FrameState& state, int timeMs) {
    for (auto& as : state.attachmentStates) {
        if (as.attachmentIndex < 0 || as.attachmentIndex >= (int)mi.attachmentSlots.size()) continue;
        auto& slot = mi.attachmentSlots[as.attachmentIndex];
        if (slot.childModelHandle == 0) continue;
        auto* child = getModel(slot.childModelHandle);
        if (!child) continue;

        bool visible = (as.visibility > 0.02f);
        child->worldTransform = as.transform;

        // When becoming visible: pick a new random animation and restart from frame 0
        if (visible && !slot.wasVisible) {
            child->pe1BirthTimeMs = timeMs;
            if (child->pe1Adapter) {
                auto seqs = child->pe1Adapter->GetSequences();
                if (!seqs.empty())
                    child->pe1SequenceIdx = rand() % (int)seqs.size();
            }
            slot.wasVisible = true;
        } else if (!visible) {
            slot.wasVisible = false;
        }

        // Authoritative parent-driven visibility. The child's own animation
        // still writes geoset/layer alphas via EvaluatePE1Children; the
        // renderer multiplies them by parentVisibility at draw time.
        child->parentVisibility = visible ? as.visibility : 0.0f;
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
    ApplyGeosetStates(*mi, state);
    ApplyLayerStates(*mi, state);
    ApplyParticleFrameStates(*mi, state);
    ApplyRibbonFrameStates(*mi, state);
    ApplyPE1FrameStates(*mi, state);

    // Collision transforms (simple 1:1 copy)
    for (int i = 0; i < (int)state.collisionTransforms.size() && i < (int)mi->collisionShapes.size(); i++)
        mi->collisionShapes[i].transform = state.collisionTransforms[i];

    ApplyAttachmentStates(*mi, state, timeMs);

    if (!mi->isPE1Child)
        animationTimeMs_ = timeMs;
}

void RenderService::ApplyFrameState(const FrameState& state, int timeMs) {
    ApplyFrameState(focusModelHandle_, state, timeMs);
}

// ============================================================================
// Team Color Texture Update
// ============================================================================

void RenderService::UpdateTeamColorTextures() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    // teamColor_ is BGR-packed (0x00BBGGRR)
    uint8_t r = (uint8_t)(teamColor_ & 0xFF);
    uint8_t g = (uint8_t)((teamColor_ >> 8) & 0xFF);
    uint8_t b = (uint8_t)((teamColor_ >> 16) & 0xFF);
    for (auto& [h, mi] : models_) {
        for (auto& [texId, replId] : mi->replaceableTexMap) {
            StagedTexture& st = mi->stagedTextures[texId];
            st.replaceableId = replId;
            // Force RGBA8 — we're writing uncompressed 32bpp pixels, but
            // the stagedTextures entry may have been created by a prior
            // pass that loaded a BC3/BC5 image into this same slot. Not
            // resetting the format would have the upload path treat 64
            // RGBA8 bytes as BCn blocks and corrupt the texture.
            st.format = gfx::Format::R8G8B8A8_UNORM;
            // Generated TeamColor / TeamGlow are single-mip. Reset
            // mipLevels in case a prior pass loaded a mipped image
            // into this same slot (stale value would cause the GPU
            // upload to walk off the end of `pixels`).
            st.mipLevels = 1;
            if (replId == 2) {
                st.pixels = DecodeTeamGlow(r, g, b, st.width, st.height);
            } else {
                st.width = 4; st.height = 4;
                st.pixels.resize(64);
                for (int j = 0; j < 16; j++) {
                    st.pixels[j * 4 + 0] = r;
                    st.pixels[j * 4 + 1] = g;
                    st.pixels[j * 4 + 2] = b;
                    st.pixels[j * 4 + 3] = 255;
                }
            }
        }
        if (!mi->replaceableTexMap.empty()) mi->stagedDirty = true;
    }
}

// ============================================================================
// Camera Presets
// ============================================================================

void RenderService::SetTeamColor(uint8_t r, uint8_t g, uint8_t b) {
    // BGR packing matches the Windows RGB() macro layout: 0x00BBGGRR.
    teamColor_ = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    UpdateTeamColorTextures();
    teamColorDirty_.store(true);
}

void RenderService::UpdateTeamColorSwatch() {
    // Pack current picker colour as RGBA8 (A = 0xFF). `teamColor_` is
    // BGR-packed so recombine R/G/B explicitly rather than memcpy'ing.
    const uint8_t r = (uint8_t)(teamColor_ & 0xFF);
    const uint8_t g = (uint8_t)((teamColor_ >> 8) & 0xFF);
    const uint8_t b = (uint8_t)((teamColor_ >> 16) & 0xFF);
    const uint32_t rgba =
        (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
    if (teamColorTex_ != gfx::TextureHandle::Invalid && teamColorTexColor_ == rgba)
        return;
    if (teamColorTex_ != gfx::TextureHandle::Invalid) {
        gfx_->Destroy(teamColorTex_);
        teamColorTex_ = gfx::TextureHandle::Invalid;
    }
    uint32_t px = rgba;
    teamColorTex_ = gfx_->CreateTexture({
        .width  = 1,
        .height = 1,
        .format = gfx::Format::R8G8B8A8_UNORM,
        .usage  = gfx::TextureUsage::ShaderResource,
    }, &px);
    teamColorTexColor_ = rgba;
}

void RenderService::SetCameraPresets(const std::vector<CameraPreset>& presets) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    // Service keeps its own copy for ActivateCameraPreset; UI drains
    // the pending queue separately.
    cameraPresets_          = presets;
    pendingCameraPresets_   = presets;
    cameraDirty_            = true;
    activeCameraPresetIdx_  = -1;
}

void RenderService::ActivateCameraPreset(int idx) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (idx < 0 || idx >= (int)cameraPresets_.size()) {
        camera_.SetOrbitalMode();
        camera_.SetFovDiagonal(Camera::kDefaultFovDiagonal);
        camera_.SetClip(Camera::kDefaultNearZ, Camera::kDefaultFarZ);
        activeCameraPresetIdx_ = -1;
        return;
    }
    const auto& p = cameraPresets_[idx];
    Vector3f pos  = p.position;
    Vector3f tgt  = p.target;
    float    roll = p.staticRoll;

    // Invoke the animator immediately so the first frame shows the
    // animated pose, not the pivot-only static default (which is
    // often (0,0,0) for portrait cameras).
    if (p.animator) {
        int seqStart = 0, seqEnd = 0;
        int seqIdx = activeSequence_.load();
        if (seqIdx >= 0 && seqIdx < (int)sequenceRanges_.size()) {
            seqStart = sequenceRanges_[seqIdx].startMs;
            seqEnd   = sequenceRanges_[seqIdx].endMs;
        }
        // Fall back to an open range when no SequenceRanges are
        // known, so FindBracket doesn't empty out and return static.
        if (seqStart == 0 && seqEnd == 0) seqEnd = 1 << 30;
        p.animator(pos, tgt, roll, animationTimeMs_.load(), seqStart, seqEnd);
    }

    camera_.SetDirectPose(pos, tgt, roll);

    // Apply fov/near/far verbatim per Previewd's MdlReadCameras +
    // SetupWorldProjection. fieldOfView=0 is "unset" — substitute a
    // default so the preview doesn't render blank.
    const float fov = (p.fovDiagonal > 1e-3f) ? p.fovDiagonal : Camera::kDefaultFovDiagonal;
    camera_.SetFovDiagonal(fov);
    camera_.SetClip(p.zNear, p.zFar);
    activeCameraPresetIdx_ = idx;
}

std::optional<std::vector<CameraPreset>> RenderService::TakePendingCameraPresets() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!cameraDirty_) return std::nullopt;
    cameraDirty_ = false;
    return std::move(pendingCameraPresets_);
}

void RenderService::SetSequences(const std::vector<std::string>& names) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    pendingSequenceNames_ = names;
    sequencesDirty_ = true;
}

void RenderService::SetSequenceRanges(
    const std::vector<IModelSource::SequenceInfo>& ranges) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    sequenceRanges_ = ranges;
}

int RenderService::GetActiveSequenceIndex() const {
    return activeSequence_.load();
}

std::optional<std::vector<std::string>> RenderService::TakePendingSequences() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!sequencesDirty_) return std::nullopt;
    sequencesDirty_ = false;
    auto result = std::move(pendingSequenceNames_);
    pendingSequenceNames_.clear();
    return result;
}

// ============================================================================
// Camera Manipulation (thread-safe wrappers)
// ============================================================================

void RenderService::RotateCamera(int dx, int dy) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    camera_.Rotate(dx, dy);
}

void RenderService::PanCamera(int dx, int dy) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    camera_.Pan(dx, dy);
}

void RenderService::ZoomCamera(int delta) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    camera_.Zoom(delta);
}

void RenderService::ZoomCameraSmooth(int dy) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    camera_.ZoomSmooth((float)dy * camera_.GetDistance() / Camera::kFactorRelDist);
}

void RenderService::ResetCamera() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    camera_.Reset();
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
    DrainTemplateResults();
    ProcessStagedData();
    UpdateAttachments();
    EvaluatePE1Children();
    UpdateAnimation();
    UpdateParticles(dt);
    UpdatePE1(dt);
    UpdateRibbons(dt);
}

void RenderService::SetAnimationTime(int ms) { animationTimeMs_ = ms; }
int  RenderService::GetAnimationTime() const { return animationTimeMs_.load(); }

void RenderService::ShutdownDevice() {
    ReleaseModelGPU();
    CleanupD3D();
}

void RenderService::GetFrameStats(int& geosets, int& textures, int& nodes,
                              int& particles, int& segments) const {
    geosets = textures = nodes = particles = segments = 0;
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : models_) {
        geosets  += (int)mi->gpuGeosets.size();
        textures += (int)mi->gpuTextures.size();
        nodes    += mi->skinning.NodeCount();
        segments += mi->ribbons.GetTotalSegmentCount();
    }
    particles += particleService_.TotalParticleCount();
}

// ============================================================================
// Staged → GPU Resource Upload (render thread only)
// ============================================================================

void RenderService::UploadStagedTextures(ModelInstance& mi) {
    for (auto& [id, st] : mi.stagedTextures) {
        if (st.width <= 0 || st.height <= 0) continue;

        if (mi.gpuTextures.count(id)) mi.gpuTextures[id].Release(*gfx_);

        GPUTexture gt;
        // Upload in the source pixel format (BC3/BC5/BC7 for normal
        // maps, sRGB variants for albedo, etc). The staged texture
        // carries the format that the DDS/BLP parser reported; bind
        // it verbatim so hd_ps.slang sees Blizzard's packed channels.
        gfx::Format texFormat = st.format == gfx::Format::Unknown
                                   ? gfx::Format::R8G8B8A8_UNORM
                                   : st.format;
        gt.tex = gfx_->CreateTexture({
            .width     = st.width,
            .height    = st.height,
            .mipLevels = (std::max)(1, st.mipLevels),
            .format    = texFormat,
            .usage     = gfx::TextureUsage::ShaderResource,
        }, st.pixels.data());
        gt.wrapFlags = st.wrapFlags;
        mi.gpuTextures[id] = gt;
    }
    mi.stagedTextures.clear();
}

void RenderService::UploadStagedGeosets(ModelInstance& mi) {
    for (auto& [id, sg] : mi.stagedGeosets) {
        GPUGeoset gg;
        gg.geosetId    = id;
        gg.materialId  = sg.materialId;
        gg.lod         = sg.lod;
        gg.indexCount   = (int)sg.indices.size();
        gg.vertexCount  = (int)sg.vertices.size();
        gg.baseVertices = sg.vertices;  // keep CPU copy for particle/ribbon reads
        gg.hasSkinning  = true; // all MDX geosets are skinned (v1200 weights or v800 vertex groups)

        // Copy priorityPlane from material for render sorting
        if (sg.materialId >= 0 && sg.materialId < (int)mi.gpuMaterials.size())
            gg.priorityPlane = mi.gpuMaterials[sg.materialId].cpu.priorityPlane;

        uint32_t vbBytes = (uint32_t)(sizeof(Vertex) * sg.vertices.size());

        // 1. Base vertex buffer (immutable structured SRV for compute skinning)
        gg.baseVertBuf = gfx_->CreateBuffer({
            .size          = vbBytes,
            .elementStride = sizeof(Vertex),
            .usage         = gfx::BufferUsage::ShaderResource,
        }, sg.vertices.data());

        // 2. Weight buffer (immutable structured SRV) — pack VertexInfluence into uint4 + float4
        struct GPUWeight { uint32_t boneIdx[4]; float weight[4]; };
        static_assert(sizeof(GPUWeight) == 32, "GPUWeight must be 32 bytes");
        std::vector<GPUWeight> gpuWeights(gg.vertexCount);
        const GeosetSkinInfo* skinInfo = mi.skinning.GetGeosetWeights(id);
        if (skinInfo && (int)skinInfo->vertices.size() == gg.vertexCount) {
            for (int v = 0; v < gg.vertexCount; v++) {
                const auto& inf = skinInfo->vertices[v];
                for (int j = 0; j < 4; j++) {
                    gpuWeights[v].boneIdx[j] = (uint32_t)inf.boneIdx[j];
                    gpuWeights[v].weight[j]  = inf.weight[j];
                }
            }
        }

        gg.weightBuf = gfx_->CreateBuffer({
            .size          = (uint32_t)(sizeof(GPUWeight) * gg.vertexCount),
            .elementStride = sizeof(GPUWeight),
            .usage         = gfx::BufferUsage::ShaderResource,
        }, gpuWeights.data());

        // 3. Skinned output buffer (structured UAV for compute output)
        gg.skinnedBuf = gfx_->CreateBuffer({
            .size          = vbBytes,
            .elementStride = sizeof(Vertex),
            .usage         = gfx::BufferUsage::UnorderedAccess,
        });

        // 4. Vertex buffer for drawing (DEFAULT — CopyBuffer target from skinned output)
        gg.vb = gfx_->CreateBuffer({
            .size  = vbBytes,
            .usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::GpuWritable,
        }, sg.vertices.data());

        // Index buffer (immutable)
        gg.ib = gfx_->CreateBuffer({
            .size  = (uint32_t)(sizeof(uint32_t) * sg.indices.size()),
            .usage = gfx::BufferUsage::Index,
        }, sg.indices.data());

        // HD-path vertex buffers: vs/hd.bls performs FourBoneSkinning in
        // the VS itself, so the HD draw binds rest-pose data directly and
        // does not use geo.vb (which holds the compute-skinned output for
        // the SD paths). Three dedicated side streams so the compute's
        // SRV state on baseVertBuf doesn't collide with a Vertex-state
        // binding on the same resource mid-frame:
        //   unskinnedVb : ATTR0..ATTR3 (slot 0, same Vertex layout as vb)
        //   tangentVb   : ATTR7        (slot 1, float4 -- xyz dir, w handedness)
        //   boneVb      : ATTR5/ATTR6  (slot 2, 4-bone weights + indices)
        gg.unskinnedVb = gfx_->CreateBuffer({
            .size  = vbBytes,
            .usage = gfx::BufferUsage::Vertex,
        }, sg.vertices.data());

        if ((int)sg.tangents.size() == gg.vertexCount) {
            gg.tangentVb = gfx_->CreateBuffer({
                .size  = (uint32_t)(sizeof(Vector4f) * sg.tangents.size()),
                .usage = gfx::BufferUsage::Vertex,
            }, sg.tangents.data());
        }

        // Pack VertexInfluence into MeshVertexSD-style BoneVertex: the
        // first 4 bytes are weights as R8G8B8A8_UNORM (quantised to
        // /255 and normalised to sum to 255), the next 4 are bone
        // indices as R8G8B8A8_UINT. Exactly matches the ATTR5/ATTR6
        // layout in kMeshSDSkinned (see bls_pso_builder.cpp:39-40).
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

        mi.gpuGeosets.push_back(gg);
    }
    mi.stagedGeosets.clear();

    // Detect whether this model has a real LOD chain. Mirrors Previewd's
    // HasLODs @0x1401b6670 intent: if any geoset carries a non-zero LOD
    // (and not the 0xFFFFFFFF always-render sentinel), there are multiple
    // LOD levels to select between. Classic / pre-v900 models leave all
    // lods at 0 and we pin selectedLOD to 0 so nothing gets filtered out.
    mi.hasLods = false;
    for (const auto& g : mi.gpuGeosets) {
        if (g.lod != 0 && g.lod != 0xFFFFFFFFu) { mi.hasLods = true; break; }
    }
}

void RenderService::CreateNodePalette(ModelInstance& mi) {
    for (auto& geo : mi.gpuGeosets)
        geo.hasSkinning = true;

    int nodeCount = mi.skinning.NodeCount();
    if (nodeCount > 0 && mi.nodePalette == gfx::BufferHandle::Invalid) {
        mi.nodePalette = gfx_->CreateBuffer({
            .size          = (uint32_t)(sizeof(Matrix44f) * nodeCount),
            .elementStride = sizeof(Matrix44f),
            .usage         = gfx::BufferUsage::ShaderResource | gfx::BufferUsage::CpuWritable,
        });
    }
    // Companion constant buffer consumed by vs/hd.bls's FourBoneSkinning
    // policy. Same bone data as nodePalette, packed as BonePaletteCb
    // (256 entries of 3 float4 rows = 12288 B) so the HD VS can read
    // vsCB3.matrices[i] directly via its `mul(row-vec, matrix)`
    // convention in bls_frame.cpp::PackBone.
    if (nodeCount > 0 && mi.bonePaletteCb == gfx::BufferHandle::Invalid) {
        mi.bonePaletteCb = gfx_->CreateBuffer({
            .size  = sizeof(bls::BonePaletteCb),
            .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
        });
    }
}

void RenderService::ProcessStagedData() {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Remove models marked for clear. Also drop their emitters from the PE2
    // service so stale entries don't accumulate across reloads.
    for (auto it = models_.begin(); it != models_.end(); ) {
        if (it->second->stagedClear) {
            const uint32_t clearedHandle = it->first;
            it->second->ReleaseGPU(*gfx_);
            it = models_.erase(it);
            particleService_.RemoveModel(clearedHandle);
        } else {
            ++it;
        }
    }

    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (!mi->stagedDirty && !mi->skinDirty) continue;

        if (mi->stagedDirty) {
            UploadStagedTextures(*mi);

            // Copy materials (CPU data for render logic)
            for (auto& [id, sm] : mi->stagedMaterials) {
                if ((int)mi->gpuMaterials.size() <= id) mi->gpuMaterials.resize(id + 1);
                mi->gpuMaterials[id].cpu = sm;
            }
            mi->stagedMaterials.clear();

            UploadStagedGeosets(*mi);
            mi->stagedDirty = false;
        }

        if (mi->skinDirty) {
            CreateNodePalette(*mi);
            mi->skinDirty = false;
        }
    }
}

void RenderService::ReleaseModelGPU() {
    for (auto& [h, miPtr] : models_)
        miPtr->ReleaseGPU(*gfx_);
}

// Build a packed Texture2DArray for a material.
// All layer textures are CPU-resized (nearest-neighbor) to the max (w,h) found
// ============================================================================
// Animation Update (render thread)
// ============================================================================

void RenderService::UpdateAnimation() {
    // GPU compute skinning: upload bone palette, dispatch per geoset
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Quick check: any model needs skinning?
    bool anySkinned = false;
    for (auto& [h, miPtr] : models_) {
        if (miPtr->skinning.HasSkeleton() && miPtr->skinning.IsReady()
            && miPtr->nodePalette != gfx::BufferHandle::Invalid) {
            anySkinned = true;
            break;
        }
    }
    if (!anySkinned) return;

    auto* cmd = gfx_->GetImmediateContext();

    // Compute skinning writes into gg.vb for the SD / SD_on_HD draw paths
    // that bind gg.vb at slot 0. The HD path reads rest-pose vertices from
    // unskinnedVb and has the VS perform FourBoneSkinning in vs/hd.bls, so
    // gg.vb is never touched there -- skip the dispatch entirely when the
    // active mode is HD. The bone palette CB upload above still runs so
    // vsCB3 is populated for the HD draws further down the frame.
    const bool skipComputeSkin = (renderMode_ == RenderMode::HD);
    if (!skipComputeSkin)
        cmd->BindPipeline(skinPSO_);

    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (!mi->skinning.HasSkeleton() || !mi->skinning.IsReady()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent — skip skinning
        if (mi->nodePalette == gfx::BufferHandle::Invalid) continue;

        mi->skinning.ComputeOffsetMatrices();

        // Upload offset matrices to the node palette buffer (SRV, used by
        // the legacy csSkin compute path)
        void* mapped = gfx_->MapBuffer(mi->nodePalette);
        if (!mapped) continue;
        memcpy(mapped, mi->skinning.OffsetMatrices(),
               sizeof(Matrix44f) * mi->skinning.NodeCount());
        gfx_->UnmapBuffer(mi->nodePalette);

        // Companion CB (b3) consumed by vs/hd.bls. Pack the same offset
        // matrices into ShaderBone (3 rows of 4 floats) via BuildBonePalette;
        // the HD VS reads vsCB3.matrices[i] to skin vertices in the VS.
        if (mi->bonePaletteCb != gfx::BufferHandle::Invalid) {
            if (auto bp = bls::ScopedCb<bls::BonePaletteCb>(gfx_.get(), mi->bonePaletteCb)) {
                bls::BuildBonePalette(*bp, mi->skinning.OffsetMatrices(),
                                      mi->skinning.NodeCount());
            }
        }

        // Bind node palette SRV (slot 0) — shared across all geosets of this model
        cmd->BindShaderResource(gfx::ShaderStage::Compute, 0, mi->nodePalette);

        if (skipComputeSkin) continue;

        for (auto& geo : mi->gpuGeosets) {
            if (geo.skinnedBuf == gfx::BufferHandle::Invalid) continue;

            // Slots: t0 = BonePalette, t1 = BaseVerts, t2 = Weights
            cmd->BindShaderResource(gfx::ShaderStage::Compute, 1, geo.baseVertBuf);
            cmd->BindShaderResource(gfx::ShaderStage::Compute, 2, geo.weightBuf);

            // UAV: u0 = OutVerts
            cmd->BindUnorderedAccess(0, geo.skinnedBuf);

            // Dispatch: one thread per vertex, ceil(vertCount / 256)
            uint32_t groups = (geo.vertexCount + 255) / 256;
            cmd->Dispatch(groups, 1, 1);

            // Unbind UAV before CopyBuffer (avoid resource hazard)
            cmd->BindUnorderedAccess(0, gfx::BufferHandle::Invalid);

            // Copy structured output to plain vertex buffer for drawing
            cmd->CopyBuffer(geo.vb, geo.skinnedBuf);
        }
    }

    // Unbind compute resources to avoid hazards with vertex/pixel stages
    cmd->BindShaderResource(gfx::ShaderStage::Compute, 0, gfx::BufferHandle::Invalid);
    cmd->BindShaderResource(gfx::ShaderStage::Compute, 1, gfx::BufferHandle::Invalid);
    cmd->BindShaderResource(gfx::ShaderStage::Compute, 2, gfx::BufferHandle::Invalid);
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

namespace {

// Map the PE2 service's material FilterMode enum to the renderer's legacy
// FILTER_* integer constants used by LookupMeshPSO. The two enums line up
// with the MDX FilterMode byte (docs/PARTICLEEMITTERS2.md §3.9 table), we
// just spell out the conversion because they're independent types.
int ServiceFilterToLegacy(particle::FilterMode fm) {
    switch (fm) {
        case particle::FilterMode::Blend:      return FILTER_BLEND;
        case particle::FilterMode::Additive:   return FILTER_ADDITIVE;
        case particle::FilterMode::Modulate:   return FILTER_MODULATE;
        case particle::FilterMode::Modulate2X: return FILTER_MODULATE_2X;
        case particle::FilterMode::AlphaKey:   return FILTER_TRANSPARENT;
    }
    return FILTER_BLEND;
}

} // namespace

bool RenderService::RenderParticlesBls() {
    // Route PE2 particles through the stock Blizzard SD VS + SD PS instead of
    // our Slang mesh shader. Returns true if the BLS path ran (success or
    // benignly skipped); false only if the program failed to load.
    if (!blsSdProgram_ || !blsPsoBuilder_) return false;

    auto* cmd = gfx_->GetImmediateContext();

    std::vector<Vertex> verts;
    std::vector<particle::EmitterDrawList> drawLists;
    Matrix44f viewMat;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (particleService_.EmitterCount() == 0) return true;
        viewMat = camera_.GetViewMatrix();
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
    frame.projection = camera_.ProjectionRH(aspect);
    frame.effectTime = animationTimeMs_.load() * 0.001f;
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
        bool hasModelTex = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            ModelInstance* owner = getModel(dl.model);
            if (owner && dl.material.textureId >= 0) {
                auto it = owner->gpuTextures.find(dl.material.textureId);
                if (it != owner->gpuTextures.end() && it->second.tex != gfx::TextureHandle::Invalid) {
                    cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, it->second.tex);
                    hasModelTex = true;
                }
            }
        }
        if (!hasModelTex) {
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, defaultTex_);
        }
        cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerWrap_[wrapFlags]);

        cmd->Draw(dl.vertexCount, drawOffset);
        drawOffset += dl.vertexCount;
    }
    return true;
}

void RenderService::RenderParticles() {
    if (RenderParticlesBls()) return;   // BLS path took over
    // Legacy Slang fallback -- Single service-driven path. Snapshot geometry +
    // view matrix under the data lock, upload and issue draws without the lock held.
    auto* cmd = gfx_->GetImmediateContext();

    std::vector<Vertex> verts;
    std::vector<particle::EmitterDrawList> drawLists;
    Matrix44f viewMat;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (particleService_.EmitterCount() == 0) return;
        viewMat = camera_.GetViewMatrix();
        particleService_.BuildGeometry(viewMat, verts, drawLists);
    }
    if (verts.empty()) return;

    const int vertCount = (int)verts.size();

    // Grow the global particle VB if needed.
    if (particleServiceVB_ == gfx::BufferHandle::Invalid || vertCount > particleServiceVBSize_) {
        gfx_->Destroy(particleServiceVB_);
        int newSize = (std::max)(vertCount, 4096);
        gfx::BufferDesc bd;
        bd.size  = (uint32_t)(sizeof(Vertex) * newSize);
        bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
        particleServiceVB_     = gfx_->CreateBuffer(bd);
        particleServiceVBSize_ = newSize;
    }

    // Upload.
    if (void* mapped = gfx_->MapBuffer(particleServiceVB_)) {
        memcpy(mapped, verts.data(), sizeof(Vertex) * vertCount);
        gfx_->UnmapBuffer(particleServiceVB_);
    }

    cmd->BindVertexBuffer(0, particleServiceVB_, sizeof(Vertex));

    // Render each emitter's slice.
    int drawOffset = 0;
    for (const auto& dl : drawLists) {
        if (dl.vertexCount <= 0) continue;

        const int legacyFilter = ServiceFilterToLegacy(dl.material.filterMode);
        cmd->BindPipeline(LookupMeshPSO(legacyFilter, /*twoSided*/true,
                                        /*noDepthTest*/false, /*noDepthSet*/true));

        {
            const float alphaRef = (legacyFilter == FILTER_TRANSPARENT) ? 0.75f : 0.0f;
            const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            render_detail::CbPerFrameDesc d;
            d.view         = viewMat;
            d.projection   = camera_.ProjectionRH(aspect);
            d.lightDir     = render_detail::NormalizedLightDir4(kDefaultLightDir);
            d.lightColor   = kParticleLightColor;
            d.ambientColor = {kParticleAmbientBase.x, kParticleAmbientBase.y, kParticleAmbientBase.z, alphaRef};
            d.materialFlags = {dl.material.unshaded ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            render_detail::WriteCbPerFrame(gfx_.get(), cbPerFrame_, d);
        }
        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
        cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, cbPerFrame_);

        // Texture lookup — textures are per-ModelInstance; the owner lookup
        // needs the data mutex, but the shader-resource bind that follows
        // is safe to issue under the lock since it's immediate-context.
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            if (ModelInstance* owner = getModel(dl.model)) {
                render_detail::BindLayerAlbedo(cmd, *owner, dl.material.textureId,
                                               defaultTex_, samplerWrap_);
            } else {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, defaultTex_);
                cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerWrap_[kWrapFlagsMask]);
            }
        }

        cmd->Draw(dl.vertexCount, drawOffset);
        drawOffset += dl.vertexCount;
    }
}

// ============================================================================
// Ribbon Simulation + Rendering (render thread)
// ============================================================================

void RenderService::UpdateRibbons(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : models_) {
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        mi->ribbons.Simulate(dt);
    }
}

void RenderService::RenderRibbons() {
    auto* cmd = gfx_->GetImmediateContext();

    for (auto& [_mh, _mi] : models_) {
    auto* mi = _mi.get();
    Matrix44f viewMat;
    RibbonSystem::StripResult stripResult;
    std::vector<RibbonEmitterConfig> configs;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!mi->ribbons.HasEmitters()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        viewMat = camera_.GetViewMatrix();
        stripResult = mi->ribbons.BuildStrips();
        for (int eid : stripResult.emitterIds) {
            auto* c = mi->ribbons.GetConfig(eid);
            configs.push_back(c ? *c : RibbonEmitterConfig{});
        }
    }

    auto& verts = stripResult.vertices;
    auto& emitterIds = stripResult.emitterIds;
    if (verts.empty()) continue;
    int vertCount = (int)verts.size();

    // Grow ribbon VB if needed
    if (mi->ribbonVB == gfx::BufferHandle::Invalid || vertCount > mi->ribbonVBSize) {
        gfx_->Destroy(mi->ribbonVB);
        int newSize = (std::max)(vertCount, 512);
        gfx::BufferDesc bd;
        bd.size  = (uint32_t)(sizeof(Vertex) * newSize);
        bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
        mi->ribbonVB = gfx_->CreateBuffer(bd);
        mi->ribbonVBSize = newSize;
    }

    // Upload vertex data
    void* mapped = gfx_->MapBuffer(mi->ribbonVB);
    if (!mapped) continue;
    memcpy(mapped, verts.data(), sizeof(Vertex) * vertCount);
    gfx_->UnmapBuffer(mi->ribbonVB);

    // Bind ribbon VB
    cmd->BindVertexBuffer(0, mi->ribbonVB, sizeof(Vertex));

    int drawOffset = 0;
    std::vector<int> vertCounts;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (int eid : emitterIds)
            vertCounts.push_back(mi->ribbons.GetEmitterVertCount(eid));
    }

    for (int ei = 0; ei < (int)emitterIds.size(); ei++) {
        auto& cfg = configs[ei];
        int count = vertCounts[ei];
        if (count <= 0) { continue; }

        // Ribbons: use cfg.twoSided, always noWrite depth
        cmd->BindPipeline(LookupMeshPSO(cfg.filterMode, cfg.twoSided, false, true));

        {
            float alphaRef = (cfg.filterMode == FILTER_TRANSPARENT) ? 0.75f : 0.0f;
            float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            render_detail::CbPerFrameDesc d;
            d.view         = viewMat;
            d.projection   = camera_.ProjectionRH(aspect);
            d.lightDir     = render_detail::NormalizedLightDir4(kDefaultLightDir);
            d.lightColor   = kParticleLightColor;
            d.ambientColor = {kParticleAmbientBase.x, kParticleAmbientBase.y, kParticleAmbientBase.z, alphaRef};
            d.materialFlags = {cfg.unshaded ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            render_detail::WriteCbPerFrame(gfx_.get(), cbPerFrame_, d);
        }

        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
        cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, cbPerFrame_);

        render_detail::BindLayerAlbedo(cmd, *mi, cfg.textureId,
                                       defaultTex_, samplerWrap_);

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
    Vector3f camPos = camera_.GetSource();
    float viewDist = std::sqrt(camPos.x*camPos.x + camPos.y*camPos.y + camPos.z*camPos.z);
    if (viewDist < 1.0f) return 0;

    const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    Matrix44f proj = camera_.ProjectionLH(aspect);
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

    // Samplers (via GFX)
    {
        using AM = gfx::AddressMode;
        gfx::SamplerDesc sd;
        samplerLinear_ = gfx_->CreateSampler(sd); // default: linear, wrap

        AM modes[2] = { AM::Clamp, AM::Wrap };
        for (int i = 0; i < 4; i++) {
            sd.addressU = modes[(i >> 0) & 1];
            sd.addressV = modes[(i >> 1) & 1];
            sd.addressW = AM::Clamp;
            samplerWrap_[i] = gfx_->CreateSampler(sd);
        }
    }

    // Constant buffer (via GFX)
    cbPerFrame_ = gfx_->CreateBuffer({
        .size  = sizeof(CBPerFrame),
        .usage = gfx::BufferUsage::Constant | gfx::BufferUsage::CpuWritable,
    });

    // 1x1 white default texture (via GFX)
    {
        uint32_t white = 0xFFFFFFFF;
        defaultTex_ = gfx_->CreateTexture({
            .width  = 1,
            .height = 1,
            .format = gfx::Format::R8G8B8A8_UNORM,
            .usage  = gfx::TextureUsage::ShaderResource,
        }, &white);
    }
    // 1x1 RGBA(0,0,0,0) — bound to t2 ORM / t3 emissive / t4 teamColor when
    // a v1200 HD layer doesn't author those subtextures. orm.w = 0 zeroes
    // the multiLayerBlend team-colour weight (see effects/multi_layer.slang),
    // so unauthored layers stop painting the entire surface with team hue.
    {
        uint32_t zero = 0x00000000;
        defaultBlack_ = gfx_->CreateTexture({
            .width  = 1,
            .height = 1,
            .format = gfx::Format::R8G8B8A8_UNORM,
            .usage  = gfx::TextureUsage::ShaderResource,
        }, &zero);
    }
    // 1x1 flat normal. decodeNormalMap does nx = 2*r*a - 1, so r=0.5 a=1.0
    // (packed 0x80 and 0xFF) yields nx = ny = 0, nz = 1 in the shader.
    {
        uint32_t flatN = 0xFF808080u;
        defaultNormal_ = gfx_->CreateTexture({
            .width  = 1,
            .height = 1,
            .format = gfx::Format::R8G8B8A8_UNORM,
            .usage  = gfx::TextureUsage::ShaderResource,
        }, &flatN);
    }
    // 1x1 ORM fallback — R=1 (occlusion), G=1 (roughness), B=0 (metalness),
    // A=0 (team blend weight). The roughness=1 is critical: the HD IBL
    // path reads `orm.y = roughness` and uses it to pick a cubemap mip.
    // With roughness=0 (mirror), every fragment samples mip 0 of the
    // specular cube and the cube's sky/ground horizon shows up as a
    // sharp bright/dark split right at the view centre. Defaulting to
    // roughness=1 (fully matte) samples the smallest mip, effectively
    // a uniform probe-average tint — no seam, no hot reflection.
    // Byte order: R8G8B8A8 little-endian → 0x0000FFFF = R=0xFF, G=0xFF, B=0, A=0.
    {
        uint32_t ormNeutral = 0x0000FFFFu;
        defaultOrm_ = gfx_->CreateTexture({
            .width  = 1,
            .height = 1,
            .format = gfx::Format::R8G8B8A8_UNORM,
            .usage  = gfx::TextureUsage::ShaderResource,
        }, &ormNeutral);
    }

    // Shaders + pipelines + default geometry (grid, view cube) are part of device init
    if (!CreateShaders())          { CleanupD3D(); return false; }
    if (!CreatePipelines())        { CleanupD3D(); return false; }
    if (!CreateDefaultResources()) { CleanupD3D(); return false; }

    // BLS shaders live alongside Slang shaders. Missing .bls files are tolerated.
    InitBlsShaders();

    return true;
}

bool RenderService::InitBlsShaders() {
    if (!gfx_ || !activeContentProvider_) return false;

    blsShaderCache_ = std::make_unique<bls::BlsShaderCache>(gfx_.get(), activeContentProvider_);
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
    iblSplitSumLut_ = ibl::CreateSplitSumLutTexture(*gfx_);

    // Default to the portrait-tuned probe — softer, horizontally
    // isotropic, no sharp sun/horizon features. Avoids the "dark
    // patches in concavities" look that Day_IBL's directional
    // content produces on close-range preview subjects. SetEnvProbe()
    // swaps at runtime via the UI combo.
    SetEnvProbe(ibl::kPortraitIblPath);

    return blsSdProgram_ != nullptr;
}

void RenderService::SetEnvProbe(const std::string& relPath) {
    if (!gfx_) return;

    // Destroy the previous pair; skip double-free when from/to alias.
    if (iblFromProbe_ != gfx::TextureHandle::Invalid) {
        gfx_->Destroy(iblFromProbe_);
        if (iblToProbe_ != iblFromProbe_ && iblToProbe_ != gfx::TextureHandle::Invalid)
            gfx_->Destroy(iblToProbe_);
        iblFromProbe_ = gfx::TextureHandle::Invalid;
        iblToProbe_   = gfx::TextureHandle::Invalid;
    }

    int mips = 0;
    if (!relPath.empty() && activeContentProvider_) {
        auto probe = ibl::LoadEnvProbe(*gfx_, *activeContentProvider_, relPath);
        if (probe.handle != gfx::TextureHandle::Invalid) {
            iblFromProbe_ = probe.handle;
            mips          = probe.mipCount;
        }
    }
    if (iblFromProbe_ == gfx::TextureHandle::Invalid) {
        OutputDebugStringA("[WDEX IBL] probe load failed — using procedural debug probe\n");
        iblFromProbe_ = ibl::CreateDebugFacesEnvProbe(*gfx_);
        mips          = ibl::kEnvProbeMipLevels;
    }
    iblToProbe_     = iblFromProbe_;           // aliased; envTransitionT=0 picks "from"
    iblProbeMipEnd_ = static_cast<float>(mips - 1);
}

void RenderService::ShutdownBlsShaders() {
    blsSdProgram_     = nullptr;
    blsSdOnHdProgram_ = nullptr;
    blsHdProgram_     = nullptr;
    if (gfx_) {
        gfx_->Destroy(blsSdVsCb_);     blsSdVsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsSdPsCb_);     blsSdPsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsHdVsCb_);     blsHdVsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsHdPsCb_);     blsHdPsCb_     = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsSdOnHdPsCb_); blsSdOnHdPsCb_ = gfx::BufferHandle::Invalid;
        gfx_->Destroy(blsHdDebugVisCb_); blsHdDebugVisCb_ = gfx::BufferHandle::Invalid;
        gfx_->Destroy(iblSplitSumLut_); iblSplitSumLut_ = gfx::TextureHandle::Invalid;
        // from + to may alias when the night probe failed to load -- guard
        // the second destroy so we don't touch a freed handle.
        gfx_->Destroy(iblFromProbe_);
        if (iblToProbe_ != iblFromProbe_) gfx_->Destroy(iblToProbe_);
        iblFromProbe_ = gfx::TextureHandle::Invalid;
        iblToProbe_   = gfx::TextureHandle::Invalid;
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

    target.color = gfx_->GetSwapChainBackBuffer(target.swap);
    target.depth = gfx_->CreateDepthTarget(w, h, gfx::Format::D24_UNORM_S8_UINT);
    target.width = w;
    target.height = h;

    RenderTargetId id = target.id;
    targets_[id] = target;
    return id;
}

RenderTargetId RenderService::CreateOffscreenTarget(int w, int h) {
    if (!gfx_) return 0;

    RenderTarget target;
    target.id    = nextTargetId_++;
    target.color = gfx_->CreateColorTarget(w, h, gfx::Format::R8G8B8A8_UNORM);
    target.depth = gfx_->CreateDepthTarget(w, h, gfx::Format::D24_UNORM_S8_UINT);
    target.width = w;
    target.height = h;

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
    gfx_->Destroy(t.depth);
    targets_.erase(it);
}

void RenderService::ResizeRenderTarget(RenderTargetId id, int w, int h) {
    auto it = targets_.find(id);
    if (it == targets_.end() || !gfx_) return;
    auto& t = it->second;

    // Destroy old depth
    gfx_->Destroy(t.depth);

    if (t.swap != gfx::SwapChainHandle::Invalid) {
        gfx_->ResizeSwapChain(t.swap, w, h);
        t.color = gfx_->GetSwapChainBackBuffer(t.swap);
    } else {
        gfx_->Destroy(t.color);
        t.color = gfx_->CreateColorTarget(w, h, gfx::Format::R8G8B8A8_UNORM);
    }

    t.depth = gfx_->CreateDepthTarget(w, h, gfx::Format::D24_UNORM_S8_UINT);
    t.width = w;
    t.height = h;
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
        // Shaders
        gfx_->Destroy(meshVS_);  gfx_->Destroy(meshPS_);
        gfx_->Destroy(lineVS_);  gfx_->Destroy(linePS_);
        gfx_->Destroy(skinCS_);

        // Pipelines
        for (auto& fm : meshPSO_)
            for (auto& cu : fm)
                for (auto& dp : cu)
                    gfx_->Destroy(dp);
        gfx_->Destroy(linePSO_);
        gfx_->Destroy(skinPSO_);

        // Resources
        gfx_->Destroy(cbPerFrame_);
        gfx_->Destroy(samplerLinear_);
        for (auto& s : samplerWrap_) gfx_->Destroy(s);
        gfx_->Destroy(defaultTex_);
        gfx_->Destroy(defaultBlack_);
        gfx_->Destroy(defaultNormal_);
        gfx_->Destroy(defaultOrm_);
        gfx_->Destroy(teamColorTex_); teamColorTex_ = gfx::TextureHandle::Invalid;

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

    meshVS_ = gfx_->CreateShader(gfx::ShaderStage::Vertex,  kMeshVS, sizeof(kMeshVS));
    meshPS_ = gfx_->CreateShader(gfx::ShaderStage::Pixel,   kMeshPS, sizeof(kMeshPS));
    lineVS_ = gfx_->CreateShader(gfx::ShaderStage::Vertex,  kLineVS, sizeof(kLineVS));
    linePS_ = gfx_->CreateShader(gfx::ShaderStage::Pixel,   kLinePS, sizeof(kLinePS));
    skinCS_ = gfx_->CreateShader(gfx::ShaderStage::Compute, kSkinCS, sizeof(kSkinCS));

    return meshVS_ != gfx::ShaderHandle::Invalid
        && meshPS_ != gfx::ShaderHandle::Invalid
        && lineVS_ != gfx::ShaderHandle::Invalid
        && linePS_ != gfx::ShaderHandle::Invalid
        && skinCS_ != gfx::ShaderHandle::Invalid;
}

// ============================================================================
// Pipeline (PSO) Creation
// ============================================================================

// Blend desc per FilterMode (exact Magos mapping)
static gfx::BlendDesc FilterBlend(int filterMode) {
    using BF = gfx::BlendFactor;
    gfx::BlendDesc b;
    switch (filterMode) {
    case FILTER_NONE:
        b.enable = false;
        break;
    case FILTER_TRANSPARENT: case FILTER_BLEND:
        b.enable = true;
        b.srcColor = BF::SrcAlpha; b.dstColor = BF::InvSrcAlpha;
        break;
    case FILTER_ADDITIVE: case FILTER_ADD_ALPHA:
        b.enable = true;
        b.srcColor = BF::SrcAlpha; b.dstColor = BF::One;
        break;
    case FILTER_MODULATE:
        b.enable = true;
        b.srcColor = BF::Zero; b.dstColor = BF::SrcColor;
        break;
    case FILTER_MODULATE_2X:
        b.enable = true;
        b.srcColor = BF::DstColor; b.dstColor = BF::SrcColor;
        break;
    }
    return b;
}

// Depth desc: 0 = default (write+test), 1 = noWrite, 2 = disabled
static gfx::DepthStencilDesc MakeDepth(int depthKey) {
    gfx::DepthStencilDesc d;
    d.depthCompare = gfx::CompareOp::LessEqual;
    switch (depthKey) {
    case 0: d.depthTest = true;  d.depthWrite = true;  break;
    case 1: d.depthTest = true;  d.depthWrite = false; break;
    case 2: d.depthTest = false; d.depthWrite = false; break;
    }
    return d;
}

bool RenderService::CreatePipelines() {
    using namespace gfx;

    // Input layouts
    InputElement meshInput[] = {
        {"POSITION", 0, Format::R32G32B32_FLOAT,    0},
        {"NORMAL",   0, Format::R32G32B32_FLOAT,   12},
        {"COLOR",    0, Format::R32G32B32A32_FLOAT, 24},
        {"TEXCOORD", 0, Format::R32G32_FLOAT,       40},
    };
    InputElement lineInput[] = {
        {"POSITION", 0, Format::R32G32B32_FLOAT,    0},
        {"COLOR",    0, Format::R32G32B32A32_FLOAT, 12},
    };

    // Build all mesh PSOs: [7 filters][2 cull][3 depth]
    for (int f = 0; f < 7; f++) {
        BlendDesc blend = FilterBlend(f);
        for (int c = 0; c < 2; c++) {
            for (int d = 0; d < 3; d++) {
                GraphicsPipelineDesc desc;
                desc.vs          = meshVS_;
                desc.ps          = meshPS_;
                desc.inputLayout = meshInput;
                desc.topology    = PrimitiveTopology::TriangleList;
                desc.blend       = blend;
                desc.depthStencil = MakeDepth(d);
                desc.rasterizer.cull     = (c == 0) ? CullMode::Back : CullMode::None;
                desc.rasterizer.frontCCW = true;
                meshPSO_[f][c][d] = gfx_->CreateGraphicsPipeline(desc);
            }
        }
    }

    // Line PSO (grid, collision, viewcube edges): opaque + default depth + noCull + LineList
    {
        GraphicsPipelineDesc desc;
        desc.vs          = lineVS_;
        desc.ps          = linePS_;
        desc.inputLayout = lineInput;
        desc.topology    = PrimitiveTopology::LineList;
        desc.blend.enable = false;
        desc.depthStencil = MakeDepth(0);
        desc.rasterizer.cull     = CullMode::None;
        desc.rasterizer.frontCCW = true;
        linePSO_ = gfx_->CreateGraphicsPipeline(desc);
    }

    // Skin compute PSO
    skinPSO_ = gfx_->CreateComputePipeline({skinCS_});

    return linePSO_ != PipelineHandle::Invalid
        && skinPSO_ != PipelineHandle::Invalid;
}

gfx::PipelineHandle RenderService::LookupMeshPSO(int filterMode, bool twoSided,
                                                   bool noDepthTest, bool noDepthSet) const {
    int f = (filterMode >= 0 && filterMode < 7) ? filterMode : 0;
    int c = twoSided ? 1 : 0;
    int d;
    if (noDepthTest) d = 2;
    else if (noDepthSet) d = 1;
    else d = (f <= 1) ? 0 : 1; // NONE/TRANSPARENT default to write, rest to noWrite
    return meshPSO_[f][c][d];
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
    if (target.color == gfx::TextureHandle::Invalid || !gfx_) return;

    // Re-sample the active MDX camera animator every frame.
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (activeCameraPresetIdx_ >= 0 &&
            activeCameraPresetIdx_ < (int)cameraPresets_.size()) {
            const auto& preset = cameraPresets_[activeCameraPresetIdx_];
            if (preset.animator) {
                int seqStart = 0, seqEnd = 0;
                int idx = activeSequence_.load();
                if (idx >= 0 && idx < (int)sequenceRanges_.size()) {
                    seqStart = sequenceRanges_[idx].startMs;
                    seqEnd   = sequenceRanges_[idx].endMs;
                }
                if (seqStart == 0 && seqEnd == 0) {
                    seqEnd = 1 << 30;
                }
                Vector3f pos  = preset.position;
                Vector3f tgt  = preset.target;
                float    roll = preset.staticRoll;
                preset.animator(pos, tgt, roll,
                                animationTimeMs_.load(), seqStart, seqEnd);
                camera_.SetDirectPose(pos, tgt, roll);
            }
        }
    }

    auto* cmd = gfx_->GetImmediateContext();
    float clearColor[4] = {0.39f, 0.39f, 0.40f, 1.0f};  // Magos-matched gray
    cmd->BeginRenderPass(target.color, target.depth, clearColor, 1.0f, 0);
    cmd->SetViewport({0, 0, (float)target.width, (float)target.height, 0, 1});

    Matrix44f view, proj;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        view = camera_.GetViewMatrix();
    }
    float aspect = (target.height > 0) ? (float)target.width / (float)target.height : 1.0f;
    proj = camera_.ProjectionRH(aspect);

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
    if (showParticles_) RenderParticles();
    if (showRibbons_) RenderRibbons();
    if (showCollisions_) debug_->RenderCollisions();
    if (showLights_)     debug_->RenderLightMarkers();
    debug_->RenderViewCube();
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
        view = rs_.camera_.GetViewMatrix();
        const float aspect = (rs_.height_ > 0)
            ? static_cast<float>(rs_.width_) / static_cast<float>(rs_.height_) : 1.0f;
        proj = rs_.camera_.ProjectionRH(aspect);
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
        ModelInstance* mi  = ref.mi;
        auto&          geo = mi->gpuGeosets[ref.idx];

        GPUMaterial* mat = nullptr;
        const int matId = geo.materialId;
        if (matId >= 0 && matId < (int)mi->gpuMaterials.size()) mat = &mi->gpuMaterials[matId];

        const float geoAlpha = geo.geosetAlpha * mi->parentVisibility;
        if (geoAlpha < 0.01f) return;

        int numLayers = mat ? (int)mat->cpu.layers.size() : 0;
        if (numLayers <= 0) numLayers = 1;

        render_detail::BindSdMeshGeometry(cmd, geo);

        for (int li = 0; li < numLayers; ++li) {
            const render_detail::UnpackedLayer layer = render_detail::UnpackLayer(mat, li);

            render_detail::ApplyTexAnimPaletteToFrame(frame, *mi, layer.textureAnimationId);

            const float combinedAlpha = geoAlpha * layer.alpha;
            if (combinedAlpha < 0.004f) continue;
            int effectiveFilter = layer.filterMode;
            if (combinedAlpha < 0.99f && layer.filterMode <= FILTER_TRANSPARENT)
                effectiveFilter = FILTER_BLEND;

            bls::MatParams mp = bls::FromMdxLayer(effectiveFilter, layer.flags, bls::GxShaderID::SD);
            // SelectModelMaterial mirror: diffuseColor = geoColor * combinedAlpha.
            // Modulate takes the alpha-into-RGB trick; per-vertex color already
            // carries the model tint so geoColor stays white by default.
            if (mp.alpha == bls::GxMatAlpha::Modulate) {
                mp.diffuseColor = {combinedAlpha, 1, 1, 1};
            } else {
                mp.diffuseColor = {geo.geosetColor.x, geo.geosetColor.y, geo.geosetColor.z, combinedAlpha};
            }

            // Unshaded layers (MAT_UNSHADED / kDisableLighting) skip the VS
            // lighting loop. Permute count must agree with the upload: the
            // "no lights" VS is picked when numLights=0.
            const bool unlit   = (mp.disables & bls::kDisableLighting) != 0;
            const int  activeN = unlit ? 0 : lightCountForGeoset;
            frame.numLights = activeN;

            const auto rs   = bls::MakeSdMeshRenderState(mp, activeN, unlit);
            const auto perm = bls::SelectPermutes(rs);
            // VertexLayoutKind::ParticleSD reuses our 48 B Vertex struct.
            const auto req  = bls::MakePsoRequest(rs_.blsSdProgram_,
                                                  bls::VertexLayoutKind::ParticleSD,
                                                  mp, perm);
            auto pso = rs_.blsPsoBuilder_->GetOrBuild(req);
            if (pso == gfx::PipelineHandle::Invalid) continue;
            cmd->BindPipeline(pso);

            frame.world = mi->worldTransform;

            if (auto vs = bls::ScopedCb<bls::SdVsCbA>(rs_.gfx_.get(), rs_.blsSdVsCb_)) {
                bls::BuildSdVsCbA(*vs, frame, mp);
            }
            if (auto ps = bls::ScopedCb<bls::SdPsCbA>(rs_.gfx_.get(), rs_.blsSdPsCb_)) {
                bls::BuildSdPsCbA(*ps, frame, mp);
            }
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, rs_.blsSdVsCb_);
            cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, rs_.blsSdPsCb_);

            render_detail::BindLayerAlbedo(cmd, *mi, layer.textureId,
                                           rs_.defaultTex_, rs_.samplerWrap_);

            cmd->DrawIndexed(geo.indexCount);
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
        view = rs_.camera_.ViewLH();
        proj = rs_.camera_.ProjectionLH(aspect);
    }

    void BindPassResources(gfx::IGFXCommandList* cmd, bls::FrameInputs& frame) {
        // Refresh the team-colour swatch once per pass (matches the UI picker).
        rs_.UpdateTeamColorSwatch();

        // Day at t13 / Night at t14 — envTransitionT=0.75 picks mostly day.
        // envMipEnd clamps the roughness→mip remap; nonzero keeps sampleIBL
        // off the "probe disabled" fast-out path.
        frame.envFromMipEnd  = rs_.iblProbeMipEnd_;
        frame.envToMipEnd    = rs_.iblProbeMipEnd_;
        frame.envTransitionT = 0.75f;

        // Dynamic sampler table covers s0..s3. Per-layer BindSampler(0, ...)
        // overrides s0 with the wrap-appropriate variant at draw time.
        cmd->BindSampler(gfx::ShaderStage::Pixel, 1, rs_.samplerLinear_);
        cmd->BindSampler(gfx::ShaderStage::Pixel, 2, rs_.samplerLinear_);
        cmd->BindSampler(gfx::ShaderStage::Pixel, 3, rs_.samplerLinear_);

        // s13..s15 are STATIC samplers baked into the root signature, so
        // we only bind the SRVs here. Binding them via the dynamic heap
        // would overflow D3D12's 2048-entry sampler cap and TDR.
        if (rs_.iblFromProbe_ != gfx::TextureHandle::Invalid)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 13, rs_.iblFromProbe_);
        if (rs_.iblToProbe_ != gfx::TextureHandle::Invalid)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 14, rs_.iblToProbe_);
        if (rs_.iblSplitSumLut_ != gfx::TextureHandle::Invalid)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 15, rs_.iblSplitSumLut_);
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
        ModelInstance* mi  = ref.mi;
        auto&          geo = mi->gpuGeosets[ref.idx];

        GPUMaterial* mat = nullptr;
        const int matId = geo.materialId;
        if (matId >= 0 && matId < (int)mi->gpuMaterials.size()) mat = &mi->gpuMaterials[matId];

        const float geoAlpha = geo.geosetAlpha * mi->parentVisibility;
        if (geoAlpha < 0.01f) return;

        int numLayers = mat ? (int)mat->cpu.layers.size() : 0;
        if (numLayers <= 0) numLayers = 1;

        // HD path uses vs/hd.bls with FourBoneSkinning — bind rest-pose data
        // from unskinnedVb, not the compute-skinned gg.vb. Dedicated rest-
        // pose VB (separate from baseVertBuf, which stays as a structured
        // SRV for the SD compute skinner) avoids cross-frame SRV<->Vertex
        // state churn on a shared resource. Falls back to gg.vb when the
        // side stream wasn't created (shouldn't happen for MDX models).
        const gfx::BufferHandle vb0 =
            (geo.unskinnedVb != gfx::BufferHandle::Invalid) ? geo.unskinnedVb : geo.vb;
        cmd->BindVertexBuffer(0, vb0, sizeof(Vertex));
        cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);

        // Tangent side-stream (ATTR7 on slot 1). Required by the HD VS
        // when the hasTangent permute is picked.
        const bool hasTangents = (geo.tangentVb != gfx::BufferHandle::Invalid);
        if (hasTangents)
            cmd->BindVertexBuffer(1, geo.tangentVb, sizeof(Vector4f));

        // Bone weights+indices feeding FourBoneSkinning (ATTR5/ATTR6) +
        // bone palette CB (vsCB3). Bone slot collapses onto slot 1 when
        // no tangents are present.
        const bool hasBones =
            (geo.boneVb != gfx::BufferHandle::Invalid) &&
            (mi->bonePaletteCb != gfx::BufferHandle::Invalid);
        if (hasBones) {
            const uint32_t boneSlot = hasTangents ? 2u : 1u;
            cmd->BindVertexBuffer(boneSlot, geo.boneVb, sizeof(BoneVertex));
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 3, mi->bonePaletteCb);
        }

        for (int li = 0; li < numLayers; ++li) {
            const render_detail::UnpackedLayer layer = render_detail::UnpackLayer(mat, li);

            render_detail::ApplyTexAnimPaletteToFrame(frame, *mi, layer.textureAnimationId);

            float combinedAlpha = geoAlpha * layer.alpha;
            if (combinedAlpha < 0.004f) continue;
            int effectiveFilter = layer.filterMode;
            if (combinedAlpha < 0.99f && layer.filterMode <= FILTER_TRANSPARENT)
                effectiveFilter = FILTER_BLEND;

            // Route program + permute family based on the MDX layer's
            // shader id. MatSelect (0x1403fa7c0) canonicalises SD <-> SD_on_HD;
            // Crystal shares the HD permute axes. Any mesh-authored HD
            // variant goes through hd.bls; SD-authored or unknown layers use
            // sd_on_hd.bls (which is the HD-mode fallback for legacy assets).
            const bool isHdMaterial =
                (layer.shaderId == 1) /* HD */ || (layer.shaderId == 24) /* Crystal */;
            const bls::GxShaderID programShaderId =
                isHdMaterial ? bls::GxShaderID::HD : bls::GxShaderID::SD_on_HD;
            const bls::BlsProgram* program =
                isHdMaterial ? rs_.blsHdProgram_ : rs_.blsSdOnHdProgram_;
            // Fall back to whichever program is available if the selected
            // one failed to load; keeps draws alive instead of going black.
            if (!program) program = rs_.blsHdProgram_ ? rs_.blsHdProgram_ : rs_.blsSdOnHdProgram_;
            if (!program) continue;

            bls::MatParams mp = bls::FromMdxLayer(effectiveFilter, layer.flags, programShaderId);
            if (mp.alpha == bls::GxMatAlpha::Modulate) {
                mp.diffuseColor = {combinedAlpha, 1, 1, 1};
            } else {
                mp.diffuseColor = {geo.geosetColor.x, geo.geosetColor.y, geo.geosetColor.z, combinedAlpha};
            }
            // HD PS pixelParams / fresnelColor sourced from the MDX layer.
            // Engine layout per CGxMatParams::PixelParams (IDA):
            //   pixelParams1 = {inverseSoftness, cloak, fresnelTeamColor, 0}
            //   fresnelColor = {fresnelR, fresnelG, fresnelB, fresnelOpacity}
            mp.emissiveGain     = layer.emissiveGain;
            mp.fresnelTeamColor = layer.fresnelTeamColor;
            mp.fresnelOpacity   = layer.fresnelOpacity;
            mp.fresnelColor     = layer.fresnelColor;

            // Phase 6: GFX supports TextureCubeArray now (kSrvsPerStage=16,
            // TextureDesc::isCube), so we can legally pick lights>0
            // permutes. The HD PS compiles HAS_IBL = (lights > 0) -- those
            // perms sample t13/t14/t15 which we bind above. The direct-
            // lighting path (psIBLBody) still runs Cook-Torrance over
            // ShaderLight[]; the BRDF LUT + env probe supply the IBL
            // contribution.
            const bool unlit   = (mp.disables & bls::kDisableLighting) != 0;
            const int  activeN = unlit ? 0 : lightCountForGeoset;
            frame.numLights = activeN;

            bls::RenderState rs;
            rs.shaderId       = programShaderId;
            rs.alphaMode      = static_cast<uint8_t>(mp.alpha);
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
            rs.depthWrite     = mp.DepthWriteEnabled();
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
            rs.teamColor      = (layer.teamColorMapId >= 0);
            const int  dbgMode     = rs_.hdDebugMode_.load();
            const bool debugActive = (dbgMode > 0);
            rs.debugShader = debugActive;
            auto perm = bls::SelectPermutes(rs);

            bls::PsoRequest req{};
            req.program   = program;
            req.vsIndex   = perm.vs;
            req.psIndex   = perm.ps;
            req.material  = mp;
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
            req.rtvFormat = gfx::Format::R8G8B8A8_UNORM;
            req.dsvFormat = gfx::Format::D24_UNORM_S8_UINT;
            req.lhClipSpace = true;  // HD/SD_on_HD stack (distinct PSO hash)
            auto pso = rs_.blsPsoBuilder_->GetOrBuild(req);
            if (pso == gfx::PipelineHandle::Invalid) continue;
            cmd->BindPipeline(pso);

            frame.world = mi->worldTransform;

            // VS CB layout is shared between HD and SD_on_HD programs.
            if (auto vs = bls::ScopedCb<bls::HdVsCb>(rs_.gfx_.get(), rs_.blsHdVsCb_)) {
                bls::BuildHdVsCb(*vs, frame, mp);
            }
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 2, rs_.blsHdVsCb_);

            // PS CB layout diverges by program: hd_ps.slang reads PBR
            // fields directly (HdPsCb) while sd_on_hd_ps.slang reads a
            // padded legacy layout (SdOnHdPsCb) with invViewRow rows and
            // lightCountSlot.z bit-reinterpret.
            if (program == rs_.blsHdProgram_) {
                if (auto ps = bls::ScopedCb<bls::HdPsCb>(rs_.gfx_.get(), rs_.blsHdPsCb_)) {
                    bls::BuildHdPsCb(*ps, frame, mp);
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
                    bls::BuildSdOnHdPsCb(*ps, frame, mp);
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
                if (texId >= 0) {
                    auto it = mi->gpuTextures.find(texId);
                    if (it != mi->gpuTextures.end() && it->second.tex != gfx::TextureHandle::Invalid) {
                        cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, it->second.tex);
                        if (outWrap) *outWrap = it->second.wrapFlags & kWrapFlagsMask;
                        return true;
                    }
                }
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, fallback);
                return false;
            };

            uint32_t wrapFlags = kWrapFlagsMask;
            bindMaterialTex(0, layer.textureId,      rs_.defaultTex_,    &wrapFlags); // t0 albedo (white)
            bindMaterialTex(1, layer.normalMapId,    rs_.defaultNormal_, nullptr);    // t1 normal (flat)
            bindMaterialTex(2, layer.ormMapId,       rs_.defaultOrm_,    nullptr);    // t2 ORM (occlusion=1, roughness=1, metal=0, teamBlend=0)
            bindMaterialTex(3, layer.emissiveMapId,  rs_.defaultBlack_,  nullptr);    // t3 emissive (no glow)
            // t4 team colour: when the MDX layer authors a TeamColor
            // subtexture (or the model carries a replaceableId=1 slot at
            // any point), bind the live UI swatch — the engine itself
            // ignores whatever texture the .mdx references here and
            // swaps in the per-player tint at draw time. Without an
            // authored slot, fall back to black so ORM.w-driven blends
            // on unrelated materials stay neutral.
            if (layer.teamColorMapId >= 0 && rs_.teamColorTex_ != gfx::TextureHandle::Invalid) {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 4, rs_.teamColorTex_);
            } else {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 4, rs_.defaultBlack_);
            }
            cmd->BindSampler(gfx::ShaderStage::Pixel, 0, rs_.samplerWrap_[wrapFlags]);

            cmd->DrawIndexed(geo.indexCount);
        }
    }
};

bool RenderService::RenderGeosetsHd() {
    return GeosetPassHd{*this}.Run();
}

void RenderService::RenderGeosets() {
    // Route to HD-path when render mode is HD and the HD program loaded.
    // Otherwise fall through to the SD path (which itself falls through to
    // the legacy Slang path if its BLS program failed to load).
    if (renderMode_ == RenderMode::HD) {
        if (RenderGeosetsHd()) return;
        // HD program unavailable -- fall back to SD so we still render.
    }
    if (RenderGeosetsBls()) return;  // BLS path took over
    if (models_.empty()) return;

    auto* cmd = gfx_->GetImmediateContext();
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
    cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, cbPerFrame_);
    cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerLinear_);

    auto refs = render_detail::CollectSortedGeosetRefs(models_, ComputeSelectedLod());
    if (refs.empty()) return;

    Matrix44f view2;
    { std::lock_guard<std::mutex> lock(dataMutex_); view2 = camera_.GetViewMatrix(); }
    float aspect2 = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    Matrix44f proj2 = camera_.ProjectionRH(aspect2);

    for (auto& ref : refs) {
        auto* mi = ref.mi;
        auto& geo = mi->gpuGeosets[ref.idx];
        if (geo.vb == gfx::BufferHandle::Invalid || geo.ib == gfx::BufferHandle::Invalid || geo.indexCount == 0) continue;

        int matId = geo.materialId;
        GPUMaterial* mat = nullptr;
        if (matId >= 0 && matId < (int)mi->gpuMaterials.size())
            mat = &mi->gpuMaterials[matId];

        float geoAlpha = geo.geosetAlpha * mi->parentVisibility;
        if (geoAlpha < 0.01f) continue;
        Vector3f geoColor = geo.geosetColor;

        int numLayers = mat ? (int)mat->cpu.layers.size() : 0;
        if (numLayers <= 0) numLayers = 1;

        bool anyLayerThisPass = false;
        for (int li = 0; li < numLayers; ++li) {
            const render_detail::UnpackedLayer layer = render_detail::UnpackLayer(mat, li);

            if (!anyLayerThisPass) {
                render_detail::BindSdMeshGeometry(cmd, geo);
                anyLayerThisPass = true;
            }

            bool twoSided    = (layer.flags & MAT_TWO_SIDED) != 0;
            bool noDepthTest = (layer.flags & MAT_NO_DEPTH_TEST) != 0;
            bool noDepthSet  = (layer.flags & MAT_NO_DEPTH_SET) != 0;

            float combinedAlpha = geoAlpha * layer.alpha;
            if (combinedAlpha < 0.004f) continue;

            // For semi-transparent opaque/alpha-test layers, override to alpha-blend + noWrite
            int effectiveFilter = layer.filterMode;
            if (combinedAlpha < 0.99f && layer.filterMode <= FILTER_TRANSPARENT)
                effectiveFilter = FILTER_BLEND;

            cmd->BindPipeline(LookupMeshPSO(effectiveFilter, twoSided, noDepthTest, noDepthSet));

            float alphaRef = 0.0f;
            if (layer.filterMode == FILTER_TRANSPARENT) alphaRef = 0.75f;
            else if (layer.filterMode >= FILTER_MODULATE) alphaRef = 0.02f;

            // Texture animation (per-layer, per-model)
            float uOff=0, vOff=0, uTile=1, vTile=1, texRot=0;
            {
                int key = matId * 1000 + li;
                auto it = mi->matTexAnim.find(key);
                if (it != mi->matTexAnim.end()) {
                    uOff = it->second.uOff; vOff = it->second.vOff;
                    uTile = it->second.uTile; vTile = it->second.vTile;
                    texRot = it->second.rotation;
                }
            }

            {
                render_detail::CbPerFrameDesc d;
                d.world         = mi->worldTransform;
                d.view          = view2;
                d.projection    = proj2;
                d.lightDir      = render_detail::NormalizedLightDir4(kDefaultLightDir);
                d.lightColor    = kGeosetLightColor;
                d.ambientColor  = {kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z, alphaRef};
                d.extraParams   = {combinedAlpha, geoColor.x, geoColor.y, geoColor.z};
                d.texAnimParams = {uOff, vOff, uTile, vTile};
                d.materialFlags = {
                    (layer.flags & MAT_UNSHADED)       ? 1.0f : 0.0f,
                    (layer.flags & MAT_CONSTANT_COLOR) ? 1.0f : 0.0f,
                    texRot, 0.0f
                };
                render_detail::WriteCbPerFrame(gfx_.get(), cbPerFrame_, d);
            }

            render_detail::BindLayerAlbedo(cmd, *mi, layer.textureId,
                                           defaultTex_, samplerWrap_);

            cmd->DrawIndexed(geo.indexCount, 0, 0);
        }
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
        camera_.SetYaw(Camera::kDefaultYaw);
        camera_.SetPitch(n.z > 0 ? kTopBottomPitch : -kTopBottomPitch);
    } else {
        camera_.SetYaw(std::atan2(n.y, n.x));
        camera_.SetPitch(0.0f);
    }
}

} // namespace WhiteoutDex
