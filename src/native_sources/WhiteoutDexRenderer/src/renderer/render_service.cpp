// ============================================================================
// WhiteoutDex Real-Time Renderer — Render Service Implementation
// ============================================================================

#include "render_service.h"
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

RenderService::RenderService() {
    activeContentProvider_ = &contentProvider_;
    StartTemplateLoader();
}
RenderService::~RenderService() { StopTemplateLoader(); }

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
            if (auto* bp = (bls::BonePaletteCb*)gfx_->MapBuffer(mi->bonePaletteCb)) {
                bls::BuildBonePalette(*bp, mi->skinning.OffsetMatrices(),
                                      mi->skinning.NodeCount());
                gfx_->UnmapBuffer(mi->bonePaletteCb);
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
    frame.projection = Matrix44f::perspective_fov_rh(
        std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f);
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
        bls::PsoRequest req{};
        req.program   = blsSdProgram_;
        req.vsIndex   = perm.vs;
        req.psIndex   = perm.ps;
        req.material  = mp;
        req.layout    = bls::VertexLayoutKind::ParticleSD;
        req.topology  = gfx::PrimitiveTopology::TriangleList;
        req.rtvFormat = gfx::Format::R8G8B8A8_UNORM;
        req.dsvFormat = gfx::Format::D24_UNORM_S8_UINT;
        auto pso = blsPsoBuilder_->GetOrBuild(req);
        if (pso == gfx::PipelineHandle::Invalid) { drawOffset += dl.vertexCount; continue; }
        cmd->BindPipeline(pso);

        // VS CB (208 B fixed + 64 B per light; unlit → 208 B).
        // PS CB (48 B: alphaRef + fog).
        if (auto* vs = (bls::SdVsCbA*)gfx_->MapBuffer(blsSdVsCb_)) {
            bls::BuildSdVsCbA(*vs, frame, mp);
            gfx_->UnmapBuffer(blsSdVsCb_);
        }
        if (auto* ps = (bls::SdPsCbA*)gfx_->MapBuffer(blsSdPsCb_)) {
            bls::BuildSdPsCbA(*ps, frame, mp);
            gfx_->UnmapBuffer(blsSdPsCb_);
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
            CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
            cb->world      = Matrix44f::identity().transpose();
            const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            Matrix44f proj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f);
            cb->view       = viewMat.transpose();
            cb->projection = proj.transpose();
            Vector3f ldN = Vector3f{kDefaultLightDir.x, kDefaultLightDir.y, kDefaultLightDir.z}.normalized();
            cb->lightDir      = {ldN.x, ldN.y, ldN.z, 0.0f};
            cb->lightColor    = kParticleLightColor;
            cb->ambientColor  = {kParticleAmbientBase.x, kParticleAmbientBase.y, kParticleAmbientBase.z, alphaRef};
            cb->extraParams   = {1.0f, 1.0f, 1.0f, 1.0f};
            cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
            cb->materialFlags = {dl.material.unshaded ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            gfx_->UnmapBuffer(cbPerFrame_);
        }
        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
        cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, cbPerFrame_);

        // Texture lookup — textures are per-ModelInstance.
        uint32_t wrapFlags  = kWrapFlagsMask;
        bool     hasModelTex = false;
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            ModelInstance* owner = getModel(dl.model);
            if (owner && dl.material.textureId >= 0) {
                auto it = owner->gpuTextures.find(dl.material.textureId);
                if (it != owner->gpuTextures.end() && it->second.tex != gfx::TextureHandle::Invalid) {
                    cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, it->second.tex);
                    wrapFlags  = it->second.wrapFlags & kWrapFlagsMask;
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
            CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
            cb->world      = Matrix44f::identity().transpose();
            float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            Matrix44f proj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f);
            cb->view       = viewMat.transpose();
            cb->projection = proj.transpose();
            Vector3f ldN = Vector3f{kDefaultLightDir.x, kDefaultLightDir.y, kDefaultLightDir.z}.normalized();
            cb->lightDir = {ldN.x, ldN.y, ldN.z, 0.0f};
            cb->lightColor   = kParticleLightColor;
            cb->ambientColor = {kParticleAmbientBase.x, kParticleAmbientBase.y, kParticleAmbientBase.z, alphaRef};
            cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
            cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
            cb->materialFlags = {cfg.unshaded ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            gfx_->UnmapBuffer(cbPerFrame_);
        }

        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
        cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, cbPerFrame_);

        // Bind texture
        uint32_t wrapFlags = kWrapFlagsMask;
        bool hasModelTex = false;
        if (cfg.textureId >= 0 && mi->gpuTextures.count(cfg.textureId)) {
            auto& gt = mi->gpuTextures[cfg.textureId];
            if (gt.tex != gfx::TextureHandle::Invalid) {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, gt.tex);
                wrapFlags = gt.wrapFlags & kWrapFlagsMask;
                hasModelTex = true;
            }
        }
        if (!hasModelTex)
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, defaultTex_);
        cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerWrap_[wrapFlags]);

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
// Collision Shape Wireframe Rendering
// ============================================================================

void RenderService::RenderCollisions() {
    std::vector<CollisionShape> shapes;
    Matrix44f viewMat;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (auto& [h, mi] : models_) {
            if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
            shapes.insert(shapes.end(), mi->collisionShapes.begin(), mi->collisionShapes.end());
        }
        if (shapes.empty()) return;
        viewMat = camera_.GetViewMatrix();
    }

    auto* cmd = gfx_->GetImmediateContext();
    cmd->BindPipeline(linePSO_);

    // Line color: green for collision shapes
    Vector4f col = {0.0f, 1.0f, 0.3f, 1.0f};

    for (auto& cs : shapes) {
        // Build line vertices in local space, then transform
        struct LV { Vector3f pos; Vector4f col; };
        std::vector<LV> lines;

        // Previewd COLLIDE_TYPE (from IDA RE of MDL::ReadBinCollisions
        // @0x1407810f0 / AddCollisionGeometryGeoset @0x1403155b0):
        //   0 = Box (2 vec3 extents), 1 = Cylinder (2 vec3 endpoints + radius),
        //   2 = Sphere (vec3 center + radius), 3 = Plane (2 floats width/height).
        // WhiteoutLib stores the raw u32 in cs.type — labels in its enum
        // are mis-named but the numeric values match the file.
        // Previewd geoset layout: corners stored at `pivot + extent` in bind-pose
        // world space; the bone matrix is a skinning delta (identity at bind).
        // Reproduce that by adding pivot here before applying cs.transform.
        const Vector3f& piv = cs.pivot;
        auto pushLine = [&](const Vector3f& a, const Vector3f& b) {
            Vector3f ap = {a.x + piv.x, a.y + piv.y, a.z + piv.z};
            Vector3f bp = {b.x + piv.x, b.y + piv.y, b.z + piv.z};
            Vector3f pa = whiteout::transform_point(ap, cs.transform);
            Vector3f pb = whiteout::transform_point(bp, cs.transform);
            lines.push_back({pa, col});
            lines.push_back({pb, col});
        };
        auto emitCircle = [&](const Vector3f& c, float r, int axis) {
            const int segs = 24;
            for (int i = 0; i < segs; i++) {
                float a0 = (float)i / segs * 6.28318530f;
                float a1 = (float)(i+1) / segs * 6.28318530f;
                float c0 = r * cosf(a0), s0 = r * sinf(a0);
                float c1 = r * cosf(a1), s1 = r * sinf(a1);
                Vector3f p0, p1;
                if (axis == 2)      { p0 = {c.x+c0, c.y+s0, c.z}; p1 = {c.x+c1, c.y+s1, c.z}; }
                else if (axis == 1) { p0 = {c.x+c0, c.y, c.z+s0}; p1 = {c.x+c1, c.y, c.z+s1}; }
                else                { p0 = {c.x, c.y+c0, c.z+s0}; p1 = {c.x, c.y+c1, c.z+s1}; }
                pushLine(p0, p1);
            }
        };

        if (cs.type == 0) {
            // Box: 12 edges between (vmin, vmax)
            Vector3f mn = cs.vmin, mx = cs.vmax;
            Vector3f corners[8] = {
                {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z},
                {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z}
            };
            int edges[24] = {0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7};
            for (int i = 0; i < 24; i += 2)
                pushLine(corners[edges[i]], corners[edges[i+1]]);
        } else if (cs.type == 2) {
            // Sphere: center = vertices[0] (stored in vmin), radius
            emitCircle(cs.vmin, cs.radius, 0);
            emitCircle(cs.vmin, cs.radius, 1);
            emitCircle(cs.vmin, cs.radius, 2);
        } else if (cs.type == 1) {
            // Cylinder: two endpoints (vmin, vmax) with radius.
            // Draw end caps + 4 connecting lines along the axis.
            Vector3f axisVec = { cs.vmax.x - cs.vmin.x, cs.vmax.y - cs.vmin.y, cs.vmax.z - cs.vmin.z };
            float axisLen = std::sqrt(axisVec.x*axisVec.x + axisVec.y*axisVec.y + axisVec.z*axisVec.z);
            Vector3f axis = (axisLen > 1e-5f)
                ? Vector3f{axisVec.x/axisLen, axisVec.y/axisLen, axisVec.z/axisLen}
                : Vector3f{0, 0, 1};
            Vector3f tmp = (std::abs(axis.z) < 0.9f) ? Vector3f{0,0,1} : Vector3f{1,0,0};
            Vector3f u = {
                axis.y*tmp.z - axis.z*tmp.y,
                axis.z*tmp.x - axis.x*tmp.z,
                axis.x*tmp.y - axis.y*tmp.x };
            float uLen = std::sqrt(u.x*u.x + u.y*u.y + u.z*u.z);
            if (uLen > 1e-5f) { u.x/=uLen; u.y/=uLen; u.z/=uLen; }
            Vector3f v = {
                axis.y*u.z - axis.z*u.y,
                axis.z*u.x - axis.x*u.z,
                axis.x*u.y - axis.y*u.x };
            const int segs = 24;
            auto ringPt = [&](const Vector3f& c, float a) {
                float cs_ = cs.radius * cosf(a), sn_ = cs.radius * sinf(a);
                return Vector3f{ c.x + u.x*cs_ + v.x*sn_,
                                 c.y + u.y*cs_ + v.y*sn_,
                                 c.z + u.z*cs_ + v.z*sn_ };
            };
            for (int i = 0; i < segs; i++) {
                float a0 = (float)i / segs * 6.28318530f;
                float a1 = (float)(i+1) / segs * 6.28318530f;
                pushLine(ringPt(cs.vmin, a0), ringPt(cs.vmin, a1));
                pushLine(ringPt(cs.vmax, a0), ringPt(cs.vmax, a1));
            }
            for (int i = 0; i < 4; i++) {
                float a = (float)i / 4 * 6.28318530f;
                pushLine(ringPt(cs.vmin, a), ringPt(cs.vmax, a));
            }
        } else if (cs.type == 3) {
            // Plane: vmin.x / vmin.y = half-extents in local XY around pivot.
            float hw = cs.vmin.x, hh = cs.vmin.y;
            Vector3f p0 = {-hw, -hh, 0}, p1 = {hw, -hh, 0};
            Vector3f p2 = {hw, hh, 0},   p3 = {-hw, hh, 0};
            pushLine(p0, p1); pushLine(p1, p2); pushLine(p2, p3); pushLine(p3, p0);
        }

        if (lines.empty()) continue;

        // Upload to a temp dynamic buffer
        gfx::BufferDesc bd;
        bd.size  = (uint32_t)(sizeof(LV) * lines.size());
        bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
        gfx::BufferHandle tempVB = gfx_->CreateBuffer(bd);
        if (tempVB == gfx::BufferHandle::Invalid) continue;

        void* mapped = gfx_->MapBuffer(tempVB);
        if (!mapped) { gfx_->Destroy(tempVB); continue; }
        memcpy(mapped, lines.data(), sizeof(LV) * lines.size());
        gfx_->UnmapBuffer(tempVB);

        // Update CB with identity world
        {
            CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
            cb->world = Matrix44f::identity().transpose();
            float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            cb->view = viewMat.transpose();
            cb->projection = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f).transpose();
            cb->lightDir = {0,0,0,0};
            cb->lightColor = kCollisionLightColor;
            cb->ambientColor = kCollisionAmbientColor;
            cb->extraParams = {1,1,1,1};
            cb->texAnimParams = {0,0,1,1};
            cb->materialFlags = {0,0,0,0};
            gfx_->UnmapBuffer(cbPerFrame_);
        }

        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);

        cmd->BindVertexBuffer(0, tempVB, sizeof(LV));
        cmd->Draw((uint32_t)lines.size(), 0);
        gfx_->Destroy(tempVB);
    }
}

// ============================================================================
// Light markers (debug: one wireframe sphere per evaluated light)
// ============================================================================
//
// Shows where `FrameState::LightState` ends up in world space AFTER the
// adapter's node-world resolution. We draw the EXACT `L.worldPos` (omni)
// or a short ray from origin along `-L.worldDir` (directional), tinted by
// the light's own diffuse colour so markers line up with the shading
// each light produces in-scene.
void RenderService::RenderLightMarkers() {
    struct MarkerLight {
        Vector3f worldPos;
        Vector3f worldDir;
        Vector3f diffuse;
        bool     isDirectional;
        bool     enabled;
    };
    std::vector<MarkerLight> lights;
    int totalAuthored = 0;
    Matrix44f viewMat;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (auto& [h, mi] : models_) {
            if (mi->parentVisibility <= 0.02f) continue;
            totalAuthored += (int)mi->activeLights.size();
            for (const auto& L : mi->activeLights) {
                const bool dir = (L.kind == FrameState::LightKind::Directional);
                lights.push_back({
                    dir ? whiteout::transform_point(Vector3f{0,0,0}, mi->worldTransform)
                        : L.worldPos,
                    L.worldDir,
                    L.diffuse,
                    dir,
                    L.enabled
                });
            }
        }
        if (lights.empty()) return;
        viewMat = camera_.GetViewMatrix();
    }

    auto* cmd = gfx_->GetImmediateContext();
    cmd->BindPipeline(linePSO_);

    struct LV { Vector3f pos; Vector4f col; };
    std::vector<LV> verts;
    verts.reserve(lights.size() * 3 * 24 * 2 + lights.size() * 2);

    const float kMarkerRadius = 20.0f;  // world units -- tuned for typical WC3 scale
    const int   kSegs         = 24;
    for (const auto& m : lights) {
        // Enabled lights: bright + tinted by their diffuse (so each marker
        // matches the colour it should cast). Disabled (KLAV-gated) lights:
        // dim gray so you can still see they exist and where, but can't
        // confuse them with a contributing light.
        Vector4f col = m.enabled
            ? Vector4f{ std::max(m.diffuse.x, 0.2f),
                        std::max(m.diffuse.y, 0.2f),
                        std::max(m.diffuse.z, 0.2f),
                        1.0f }
            : Vector4f{ 0.25f, 0.25f, 0.25f, 1.0f };

        // 3 great circles around the light centre -- instantly readable as a sphere.
        const Vector3f c = m.worldPos;
        for (int plane = 0; plane < 3; ++plane) {
            for (int i = 0; i < kSegs; ++i) {
                float a0 = (float)i       / kSegs * 6.28318530f;
                float a1 = (float)(i + 1) / kSegs * 6.28318530f;
                float c0 = kMarkerRadius * std::cos(a0), s0 = kMarkerRadius * std::sin(a0);
                float c1 = kMarkerRadius * std::cos(a1), s1 = kMarkerRadius * std::sin(a1);
                Vector3f p0, p1;
                if      (plane == 0) { p0 = {c.x + c0, c.y + s0, c.z      }; p1 = {c.x + c1, c.y + s1, c.z      }; }
                else if (plane == 1) { p0 = {c.x + c0, c.y,      c.z + s0 }; p1 = {c.x + c1, c.y,      c.z + s1 }; }
                else                 { p0 = {c.x,      c.y + c0, c.z + s0 }; p1 = {c.x,      c.y + c1, c.z + s1 }; }
                verts.push_back({p0, col});
                verts.push_back({p1, col});
            }
        }

        // Directional: add a ray from the centre along -worldDir (i.e. toward the
        // source), length = 4x the marker radius, so you can read its orientation.
        if (m.isDirectional) {
            Vector3f d = m.worldDir;
            float    n = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
            if (n > 1e-6f) { d = { d.x/n, d.y/n, d.z/n }; } else { d = {0,0,1}; }
            const float L = kMarkerRadius * 4.0f;
            Vector3f tip = { c.x - d.x * L, c.y - d.y * L, c.z - d.z * L };
            verts.push_back({c,   col});
            verts.push_back({tip, col});
        }
    }

    if (verts.empty()) return;

    gfx::BufferDesc bd;
    bd.size  = (uint32_t)(sizeof(LV) * verts.size());
    bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
    gfx::BufferHandle tempVB = gfx_->CreateBuffer(bd);
    if (tempVB == gfx::BufferHandle::Invalid) return;

    void* mapped = gfx_->MapBuffer(tempVB);
    if (!mapped) { gfx_->Destroy(tempVB); return; }
    std::memcpy(mapped, verts.data(), sizeof(LV) * verts.size());
    gfx_->UnmapBuffer(tempVB);

    {
        CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
        cb->world       = Matrix44f::identity().transpose();
        float aspect    = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
        cb->view        = viewMat.transpose();
        cb->projection  = Matrix44f::perspective_fov_rh(
            std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f).transpose();
        cb->lightDir     = {0,0,0,0};
        cb->lightColor   = {1,1,1,1};
        cb->ambientColor = {1,1,1,0};
        cb->extraParams  = {1,1,1,1};
        cb->texAnimParams = {0,0,1,1};
        cb->materialFlags = {0,0,0,0};
        gfx_->UnmapBuffer(cbPerFrame_);
    }
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
    cmd->BindVertexBuffer(0, tempVB, sizeof(LV));
    cmd->Draw((uint32_t)verts.size(), 0);
    gfx_->Destroy(tempVB);
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

        // Grid + ViewCube
        gfx_->Destroy(gridVB_);
        gfx_->Destroy(vcCubeVB_);  gfx_->Destroy(vcCubeIB_);
        gfx_->Destroy(vcOutlineVB_); gfx_->Destroy(vcFaceTex_);
        gfx_->Destroy(vcHomeVB_);

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
    std::vector<LineVertex> lines;
    const float extent = 500.0f;
    const float step   = 50.0f;
    Vector4f gridColor  = {0.45f, 0.45f, 0.46f, 1.0f};  // subtle, close to background
    Vector4f axisColorX = {0.75f, 0.2f,  0.2f,  1.0f};
    Vector4f axisColorY = {0.2f,  0.75f, 0.2f,  1.0f};
    Vector4f axisColorZ = {0.2f,  0.2f,  0.75f, 1.0f};

    for (float v = -extent; v <= extent; v += step) {
        Vector4f c = (v == 0.0f) ? axisColorY : gridColor;
        lines.push_back({{v, -extent, 0.0f}, c});
        lines.push_back({{v,  extent, 0.0f}, c});
        c = (v == 0.0f) ? axisColorX : gridColor;
        lines.push_back({{-extent, v, 0.0f}, c});
        lines.push_back({{ extent, v, 0.0f}, c});
    }
    lines.push_back({{0.0f, 0.0f, 0.0f},   axisColorZ});
    lines.push_back({{0.0f, 0.0f, extent},  axisColorZ});
    gridVertCount_ = (int)lines.size();

    gridVB_ = gfx_->CreateBuffer({
        .size  = sizeof(LineVertex) * lines.size(),
        .usage = gfx::BufferUsage::Vertex,
    }, lines.data());

    CreateViewCube();
    return true;
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
    proj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f);

    // Update constant buffer (via GFX MapBuffer)
    {
        CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
        cb->world      = Matrix44f::identity().transpose();
        cb->view       = view.transpose();
        cb->projection = proj.transpose();
        Vector3f ldN = Vector3f{kDefaultLightDir.x, kDefaultLightDir.y, kDefaultLightDir.z}.normalized();
        cb->lightDir = {ldN.x, ldN.y, ldN.z, 0.0f};
        cb->lightColor   = kGeosetLightColor;
        cb->ambientColor = {kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z, 0.0f};  // .a=0 no alpha test
        cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};     // .x=geosetAlpha (1=visible)
        cb->materialFlags = {0.0f, 0.0f, 0.0f, 0.0f};
        gfx_->UnmapBuffer(cbPerFrame_);
    }

    if (showGrid_) RenderGrid();
    RenderGeosets();
    if (showParticles_) RenderParticles();
    if (showRibbons_) RenderRibbons();
    if (showCollisions_) RenderCollisions();
    if (showLights_)     RenderLightMarkers();
    RenderViewCube();
}

void RenderService::Present(RenderTargetId targetId) {
    auto it = targets_.find(targetId);
    if (it != targets_.end() && it->second.swap != gfx::SwapChainHandle::Invalid)
        gfx_->Present(it->second.swap);
}

void RenderService::RenderGrid() {
    if (gridVB_ == gfx::BufferHandle::Invalid) return;
    auto* cmd = gfx_->GetImmediateContext();
    cmd->BindPipeline(linePSO_);
    cmd->BindVertexBuffer(0, gridVB_, sizeof(LineVertex));
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
    cmd->Draw(gridVertCount_, 0);
}

// ============================================================================
// Geoset Rendering (4-bucket sort by FilterMode, like Magos Model::Render)
// ============================================================================

bool RenderService::RenderGeosetsBls() {
    // Replace the Slang mesh shader with Blizzard's SD VS + SD PS. Skinning
    // stays on the compute pass for now -- we draw from geo.vb which the
    // compute shader writes skinned vertices into, so using weightIndex=0
    // (no VS-side skinning) produces correct animated output.
    if (!blsSdProgram_ || !blsPsoBuilder_) return false;
    if (models_.empty()) return true;

    auto* cmd = gfx_->GetImmediateContext();

    struct GeosetRef {
        ModelInstance* mi;
        int idx;
        int renderOrder, priorityPlane, geosetId;
    };
    const int selectedLod = ComputeSelectedLod();
    std::vector<GeosetRef> refs;
    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (mi->parentVisibility <= 0.02f) continue;
        const int modelLod = mi->hasLods ? selectedLod : 0;
        for (int i = 0; i < (int)mi->gpuGeosets.size(); i++) {
            auto& geo = mi->gpuGeosets[i];
            if (!GeosetPassesLod(geo.lod, modelLod)) continue;
            int ro = 1;
            int matId = geo.materialId;
            if (matId >= 0 && matId < (int)mi->gpuMaterials.size() && !mi->gpuMaterials[matId].cpu.layers.empty())
                ro = GetRenderOrder(mi->gpuMaterials[matId].cpu.layers[0].filterMode);
            refs.push_back({mi, i, ro, geo.priorityPlane, geo.geosetId});
        }
    }
    if (refs.empty()) return true;

    std::sort(refs.begin(), refs.end(), [](const GeosetRef& a, const GeosetRef& b) {
        if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
        if (a.priorityPlane != b.priorityPlane) return a.priorityPlane < b.priorityPlane;
        return a.geosetId < b.geosetId;
    });

    Matrix44f view2;
    { std::lock_guard<std::mutex> lock(dataMutex_); view2 = camera_.GetViewMatrix(); }
    const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    const Matrix44f proj = Matrix44f::perspective_fov_rh(
        std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f);

    bls::FrameInputs frame;
    frame.view       = view2;
    frame.projection = proj;
    frame.effectTime = animationTimeMs_.load() * 0.001f;
    frame.numLights  = 0;
    frame.viewportRect = { (float)width_, (float)height_, 0.0f, 0.0f };

    cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerLinear_);

    // Fallback "baseline" directional so MDX models without authored
    // lights still look lit. Camera-attached headlight: light ray
    // goes along view-forward, so `direction TO source` is constant
    // in view space (camera at origin). RH view has forward = -Z, so
    // from any fragment in front of the camera, the light source
    // sits along +Z. No per-camera-pose transform needed.
    const auto kBaselineDirToSourceVS = Vector3f{0.0f, 0.0f, 1.0f};
    const auto kBaselineDiffuse       = Vector3f{kGeosetLightColor.x, kGeosetLightColor.y, kGeosetLightColor.z};
    const auto kBaselineAmbient       = Vector3f{kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z};

    for (auto& ref : refs) {
        auto* mi  = ref.mi;
        auto& geo = mi->gpuGeosets[ref.idx];
        if (geo.vb == gfx::BufferHandle::Invalid || geo.ib == gfx::BufferHandle::Invalid || geo.indexCount == 0)
            continue;

        int matId = geo.materialId;
        GPUMaterial* mat = nullptr;
        if (matId >= 0 && matId < (int)mi->gpuMaterials.size()) mat = &mi->gpuMaterials[matId];

        float geoAlpha = geo.geosetAlpha * mi->parentVisibility;
        if (geoAlpha < 0.01f) continue;

        int numLayers = mat ? (int)mat->cpu.layers.size() : 0;
        if (numLayers <= 0) numLayers = 1;

        cmd->BindVertexBuffer(0, geo.vb, sizeof(Vertex));
        cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);

        // --- Build per-geoset ShaderLight palette (up to 8) --------------
        // Use the hardcoded baseline key light ONLY when the model has no
        // authored MDX lights. When lights are present, the shader's
        // saturate(diffAccum + ambAccum) clamp turns an extra always-on key
        // light into oversaturation (the symptom: bright blue flood across
        // the whole scene). Previewd does not add a fallback when the model
        // ships its own lights, so we mirror that.
        int lightCountForGeoset = 0;
        bool anyEnabled = false;
        for (const auto& L : mi->activeLights) { if (L.enabled) { anyEnabled = true; break; } }
        if (!anyEnabled) {
            bls::ShaderLight& sl = frame.lights[lightCountForGeoset++];
            sl.ambient  = { kBaselineAmbient.x, kBaselineAmbient.y, kBaselineAmbient.z, 0.0f };
            sl.diffuse  = { kBaselineDiffuse.x, kBaselineDiffuse.y, kBaselineDiffuse.z, 0.0f };
            sl.position = { kBaselineDirToSourceVS.x, kBaselineDirToSourceVS.y, kBaselineDirToSourceVS.z, 0.0f };
        }
        for (const auto& L : mi->activeLights) {
            if (!L.enabled) continue;
            if (lightCountForGeoset >= bls::kMaxLights) break;
            bls::ShaderLight& sl = frame.lights[lightCountForGeoset++];
            sl.ambient = { L.ambient.x, L.ambient.y, L.ambient.z, 0.0f };
            sl.diffuse = { L.diffuse.x, L.diffuse.y, L.diffuse.z, 0.0f };
            if (L.kind == FrameState::LightKind::Directional) {
                // Engine stores -normalize(m_dir) as the shader light vector.
                Vector3f d = L.worldDir;
                float    n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (n > 1e-6f) { d.x /= n; d.y /= n; d.z /= n; }
                Vector3f lv = whiteout::transform_normal(Vector3f{-d.x, -d.y, -d.z}, view2);
                sl.position = { lv.x, lv.y, lv.z, 0.0f };   // type=0
            } else {   // Omni or Ambient -- place at world position
                Vector3f p = whiteout::transform_point(L.worldPos, view2);
                sl.position = { p.x, p.y, p.z, 1.0f };     // type=1 (omni)
            }
        }
        for (int i = lightCountForGeoset; i < bls::kMaxLights; ++i) frame.lights[i] = {};

        for (int li = 0; li < numLayers; ++li) {
            int   layerFilter = FILTER_NONE;
            int   layerFlags  = 0;
            float layerAlpha  = 1.0f;
            int   layerTexId  = -1;
            int   texAnimId   = -1;
            if (mat && li < (int)mat->cpu.layers.size()) {
                const auto& L = mat->cpu.layers[li];
                layerFilter = L.filterMode;
                layerFlags  = L.flags;
                layerAlpha  = L.alpha;
                layerTexId  = L.textureId;
                texAnimId   = L.textureAnimationId;
            }

            // Resolve the per-layer UV matrix: palette lookup, or identity.
            if (texAnimId >= 0 && texAnimId < (int)mi->texAnimPalette.size()) {
                const auto& e = mi->texAnimPalette[texAnimId];
                frame.texMtx0.rows[0] = { e.row0[0], e.row0[1], e.row0[2], e.row0[3] };
                frame.texMtx0.rows[1] = { e.row1[0], e.row1[1], e.row1[2], e.row1[3] };
            } else {
                frame.texMtx0 = bls::IdentityTexMtx();
            }
            frame.texMtx1 = bls::IdentityTexMtx();

            float combinedAlpha = geoAlpha * layerAlpha;
            if (combinedAlpha < 0.004f) continue;
            int effectiveFilter = layerFilter;
            if (combinedAlpha < 0.99f && layerFilter <= FILTER_TRANSPARENT)
                effectiveFilter = FILTER_BLEND;

            bls::MatParams mp = bls::FromMdxLayer(effectiveFilter, layerFlags, bls::GxShaderID::SD);
            // SelectModelMaterial mirror: diffuseColor = geoColor * combinedAlpha.
            // Modulate takes the alpha-into-RGB trick; our geo color stays
            // white for now since per-vertex color already carries the tint.
            if (mp.alpha == bls::GxMatAlpha::Modulate) {
                mp.diffuseColor = {combinedAlpha, 1, 1, 1};
            } else {
                mp.diffuseColor = {geo.geosetColor.x, geo.geosetColor.y, geo.geosetColor.z, combinedAlpha};
            }

            // Unshaded layers (MAT_UNSHADED / kDisableLighting) skip the
            // lighting loop in the VS. Permute count must agree with what
            // we upload: numLights=0 picks the "no lights" VS, otherwise
            // we use all N lights in frame.lights[].
            const bool  unlit    = (mp.disables & bls::kDisableLighting) != 0;
            const int   activeN  = unlit ? 0 : lightCountForGeoset;
            frame.numLights = activeN;

            bls::RenderState rs;
            rs.shaderId       = bls::GxShaderID::SD;
            rs.alphaMode      = static_cast<uint8_t>(mp.alpha);
            rs.numColors      = 1;   // Vertex has per-vertex color set to white
            rs.numTexCoords   = 1;
            rs.numWeights     = 0;   // compute pass already baked skinning into geo.vb
            rs.numLights      = static_cast<uint8_t>(activeN);
            rs.fogEnabled     = false;
            rs.depthWrite     = mp.DepthWriteEnabled();
            rs.lightingEnabled= !unlit && activeN > 0;
            auto perm = bls::SelectPermutes(rs);

            bls::PsoRequest req{};
            req.program   = blsSdProgram_;
            req.vsIndex   = perm.vs;
            req.psIndex   = perm.ps;
            req.material  = mp;
            req.layout    = bls::VertexLayoutKind::ParticleSD;  // reuse our 48B Vertex
            req.topology  = gfx::PrimitiveTopology::TriangleList;
            req.rtvFormat = gfx::Format::R8G8B8A8_UNORM;
            req.dsvFormat = gfx::Format::D24_UNORM_S8_UINT;
            auto pso = blsPsoBuilder_->GetOrBuild(req);
            if (pso == gfx::PipelineHandle::Invalid) continue;
            cmd->BindPipeline(pso);

            frame.world = mi->worldTransform;

            if (auto* vs = (bls::SdVsCbA*)gfx_->MapBuffer(blsSdVsCb_)) {
                bls::BuildSdVsCbA(*vs, frame, mp);
                gfx_->UnmapBuffer(blsSdVsCb_);
            }
            if (auto* ps = (bls::SdPsCbA*)gfx_->MapBuffer(blsSdPsCb_)) {
                bls::BuildSdPsCbA(*ps, frame, mp);
                gfx_->UnmapBuffer(blsSdPsCb_);
            }
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, blsSdVsCb_);
            cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, blsSdPsCb_);

            uint32_t wrapFlags = kWrapFlagsMask;
            bool     hasTex    = false;
            if (layerTexId >= 0 && mi->gpuTextures.count(layerTexId)) {
                auto& gt = mi->gpuTextures[layerTexId];
                if (gt.tex != gfx::TextureHandle::Invalid) {
                    cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, gt.tex);
                    wrapFlags = gt.wrapFlags & kWrapFlagsMask;
                    hasTex    = true;
                }
            }
            if (!hasTex) cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, defaultTex_);
            cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerWrap_[wrapFlags]);

            cmd->DrawIndexed(geo.indexCount);
        }
    }
    return true;
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
bool RenderService::RenderGeosetsHd() {
    if (!blsHdProgram_ || !blsPsoBuilder_) return false;
    if (models_.empty()) return true;

    // Ensure the team-colour swatch matches the current UI picker. Cheap
    // (no-op when colour is unchanged) and safe on the render thread.
    UpdateTeamColorSwatch();

    auto* cmd = gfx_->GetImmediateContext();

    struct GeosetRef {
        ModelInstance* mi;
        int idx;
        int renderOrder, priorityPlane, geosetId;
    };
    const int selectedLod = ComputeSelectedLod();
    std::vector<GeosetRef> refs;
    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (mi->parentVisibility <= 0.02f) continue;
        const int modelLod = mi->hasLods ? selectedLod : 0;
        for (int i = 0; i < (int)mi->gpuGeosets.size(); i++) {
            auto& geo = mi->gpuGeosets[i];
            if (!GeosetPassesLod(geo.lod, modelLod)) continue;
            int ro = 1;
            int matId = geo.materialId;
            if (matId >= 0 && matId < (int)mi->gpuMaterials.size() && !mi->gpuMaterials[matId].cpu.layers.empty())
                ro = GetRenderOrder(mi->gpuMaterials[matId].cpu.layers[0].filterMode);
            refs.push_back({mi, i, ro, geo.priorityPlane, geo.geosetId});
        }
    }
    if (refs.empty()) return true;

    std::sort(refs.begin(), refs.end(), [](const GeosetRef& a, const GeosetRef& b) {
        if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
        if (a.priorityPlane != b.priorityPlane) return a.priorityPlane < b.priorityPlane;
        return a.geosetId < b.geosetId;
    });

    // HD / SD_on_HD require the LH view + diagonal-FOV projection —
    // see docs/CAMERA_PLAN.md.
    const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    Matrix44f view2, proj;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        view2 = camera_.ViewLH();
        proj  = camera_.ProjectionLH(aspect);
    }

    bls::FrameInputs frame;
    frame.view       = view2;
    frame.projection = proj;
    frame.effectTime = animationTimeMs_.load() * 0.001f;
    frame.numLights  = 0;
    frame.viewportRect = { (float)width_, (float)height_, 0.0f, 0.0f };

    // Day at t13, Night at t14. envTransitionT=0 picks day; once we wire
    // day-night cycle state we'll lerp this. envMipEnd is the highest
    // valid mip index of each probe's chain -- the shader's
    // roughness -> mip remap clamps against these, and a non-zero product
    // also keeps sampleIBL off the "probe disabled" fast-out path.
    //
    // Sweeping envTransitionT across [0, 1] during testing confirmed the
    // shader's scalar day/night blend works as expected; the "one half
    // brighter than the other" look that appeared once we swapped the
    // procedural grey probe for real Lordaeron DDS comes from each
    // probe being independently asymmetric (sun on one horizon, shadow
    // on the other), which every blend between two asymmetric probes
    // preserves. Mirrored normals on a symmetric character sample
    // opposite faces of the cube, so the two halves legitimately reflect
    // different content. A symmetric (averaged) probe or runtime yaw
    // alignment would flatten the seam but would no longer be the
    // scene's actual lighting.
    frame.envFromMipEnd  = iblProbeMipEnd_;
    frame.envToMipEnd    = iblProbeMipEnd_;
    frame.envTransitionT = 0.75f;

    cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerLinear_);

    // HD PS perms declare (t0 albedo, t1 normal, t3 emissive) and the
    // HAS_IBL perms additionally declare (t13/t14 env cube arrays, t15
    // split-sum LUT). Every declared slot needs a bound resource or we
    // hit validation errors on DX12 / black reads on DX11. defaultTex_
    // is a 1x1 white Texture2D -- neutral enough for albedo/normal/emissive
    // fallbacks; env slots get the procedural grey probe, LUT gets the
    // real split-sum.
    // Dynamic sampler table covers s0..s3 (kSamplersPerStage=4). Bind
    // linear to every slot; per-layer BindSampler(0, ...) overrides s0
    // with the wrap-appropriate variant at draw time. s13..s15 are static
    // samplers baked into the root signature (see d3d12_device.cpp root
    // signature setup).
    cmd->BindSampler(gfx::ShaderStage::Pixel, 1, samplerLinear_);
    cmd->BindSampler(gfx::ShaderStage::Pixel, 2, samplerLinear_);
    cmd->BindSampler(gfx::ShaderStage::Pixel, 3, samplerLinear_);

    // Samplers at s13/s14/s15 are STATIC in the root signature (linear /
    // clamp, baked at root-sig creation time) so we only need to bind the
    // texture SRVs here. Keeping them off the dynamic sampler heap is
    // essential: D3D12's sampler heap is capped at 2048 entries, and with
    // ~100 HD draws per frame across 2 frames-in-flight a 16-slot range
    // would overflow the ring and corrupt in-flight bindings (-> TDR).
    if (iblFromProbe_ != gfx::TextureHandle::Invalid) {
        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 13, iblFromProbe_);
    }
    if (iblToProbe_ != gfx::TextureHandle::Invalid) {
        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 14, iblToProbe_);
    }
    if (iblSplitSumLut_ != gfx::TextureHandle::Invalid) {
        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 15, iblSplitSumLut_);
    }

    // HD baseline key (used ONLY when the model has no authored MDX
    // lights). Camera-attached headlight: LH view has forward = +Z,
    // so direction-to-source is -Z in view space regardless of
    // camera pose. Tune intensity via constants.h.
    const auto kBaselineDirToSourceVS = Vector3f{0.0f, 0.0f, -1.0f};
    const auto kBaselineDiffuse       = Vector3f{kHdBaselineLightColor.x,   kHdBaselineLightColor.y,   kHdBaselineLightColor.z};
    const auto kBaselineAmbient       = Vector3f{kHdBaselineAmbientColor.x, kHdBaselineAmbientColor.y, kHdBaselineAmbientColor.z};

    for (auto& ref : refs) {
        auto* mi  = ref.mi;
        auto& geo = mi->gpuGeosets[ref.idx];
        if (geo.vb == gfx::BufferHandle::Invalid || geo.ib == gfx::BufferHandle::Invalid || geo.indexCount == 0)
            continue;

        int matId = geo.materialId;
        GPUMaterial* mat = nullptr;
        if (matId >= 0 && matId < (int)mi->gpuMaterials.size()) mat = &mi->gpuMaterials[matId];

        float geoAlpha = geo.geosetAlpha * mi->parentVisibility;
        if (geoAlpha < 0.01f) continue;

        int numLayers = mat ? (int)mat->cpu.layers.size() : 0;
        if (numLayers <= 0) numLayers = 1;

        // HD path uses vs/hd.bls with FourBoneSkinning -- bind rest-pose
        // data from unskinnedVb, not the compute-skinned gg.vb. Dedicated
        // rest-pose vertex buffer (separate from baseVertBuf which stays
        // as a structured SRV for the SD compute skinner) avoids the
        // cross-frame SRV<->Vertex state transition churn on a shared
        // resource. Falls back to gg.vb if the side stream didn't get
        // created (shouldn't happen for MDX models).
        const gfx::BufferHandle vb0 =
            (geo.unskinnedVb != gfx::BufferHandle::Invalid) ? geo.unskinnedVb
                                                            : geo.vb;
        cmd->BindVertexBuffer(0, vb0, sizeof(Vertex));
        cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);

        // Tangent side-stream (ATTR7 on slot 1). Required by the HD VS
        // when the hasTangent permute is picked.
        const bool hasTangents = (geo.tangentVb != gfx::BufferHandle::Invalid);
        if (hasTangents)
            cmd->BindVertexBuffer(1, geo.tangentVb, sizeof(Vector4f));

        // Bone weights+indices feeding FourBoneSkinning in vs/hd.bls +
        // bone palette CB (vsCB3). Bone stream binds to slot 2 when a
        // tangent stream lives on slot 1 (MeshHDSkinned layout), else
        // collapses onto slot 1 directly (MeshHDSkinnedNoTangent).
        // Skinning is independent of tangent availability — a mesh can
        // carry bones without authored tangents (rare for v1200 HD,
        // common otherwise) and should still animate.
        const bool hasBones =
            (geo.boneVb != gfx::BufferHandle::Invalid) &&
            (mi->bonePaletteCb != gfx::BufferHandle::Invalid);
        if (hasBones) {
            const uint32_t boneSlot = hasTangents ? 2u : 1u;
            cmd->BindVertexBuffer(boneSlot, geo.boneVb, sizeof(BoneVertex));
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 3, mi->bonePaletteCb);
        }

        // --- Per-geoset HD ShaderLight palette -----------------------------
        int lightCountForGeoset = 0;
        bool anyEnabled = false;
        for (const auto& L : mi->activeLights) { if (L.enabled) { anyEnabled = true; break; } }
        if (!anyEnabled) {
            bls::ShaderLight& sl = frame.lights[lightCountForGeoset++];
            sl.ambient  = { kBaselineAmbient.x, kBaselineAmbient.y, kBaselineAmbient.z, 0.0f };
            sl.diffuse  = { kBaselineDiffuse.x, kBaselineDiffuse.y, kBaselineDiffuse.z, 0.0f };
            sl.position = { kBaselineDirToSourceVS.x, kBaselineDirToSourceVS.y, kBaselineDirToSourceVS.z, 0.0f };
        }
        for (const auto& L : mi->activeLights) {
            if (!L.enabled) continue;
            if (lightCountForGeoset >= bls::kMaxLights) break;
            bls::ShaderLight& sl = frame.lights[lightCountForGeoset++];
            sl.ambient = { L.ambient.x, L.ambient.y, L.ambient.z, 0.0f };
            sl.diffuse = { L.diffuse.x, L.diffuse.y, L.diffuse.z, 0.0f };
            if (L.kind == FrameState::LightKind::Directional) {
                Vector3f d = L.worldDir;
                float    n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (n > 1e-6f) { d.x /= n; d.y /= n; d.z /= n; }
                Vector3f lv = whiteout::transform_normal(Vector3f{-d.x, -d.y, -d.z}, view2);
                sl.position = { lv.x, lv.y, lv.z, 0.0f };
            } else {
                Vector3f p = whiteout::transform_point(L.worldPos, view2);
                sl.position = { p.x, p.y, p.z, 1.0f };
            }
        }
        for (int i = lightCountForGeoset; i < bls::kMaxLights; ++i) frame.lights[i] = {};

        for (int li = 0; li < numLayers; ++li) {
            int   layerFilter   = FILTER_NONE;
            int   layerFlags    = 0;
            float layerAlpha    = 1.0f;
            int   layerTexId    = -1;
            int   texAnimId     = -1;
            int   layerShaderId = 0;
            int   layerNormalId    = -1;
            int   layerOrmId       = -1;
            int   layerEmissiveId  = -1;
            int   layerTeamColorId = -1;
            float layerEmissiveGain    = 0.0f;
            float layerFresnelOpacity  = 0.0f;
            float layerFresnelTeamColor = 0.0f;
            Vector3f layerFresnelColor = {0.0f, 0.0f, 0.0f};
            if (mat && li < (int)mat->cpu.layers.size()) {
                const auto& L = mat->cpu.layers[li];
                layerFilter       = L.filterMode;
                layerFlags        = L.flags;
                layerAlpha        = L.alpha;
                layerTexId        = L.textureId;
                texAnimId         = L.textureAnimationId;
                layerShaderId     = L.shaderId;
                layerNormalId     = L.normalMapId;
                layerOrmId        = L.ormMapId;
                layerEmissiveId   = L.emissiveMapId;
                layerTeamColorId  = L.teamColorMapId;
                layerEmissiveGain    = L.emissiveGain;
                layerFresnelOpacity  = L.fresnelOpacity;
                layerFresnelTeamColor = L.fresnelTeamColor;
                layerFresnelColor    = L.fresnelColor;
            }

            if (texAnimId >= 0 && texAnimId < (int)mi->texAnimPalette.size()) {
                const auto& e = mi->texAnimPalette[texAnimId];
                frame.texMtx0.rows[0] = { e.row0[0], e.row0[1], e.row0[2], e.row0[3] };
                frame.texMtx0.rows[1] = { e.row1[0], e.row1[1], e.row1[2], e.row1[3] };
            } else {
                frame.texMtx0 = bls::IdentityTexMtx();
            }
            frame.texMtx1 = bls::IdentityTexMtx();

            float combinedAlpha = geoAlpha * layerAlpha;
            if (combinedAlpha < 0.004f) continue;
            int effectiveFilter = layerFilter;
            if (combinedAlpha < 0.99f && layerFilter <= FILTER_TRANSPARENT)
                effectiveFilter = FILTER_BLEND;

            // Route program + permute family based on the MDX layer's
            // shader id. MatSelect (0x1403fa7c0) canonicalises SD <-> SD_on_HD;
            // Crystal shares the HD permute axes. Any mesh-authored HD
            // variant goes through hd.bls; SD-authored or unknown layers use
            // sd_on_hd.bls (which is the HD-mode fallback for legacy assets).
            const bool isHdMaterial =
                (layerShaderId == 1) /* HD */ || (layerShaderId == 24) /* Crystal */;
            const bls::GxShaderID programShaderId =
                isHdMaterial ? bls::GxShaderID::HD : bls::GxShaderID::SD_on_HD;
            const bls::BlsProgram* program =
                isHdMaterial ? blsHdProgram_ : blsSdOnHdProgram_;
            // Fall back to whichever program is available if the selected
            // one failed to load; keeps draws alive instead of going black.
            if (!program) program = blsHdProgram_ ? blsHdProgram_ : blsSdOnHdProgram_;
            if (!program) continue;

            bls::MatParams mp = bls::FromMdxLayer(effectiveFilter, layerFlags, programShaderId);
            if (mp.alpha == bls::GxMatAlpha::Modulate) {
                mp.diffuseColor = {combinedAlpha, 1, 1, 1};
            } else {
                mp.diffuseColor = {geo.geosetColor.x, geo.geosetColor.y, geo.geosetColor.z, combinedAlpha};
            }
            // HD PS pixelParams / fresnelColor sourced from the MDX layer.
            // Engine layout per CGxMatParams::PixelParams (IDA):
            //   pixelParams1 = {inverseSoftness, cloak, fresnelTeamColor, 0}
            //   fresnelColor = {fresnelR, fresnelG, fresnelB, fresnelOpacity}
            mp.emissiveGain     = layerEmissiveGain;
            mp.fresnelTeamColor = layerFresnelTeamColor;
            mp.fresnelOpacity   = layerFresnelOpacity;
            mp.fresnelColor     = layerFresnelColor;

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
            rs.teamColor      = (layerTeamColorId >= 0);
            const int  dbgMode     = hdDebugMode_.load();
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
            auto pso = blsPsoBuilder_->GetOrBuild(req);
            if (pso == gfx::PipelineHandle::Invalid) continue;
            cmd->BindPipeline(pso);

            frame.world = mi->worldTransform;

            // VS CB layout is shared between HD and SD_on_HD programs.
            if (auto* vs = (bls::HdVsCb*)gfx_->MapBuffer(blsHdVsCb_)) {
                bls::BuildHdVsCb(*vs, frame, mp);
                gfx_->UnmapBuffer(blsHdVsCb_);
            }
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 2, blsHdVsCb_);

            // PS CB layout diverges by program: hd_ps.slang reads PBR
            // fields directly (HdPsCb) while sd_on_hd_ps.slang reads a
            // padded legacy layout (SdOnHdPsCb) with invViewRow rows and
            // lightCountSlot.z bit-reinterpret.
            if (program == blsHdProgram_) {
                if (auto* ps = (bls::HdPsCb*)gfx_->MapBuffer(blsHdPsCb_)) {
                    bls::BuildHdPsCb(*ps, frame, mp);
                    gfx_->UnmapBuffer(blsHdPsCb_);
                }
                cmd->BindConstantBuffer(gfx::ShaderStage::Pixel, 2, blsHdPsCb_);
                // b3 DebugVisCB. Only meaningful when the HAS_DEBUG_VIS
                // permute is picked (rs.debugShader); bound
                // unconditionally so the shader slot is always live
                // even in "mode 0 = normal render" — cheap no-op bind.
                if (auto* dbg = (bls::DebugVisCb*)gfx_->MapBuffer(blsHdDebugVisCb_)) {
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
                    gfx_->UnmapBuffer(blsHdDebugVisCb_);
                }
                cmd->BindConstantBuffer(gfx::ShaderStage::Pixel, 3, blsHdDebugVisCb_);
            } else {
                if (auto* ps = (bls::SdOnHdPsCb*)gfx_->MapBuffer(blsSdOnHdPsCb_)) {
                    bls::BuildSdOnHdPsCb(*ps, frame, mp);
                    gfx_->UnmapBuffer(blsSdOnHdPsCb_);
                }
                cmd->BindConstantBuffer(gfx::ShaderStage::Pixel, 2, blsSdOnHdPsCb_);
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
            bindMaterialTex(0, layerTexId,       defaultTex_,    &wrapFlags); // t0 albedo (white)
            bindMaterialTex(1, layerNormalId,    defaultNormal_, nullptr);    // t1 normal (flat)
            bindMaterialTex(2, layerOrmId,       defaultOrm_,    nullptr);    // t2 ORM (occlusion=1, roughness=1, metal=0, teamBlend=0)
            bindMaterialTex(3, layerEmissiveId,  defaultBlack_,  nullptr);    // t3 emissive (no glow)
            // t4 team colour: when the MDX layer authors a TeamColor
            // subtexture (or the model carries a replaceableId=1 slot at
            // any point), bind the live UI swatch — the engine itself
            // ignores whatever texture the .mdx references here and
            // swaps in the per-player tint at draw time. Without an
            // authored slot, fall back to black so ORM.w-driven blends
            // on unrelated materials stay neutral.
            if (layerTeamColorId >= 0 && teamColorTex_ != gfx::TextureHandle::Invalid) {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 4, teamColorTex_);
            } else {
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 4, defaultBlack_);
            }
            cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerWrap_[wrapFlags]);

            cmd->DrawIndexed(geo.indexCount);
        }
    }
    return true;
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

    // Collect geoset draw refs from all models for global sort
    struct GeosetRef {
        ModelInstance* mi;
        int idx;
        int renderOrder, priorityPlane, geosetId;
    };
    const int selectedLod = ComputeSelectedLod();
    std::vector<GeosetRef> refs;
    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        const int modelLod = mi->hasLods ? selectedLod : 0;
        for (int i = 0; i < (int)mi->gpuGeosets.size(); i++) {
            auto& geo = mi->gpuGeosets[i];
            if (!GeosetPassesLod(geo.lod, modelLod)) continue;
            int ro = 1;
            int matId = geo.materialId;
            if (matId >= 0 && matId < (int)mi->gpuMaterials.size() && !mi->gpuMaterials[matId].cpu.layers.empty())
                ro = GetRenderOrder(mi->gpuMaterials[matId].cpu.layers[0].filterMode);
            refs.push_back({mi, i, ro, geo.priorityPlane, geo.geosetId});
        }
    }
    if (refs.empty()) return;

    std::sort(refs.begin(), refs.end(), [](const GeosetRef& a, const GeosetRef& b) {
        if (a.renderOrder != b.renderOrder) return a.renderOrder < b.renderOrder;
        if (a.priorityPlane != b.priorityPlane) return a.priorityPlane < b.priorityPlane;
        return a.geosetId < b.geosetId;
    });

    Matrix44f view2;
    { std::lock_guard<std::mutex> lock(dataMutex_); view2 = camera_.GetViewMatrix(); }
    float aspect2 = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    Matrix44f proj2 = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, aspect2, 1.0f, 10000.0f);

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
            int layerFilter = FILTER_NONE;
            int layerFlags  = 0;
            float layerAlpha = 1.0f;
            int layerTexId  = -1;

            if (mat && li < (int)mat->cpu.layers.size()) {
                const auto& L = mat->cpu.layers[li];
                layerFilter = L.filterMode;
                layerFlags  = L.flags;
                layerAlpha  = L.alpha;
                layerTexId  = L.textureId;
            }

            if (!anyLayerThisPass) {
                cmd->BindVertexBuffer(0, geo.vb, sizeof(Vertex));
                cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);
                anyLayerThisPass = true;
            }

            bool twoSided    = (layerFlags & MAT_TWO_SIDED) != 0;
            bool noDepthTest = (layerFlags & MAT_NO_DEPTH_TEST) != 0;
            bool noDepthSet  = (layerFlags & MAT_NO_DEPTH_SET) != 0;

            float combinedAlpha = geoAlpha * layerAlpha;
            if (combinedAlpha < 0.004f) continue;

            // For semi-transparent opaque/alpha-test layers, override to alpha-blend + noWrite
            int effectiveFilter = layerFilter;
            if (combinedAlpha < 0.99f && layerFilter <= FILTER_TRANSPARENT)
                effectiveFilter = FILTER_BLEND;

            cmd->BindPipeline(LookupMeshPSO(effectiveFilter, twoSided, noDepthTest, noDepthSet));

            float alphaRef = 0.0f;
            if (layerFilter == FILTER_TRANSPARENT) alphaRef = 0.75f;
            else if (layerFilter >= FILTER_MODULATE) alphaRef = 0.02f;

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
                CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
                Matrix44f world = mi->worldTransform;
                cb->world      = world.transpose();
                cb->view       = view2.transpose();
                cb->projection = proj2.transpose();

                Vector3f ldN = Vector3f{kDefaultLightDir.x, kDefaultLightDir.y, kDefaultLightDir.z}.normalized();
                cb->lightDir = {ldN.x, ldN.y, ldN.z, 0.0f};
                cb->lightColor    = kGeosetLightColor;
                cb->ambientColor  = {kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z, alphaRef};
                cb->extraParams   = {combinedAlpha, geoColor.x, geoColor.y, geoColor.z};
                cb->texAnimParams = {uOff, vOff, uTile, vTile};
                cb->materialFlags = {
                    (layerFlags & MAT_UNSHADED)       ? 1.0f : 0.0f,
                    (layerFlags & MAT_CONSTANT_COLOR) ? 1.0f : 0.0f,
                    texRot, 0.0f
                };
                gfx_->UnmapBuffer(cbPerFrame_);
            }

            // Per-model texture
            uint32_t wrapFlags = kWrapFlagsMask; // default: wrap both
            bool hasModelTex = false;
            if (layerTexId >= 0 && mi->gpuTextures.count(layerTexId)) {
                auto& gt = mi->gpuTextures[layerTexId];
                if (gt.tex != gfx::TextureHandle::Invalid) {
                    cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, gt.tex);
                    wrapFlags = gt.wrapFlags & kWrapFlagsMask;
                    hasModelTex = true;
                }
            }
            if (!hasModelTex)
                cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, defaultTex_);
            cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerWrap_[wrapFlags]);

            cmd->DrawIndexed(geo.indexCount, 0, 0);
        }
    }
}

// ============================================================================
// ViewCube — 3D orientation cube in top-right corner with Home button
// ============================================================================

Rect RenderService::GetViewCubeRect() const {
    int s = kViewCubeSize;
    int margin = 10;
    int cubeTop = margin + 28;
    return { width_ - s - margin, margin, width_ - margin, cubeTop + s };
}

bool RenderService::CreateViewCube() {
    // Generate face label atlas procedurally (platform-neutral)
    {
        int tw, th;
        auto pixels = GenerateViewCubeAtlas(tw, th);
        vcFaceTex_ = gfx_->CreateTexture({
            .width  = tw,
            .height = th,
            .format = gfx::Format::R8G8B8A8_UNORM,
            .usage  = gfx::TextureUsage::ShaderResource,
        }, pixels.data());
    }

    // Cube geometry: 24 vertices (4 per face), 36 indices
    // Face order: Front(+Y), Back(-Y), Left(-X), Right(+X), Top(+Z), Bottom(-Z)
    float s = 0.5f;
    struct VCVert { float x,y,z, nx,ny,nz, cr,cg,cb,ca, u,v; };

    // UV: each face maps to column i/6 in the texture atlas
    auto uv = [](int face, int corner) -> std::pair<float,float> {
        float u0 = face / 6.0f, u1 = (face + 1) / 6.0f;
        switch(corner) {
            case 0: return {u0, 1.0f}; // BL
            case 1: return {u1, 1.0f}; // BR
            case 2: return {u1, 0.0f}; // TR
            case 3: return {u0, 0.0f}; // TL
        }
        return {0,0};
    };

    std::vector<Vertex> verts;
    auto addFace = [&](int face, Vector3f p0, Vector3f p1, Vector3f p2, Vector3f p3, Vector3f n) {
        for (int c = 0; c < 4; c++) {
            auto [u, v] = uv(face, c);
            Vector3f p = (c==0) ? p0 : (c==1) ? p1 : (c==2) ? p2 : p3;
            verts.push_back({p, n, {1,1,1,1}, {u, v}});
        }
    };

    // Face geometry authored directly in the renderer-native default space.
    // "Front" is the face on the +forward side of the model (Blz +X, Max -Y).
    // Top/Bottom are always along +Z/-Z (Z-up is shared across supported spaces).
    struct FaceSpec {
        Vector3f p0, p1, p2, p3, n;
    };
    FaceSpec faces[6];
    if constexpr (kDefaultCoordSpace == CoordSpace::Blizzard) {
        // Blizzard: +X forward, +Y left, +Z up
        faces[0] = {{ s, s, s}, { s,-s, s}, { s,-s,-s}, { s, s,-s}, { 1, 0, 0}}; // Front (+X)
        faces[1] = {{-s,-s, s}, {-s, s, s}, {-s, s,-s}, {-s,-s,-s}, {-1, 0, 0}}; // Back (-X)
        faces[2] = {{ s, s, s}, {-s, s, s}, {-s, s,-s}, { s, s,-s}, { 0, 1, 0}}; // Left (+Y)
        faces[3] = {{-s,-s, s}, { s,-s, s}, { s,-s,-s}, {-s,-s,-s}, { 0,-1, 0}}; // Right (-Y)
        faces[4] = {{-s, s, s}, { s, s, s}, { s,-s, s}, {-s,-s, s}, { 0, 0, 1}}; // Top (+Z)
        faces[5] = {{-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s}, { 0, 0,-1}}; // Bottom (-Z)
    } else {
        // Max: +X right, +Y back, +Z up — model forward is -Y.
        faces[0] = {{ s,-s,-s}, {-s,-s,-s}, {-s,-s, s}, { s,-s, s}, { 0,-1, 0}}; // Front (-Y)
        faces[1] = {{-s, s,-s}, { s, s,-s}, { s, s, s}, {-s, s, s}, { 0, 1, 0}}; // Back (+Y)
        faces[2] = {{ s, s,-s}, { s,-s,-s}, { s,-s, s}, { s, s, s}, { 1, 0, 0}}; // Left (+X in Max — viewer-left of the facing model)
        faces[3] = {{-s,-s,-s}, {-s, s,-s}, {-s, s, s}, {-s,-s, s}, {-1, 0, 0}}; // Right (-X)
        faces[4] = {{-s, s, s}, { s, s, s}, { s,-s, s}, {-s,-s, s}, { 0, 0, 1}}; // Top (+Z)
        faces[5] = {{-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s}, { 0, 0,-1}}; // Bottom (-Z)
    }
    for (int i = 0; i < 6; ++i) {
        const auto& f = faces[i];
        addFace(i, f.p0, f.p1, f.p2, f.p3, f.n);
    }

    vcCubeVB_ = gfx_->CreateBuffer({
        .size  = sizeof(Vertex) * verts.size(),
        .usage = gfx::BufferUsage::Vertex,
    }, verts.data());

    // Index buffer: 2 triangles per face
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; f++) {
        uint32_t base = f * 4;
        idx.insert(idx.end(), {base, base+1, base+2, base, base+2, base+3});
    }
    vcCubeIB_ = gfx_->CreateBuffer({
        .size  = sizeof(uint32_t) * idx.size(),
        .usage = gfx::BufferUsage::Index,
    }, idx.data());

    // Edge lines (12 edges of the cube)
    std::vector<LineVertex> edges;
    Vector4f ec = {0.2f, 0.2f, 0.2f, 1.0f};
    float e = s * 1.001f; // slight offset to draw over faces
    // Bottom square
    edges.push_back({{-e,-e,-e}, ec}); edges.push_back({{ e,-e,-e}, ec});
    edges.push_back({{ e,-e,-e}, ec}); edges.push_back({{ e, e,-e}, ec});
    edges.push_back({{ e, e,-e}, ec}); edges.push_back({{-e, e,-e}, ec});
    edges.push_back({{-e, e,-e}, ec}); edges.push_back({{-e,-e,-e}, ec});
    // Top square
    edges.push_back({{-e,-e, e}, ec}); edges.push_back({{ e,-e, e}, ec});
    edges.push_back({{ e,-e, e}, ec}); edges.push_back({{ e, e, e}, ec});
    edges.push_back({{ e, e, e}, ec}); edges.push_back({{-e, e, e}, ec});
    edges.push_back({{-e, e, e}, ec}); edges.push_back({{-e,-e, e}, ec});
    // Verticals
    edges.push_back({{-e,-e,-e}, ec}); edges.push_back({{-e,-e, e}, ec});
    edges.push_back({{ e,-e,-e}, ec}); edges.push_back({{ e,-e, e}, ec});
    edges.push_back({{ e, e,-e}, ec}); edges.push_back({{ e, e, e}, ec});
    edges.push_back({{-e, e,-e}, ec}); edges.push_back({{-e, e, e}, ec});

    vcOutlineVB_ = gfx_->CreateBuffer({
        .size  = sizeof(LineVertex) * edges.size(),
        .usage = gfx::BufferUsage::Vertex,
    }, edges.data());

    return true;
}

void RenderService::RenderViewCube() {
    if (vcCubeVB_ == gfx::BufferHandle::Invalid ||
        vcCubeIB_ == gfx::BufferHandle::Invalid) return;

    auto* cmd = gfx_->GetImmediateContext();

    // Save main viewport, set ViewCube viewport (top-right corner)
    int s = kViewCubeSize;
    int margin = 10;
    gfx::Viewport vp = {
        (float)(width_ - s - margin),
        (float)(margin + 28),  // leave room for Home button above
        (float)s, (float)s,
        0.0f, 1.0f
    };
    cmd->SetViewport(vp);

    // Clear depth only in this viewport area
    auto* pt = primaryTarget();
    if (!pt) return;
    cmd->ClearDepth(pt->depth, 1.0f, 0);

    // Build view matrix: same rotation as camera, but fixed distance, looking at origin
    Matrix44f vcView;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        float dist = 3.5f;
        float cosP = cosf(camera_.GetPitch()), sinP = sinf(camera_.GetPitch());
        float cosY = cosf(camera_.GetYaw()),   sinY = sinf(camera_.GetYaw());
        Vector3f eye = { dist * cosP * cosY, dist * cosP * sinY, dist * sinP };
        Vector3f tgt = { 0, 0, 0 };
        Vector3f up  = { 0, 0, 1 };
        vcView = Matrix44f::look_at_rh(eye, tgt, up);
    }
    Matrix44f vcProj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, 1.0f, 0.1f, 100.0f);

    // Update constant buffer for ViewCube
    {
        CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
        cb->world      = Matrix44f::identity().transpose();
        cb->view       = vcView.transpose();
        cb->projection = vcProj.transpose();
        Vector3f ldN = Vector3f{kViewCubeLightDir.x, kViewCubeLightDir.y, kViewCubeLightDir.z}.normalized();
        cb->lightDir = {ldN.x, ldN.y, ldN.z, 0.0f};
        cb->lightColor   = kViewCubeLightColor;
        cb->ambientColor = kViewCubeAmbientColor;
        cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
        cb->materialFlags = {0.0f, 0.0f, 0.0f, 0.0f};
        gfx_->UnmapBuffer(cbPerFrame_);
    }

    // Draw cube faces (textured): opaque + noCull + default depth
    cmd->BindPipeline(meshPSO_[FILTER_NONE][1][0]);
    cmd->BindVertexBuffer(0, vcCubeVB_, sizeof(Vertex));
    cmd->BindIndexBuffer(vcCubeIB_, gfx::Format::R32_UINT);
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
    cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, cbPerFrame_);
    cmd->BindSampler(gfx::ShaderStage::Pixel, 0, samplerLinear_);
    if (vcFaceTex_ != gfx::TextureHandle::Invalid)
        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, vcFaceTex_);
    else
        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, defaultTex_);
    cmd->DrawIndexed(36, 0, 0);

    // Draw edges
    cmd->BindPipeline(linePSO_);
    cmd->BindVertexBuffer(0, vcOutlineVB_, sizeof(LineVertex));
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
    cmd->Draw(24, 0); // 12 edges × 2 verts

    // --- Home button icon (above cube, only on hover) ---
    if (vcHovered_) {
        gfx::Viewport homeVp = {
            vp.x + (float)s * kViewCubeHomeOffset,
            vp.y - 24.0f,
            (float)s * 0.3f, 20.0f,
            0.0f, 1.0f
        };
        cmd->SetViewport(homeVp);

        // Orthographic view for 2D drawing
        {
            CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
            cb->world = Matrix44f::identity().transpose();
            cb->view = Matrix44f::identity().transpose();
            cb->projection = Matrix44f::orthographic_rh(2.0f, 2.0f, -1.0f, 1.0f).transpose();
            cb->lightDir = {0,0,0,0};
            cb->lightColor = {1,1,1,1};
            cb->ambientColor = {1,1,1,1};
            cb->extraParams  = {1,0,0,0};
            cb->materialFlags = {0,0,0,0};
            gfx_->UnmapBuffer(cbPerFrame_);
        }

        // House icon (persistent VB, created once)
        if (vcHomeVB_ == gfx::BufferHandle::Invalid) {
            Vector4f hc = {0.7f, 0.7f, 0.7f, 1.0f};
            LineVertex house[] = {
                {{-0.4f, -0.6f, 0}, hc}, {{ 0.4f, -0.6f, 0}, hc}, // bottom
                {{-0.4f, -0.6f, 0}, hc}, {{-0.4f,  0.0f, 0}, hc}, // left wall
                {{ 0.4f, -0.6f, 0}, hc}, {{ 0.4f,  0.0f, 0}, hc}, // right wall
                {{-0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc}, // roof left
                {{ 0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc}, // roof right
                {{-0.5f,  0.0f, 0}, hc}, {{ 0.5f,  0.0f, 0}, hc}, // roof base
            };
            vcHomeVB_ = gfx_->CreateBuffer({
                .size  = sizeof(house),
                .usage = gfx::BufferUsage::Vertex,
            }, house);
        }
        if (vcHomeVB_ != gfx::BufferHandle::Invalid) {
            cmd->BindVertexBuffer(0, vcHomeVB_, sizeof(LineVertex));
            cmd->Draw(12, 0);
        }
    }

    // Restore main viewport
    cmd->SetViewport({0, 0, (float)width_, (float)height_, 0.0f, 1.0f});

    // Restore main scene constant buffer
    Matrix44f view, proj;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        view = camera_.GetViewMatrix();
    }
    float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    proj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f);
    {
        CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
        cb->world      = Matrix44f::identity().transpose();
        cb->view       = view.transpose();
        cb->projection = proj.transpose();
        Vector3f ldN = Vector3f{kDefaultLightDir.x, kDefaultLightDir.y, kDefaultLightDir.z}.normalized();
        cb->lightDir = {ldN.x, ldN.y, ldN.z, 0.0f};
        cb->lightColor   = kGeosetLightColor;
        cb->ambientColor = {kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z, 0.0f};
        cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
        cb->materialFlags = {0.0f, 0.0f, 0.0f, 0.0f};
        gfx_->UnmapBuffer(cbPerFrame_);
    }
}

// Hit test: returns face index 0-5, 6=Home, -1=none
int RenderService::HitTestViewCube(int mx, int my) {
    Rect r = GetViewCubeRect();

    // Home button area (above the cube, only when hovering)
    int cubeTop = r.top + 28;
    if (vcHovered_ && mx >= r.left && mx <= r.right && my >= r.top && my <= cubeTop)
        return 6;

    // Cube area
    if (mx < r.left || mx > r.right || my < cubeTop || my > r.bottom)
        return -1;

    // Ray-pick: project the 6 face centers to screen, find closest to click
    int s = kViewCubeSize;
    float vcX = (float)(width_ - s - 10);
    float vcY = 10.0f + 28.0f;  // matches cube viewport offset

    Matrix44f vcView;
    {
        float dist = 3.5f;
        float cosP = cosf(camera_.GetPitch()), sinP = sinf(camera_.GetPitch());
        float cosY = cosf(camera_.GetYaw()),   sinY = sinf(camera_.GetYaw());
        Vector3f eye = { dist*cosP*cosY, dist*cosP*sinY, dist*sinP };
        Vector3f up  = { 0, 0, 1 };
        vcView = Matrix44f::look_at_rh(eye, {0,0,0}, up);
    }
    Matrix44f vcProj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, 1.0f, 0.1f, 100.0f);
    Matrix44f vp_mat = vcView * vcProj;

    // Face centers and normals
    Vector3f centers[] = {{0,.5f,0},{0,-.5f,0},{-.5f,0,0},{.5f,0,0},{0,0,.5f},{0,0,-.5f}};
    Vector3f normals[] = {{0,1,0},{0,-1,0},{-1,0,0},{1,0,0},{0,0,1},{0,0,-1}};

    // Camera direction for backface culling
    float cosP = cosf(camera_.GetPitch()), sinP = sinf(camera_.GetPitch());
    float cosY = cosf(camera_.GetYaw()),   sinY = sinf(camera_.GetYaw());
    Vector3f camDir = { -cosP*cosY, -cosP*sinY, -sinP }; // toward target

    int bestFace = -1;
    float bestDist = 1e9f;

    for (int i = 0; i < 6; i++) {
        // Backface cull: skip faces pointing AWAY from camera
        float dot = normals[i].x*camDir.x + normals[i].y*camDir.y + normals[i].z*camDir.z;
        if (dot > -0.05f) continue;

        // Project center through View*Proj, perspective divide, viewport remap
        Vector3f c = centers[i];
        float cx = c.x*vp_mat.data[0][0] + c.y*vp_mat.data[1][0] + c.z*vp_mat.data[2][0] + vp_mat.data[3][0];
        float cy = c.x*vp_mat.data[0][1] + c.y*vp_mat.data[1][1] + c.z*vp_mat.data[2][1] + vp_mat.data[3][1];
        float cw = c.x*vp_mat.data[0][3] + c.y*vp_mat.data[1][3] + c.z*vp_mat.data[2][3] + vp_mat.data[3][3];
        if (fabsf(cw) < 1e-6f) continue;
        float ndcX = cx / cw;
        float ndcY = cy / cw;
        float spx = vcX + (float)s * (1.0f + ndcX) * 0.5f;
        float spy = vcY + (float)s * (1.0f - ndcY) * 0.5f;

        float dx = spx - mx;
        float dy = spy - my;
        float d = dx*dx + dy*dy;
        if (d < bestDist && d < (s*s*0.06f)) { // tighter radius — only on the cube itself
            bestDist = d;
            bestFace = i;
        }
    }
    return bestFace;
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
