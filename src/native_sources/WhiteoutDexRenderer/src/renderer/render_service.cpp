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
#include <whiteout/models/mdx/parser.h>
#include <numbers>
#include <cstring>

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

void RenderService::SetPE1ChildCoordSpace(CoordSpace space) {
    pe1ChildCoordSpace_ = space;
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
            slot.loaded = true;

            auto tmpl = getOrLoadTemplate(slot.config.modelPath);
            if (!tmpl) continue;

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
        std::move(model), texBasePath, pe1ChildCoordSpace_, activeContentProvider_);
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
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.pixels = tex.rgba;
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
        }
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder = mat.sortOrder;
    }
    // Stage meshes
    for (auto& mesh : tmpl.meshes) {
        StagedGeoset& sg = mi->stagedGeosets[mesh.geosetId];
        sg.materialId = mesh.materialId;
        int vc = (int)mesh.positions.size();
        sg.vertices.resize(vc);
        for (int i = 0; i < vc; i++) {
            sg.vertices[i].position = mesh.positions[i];
            sg.vertices[i].normal = (i < (int)mesh.normals.size()) ? mesh.normals[i] : Vector3f{0,0,1};
            sg.vertices[i].uv = (i < (int)mesh.uvs.size()) ? mesh.uvs[i] : Vector2f{0,0};
            sg.vertices[i].color = {1,1,1,1};
        }
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
    // PE2 particles
    for (int i = 0; i < (int)tmpl.pe2Configs.size(); i++)
        mi->particles.AddEmitter(i, tmpl.pe2Configs[i]);
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
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.pixels = tex.rgba;
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
        st.replaceableId = tex.replaceableId;
        st.wrapFlags = tex.wrapFlags;
        st.pixels = tex.rgba;
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
        }
        sm.priorityPlane = mat.priorityPlane;
        sm.sortOrder     = mat.sortOrder;
    }

    // Meshes → staged
    for (auto& mesh : meshes) {
        StagedGeoset& sg = mi->stagedGeosets[mesh.geosetId];
        sg.materialId = mesh.materialId;
        int vc = (int)mesh.positions.size();
        sg.vertices.resize(vc);
        for (int i = 0; i < vc; i++) {
            sg.vertices[i].position = mesh.positions[i];
            sg.vertices[i].normal   = (i < (int)mesh.normals.size()) ? mesh.normals[i] : Vector3f{0,0,1};
            sg.vertices[i].uv       = (i < (int)mesh.uvs.size()) ? mesh.uvs[i] : Vector2f{0,0};
            sg.vertices[i].color    = {1.0f, 1.0f, 1.0f, 1.0f};
        }
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

    // Particles
    for (size_t i = 0; i < particleConfigs.size(); i++) {
        mi->particles.AddEmitter((int)i, particleConfigs[i]);
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
}

// ============================================================================
// ApplyFrameState helpers
// ============================================================================

void RenderService::ApplyBoneMatrices(ModelInstance& mi, const FrameState& state) {
    if (state.boneWorldMatrices.empty()) return;

    int bc = (int)state.boneWorldMatrices.size();
    Vector3f camPos = camera_.GetSource();
    Vector3f worldUp = {0, 0, 1};

    std::vector<float> worldFlat(bc * 16);
    for (int i = 0; i < bc; i++) {
        Matrix44f boneM = state.boneWorldMatrices[i];

        uint32_t bbFlags = (i < (int)mi.billboardFlags.size()) ? mi.billboardFlags[i] : 0;
        if (bbFlags != 0) {
            Vector3f pivF = (i < (int)mi.nodePivots.size())
                              ? mi.nodePivots[i] : Vector3f{0, 0, 0};
            Vector3f pivWorld = whiteout::transform_point(pivF, boneM);

            Vector3f toCamera = camPos - pivWorld;
            float dist = toCamera.length();
            if (dist > kBillboardDistThreshold) {
                Matrix44f bbRot = Matrix44f::identity();
                bool haveRot = false;

                if (bbFlags & BONE_BILLBOARD_FULL) {
                    Vector3f fwd = Vector3f{-toCamera.x, -toCamera.y, -toCamera.z}.normalized();
                    Vector3f right = whiteout::cross(fwd, worldUp);
                    float rightLen = right.length();
                    if (rightLen < kBillboardDistThreshold)
                        right = {1, 0, 0};
                    right = right.normalized();
                    Vector3f up = whiteout::cross(right, fwd).normalized();
                    bbRot = {};
                    bbRot.data[0][0] = right.x; bbRot.data[0][1] = right.y; bbRot.data[0][2] = right.z;
                    bbRot.data[1][0] = fwd.x;   bbRot.data[1][1] = fwd.y;   bbRot.data[1][2] = fwd.z;
                    bbRot.data[2][0] = up.x;    bbRot.data[2][1] = up.y;    bbRot.data[2][2] = up.z;
                    bbRot.data[3][3] = 1.0f;
                    haveRot = true;
                } else if (bbFlags & BONE_BILLBOARD_LOCK_Z) {
                    float yaw = atan2f(toCamera.y, toCamera.x);
                    bbRot = Matrix44f::rotation_z(yaw);
                    haveRot = true;
                } else if (bbFlags & BONE_BILLBOARD_LOCK_Y) {
                    float angle = atan2f(toCamera.z, toCamera.x);
                    bbRot = Matrix44f::rotation_y(angle);
                    haveRot = true;
                } else if (bbFlags & BONE_BILLBOARD_LOCK_X) {
                    float angle = atan2f(toCamera.z, toCamera.y);
                    bbRot = Matrix44f::rotation_x(angle);
                    haveRot = true;
                }

                if (haveRot) {
                    Matrix44f T_negRest = Matrix44f::translation({-pivF.x, -pivF.y, -pivF.z});
                    Matrix44f T_world   = Matrix44f::translation({pivWorld.x, pivWorld.y, pivWorld.z});
                    boneM = T_negRest * bbRot * T_world;
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
}

void RenderService::ApplyParticleFrameStates(ModelInstance& mi, const FrameState& state) {
    for (auto& ps : state.particleStates) {
        // Legacy path — still the primary until Phase 6 cut-over.
        ParticleEmitterState st;
        st.transform    = ps.transform;
        st.emissionRate = ps.emissionRate;
        st.speed        = ps.speed;
        st.variation    = ps.variation;
        st.coneAngle    = ps.coneAngle;
        st.gravity      = ps.gravity;
        st.width        = ps.width;
        st.length       = ps.length;
        st.visibility   = ps.visibility;
        mi.particles.UpdateEmitterState(ps.emitterId, st);

        // New service path — mirror the state to the registered PlaneEmitter
        // if this model has one. Transform handling depends on the emitter's
        // declared simulation coord space:
        //   CoordSpace::Max       — pass ps.transform directly (MDX-sourced
        //                           emitters; matches legacy visuals).
        //   CoordSpace::Blizzard  — conjugate via MaxToBlzTransform.
        if (auto* em = particleService_.GetEmitter(mi.handle, ps.emitterId)) {
            em->SetEmissionRate(ps.emissionRate);
            em->SetVelocity(ps.speed);
            em->SetVelocityVariation(ps.variation);
            em->SetLatitude(ps.coneAngle);
            em->SetAcceleration(ps.gravity);
            em->SetWidth(ps.width);
            em->SetHeight(ps.length);
            em->SetVisible(ps.visibility > 0.02f);
            if (em->GetCoordSpace() == particle::CoordSpace::Blizzard) {
                em->SetModelToWorld(particle::MaxToBlzTransform(ps.transform));
            } else {
                em->SetModelToWorld(ps.transform);
            }
        }
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

void RenderService::SetCameraPresets(const std::vector<CameraPreset>& presets) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    pendingCameraPresets_ = presets;
    cameraDirty_ = true;
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
}

DisplayFlags RenderService::GetDisplayFlags() const {
    return { showGrid_, showParticles_, showRibbons_, showCollisions_ };
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
        geosets   += (int)mi->gpuGeosets.size();
        textures  += (int)mi->gpuTextures.size();
        nodes     += mi->skinning.NodeCount();
        particles += mi->particles.GetTotalParticleCount();
        segments  += mi->ribbons.GetTotalSegmentCount();
    }
}

// ============================================================================
// Staged → GPU Resource Upload (render thread only)
// ============================================================================

void RenderService::UploadStagedTextures(ModelInstance& mi) {
    for (auto& [id, st] : mi.stagedTextures) {
        if (st.width <= 0 || st.height <= 0) continue;

        if (mi.gpuTextures.count(id)) mi.gpuTextures[id].Release(*gfx_);

        GPUTexture gt;
        gt.tex = gfx_->CreateTexture({
            .width  = st.width,
            .height = st.height,
            .format = gfx::Format::R8G8B8A8_UNORM,
            .usage  = gfx::TextureUsage::ShaderResource,
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

        mi.gpuGeosets.push_back(gg);
    }
    mi.stagedGeosets.clear();
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
}

void RenderService::ProcessStagedData() {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Remove models marked for clear
    for (auto it = models_.begin(); it != models_.end(); ) {
        if (it->second->stagedClear) {
            it->second->ReleaseGPU(*gfx_);
            it = models_.erase(it);
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
    cmd->BindPipeline(skinPSO_);

    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (!mi->skinning.HasSkeleton() || !mi->skinning.IsReady()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent — skip skinning
        if (mi->nodePalette == gfx::BufferHandle::Invalid) continue;

        mi->skinning.ComputeOffsetMatrices();

        // Upload offset matrices to the node palette buffer
        void* mapped = gfx_->MapBuffer(mi->nodePalette);
        if (!mapped) continue;
        memcpy(mapped, mi->skinning.OffsetMatrices(),
               sizeof(Matrix44f) * mi->skinning.NodeCount());
        gfx_->UnmapBuffer(mi->nodePalette);

        // Bind node palette SRV (slot 0) — shared across all geosets of this model
        cmd->BindShaderResource(gfx::ShaderStage::Compute, 0, mi->nodePalette);

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
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : models_) {
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        mi->particles.Simulate(dt);
    }
    // New PE2 service — runs across all registered emitters in one pass.
    // Parent-visibility gating isn't applied here yet; visibility is set via
    // SetVisible() in ApplyParticleFrameStates.
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

void RenderService::RenderParticles() {
    auto* cmd = gfx_->GetImmediateContext();

    // -----------------------------------------------------------------
    // Phase 5c: service draw path. When any PE2-service emitter is
    // registered, the service owns all PE2 rendering — the legacy
    // per-model ParticleSystem::BuildBillboards loop is skipped entirely
    // for those models. Models that ONLY used the legacy path (e.g.
    // Max-plugin adapter which doesn't feed the service yet) still get
    // rendered via the legacy path below.
    // -----------------------------------------------------------------
    std::vector<Vertex> serviceVerts;
    std::vector<particle::EmitterDrawList> serviceDrawLists;
    Matrix44f viewMatService;
    bool haveServiceParticles = false;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        haveServiceParticles = particleService_.EmitterCount() > 0;
        if (haveServiceParticles) {
            viewMatService = camera_.GetViewMatrix();
            particleService_.BuildGeometry(viewMatService, serviceVerts, serviceDrawLists);
        }
    }

    if (haveServiceParticles && !serviceVerts.empty()) {
        int vertCount = (int)serviceVerts.size();

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
            memcpy(mapped, serviceVerts.data(), sizeof(Vertex) * vertCount);
            gfx_->UnmapBuffer(particleServiceVB_);
        }

        cmd->BindVertexBuffer(0, particleServiceVB_, sizeof(Vertex));

        // Render each emitter's slice.
        int drawOffset = 0;
        for (const auto& dl : serviceDrawLists) {
            if (dl.vertexCount <= 0) continue;

            int legacyFilter = ServiceFilterToLegacy(dl.material.filterMode);
            cmd->BindPipeline(LookupMeshPSO(legacyFilter, /*twoSided*/true,
                                            /*noDepthTest*/false, /*noDepthSet*/true));

            {
                float alphaRef = (legacyFilter == FILTER_TRANSPARENT) ? 0.75f : 0.0f;
                CBPerFrame* cb = (CBPerFrame*)gfx_->MapBuffer(cbPerFrame_);
                cb->world      = Matrix44f::identity().transpose();
                float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
                Matrix44f proj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, aspect, 1.0f, 10000.0f);
                cb->view       = viewMatService.transpose();
                cb->projection = proj.transpose();
                Vector3f ldN = Vector3f{kDefaultLightDir.x, kDefaultLightDir.y, kDefaultLightDir.z}.normalized();
                cb->lightDir = {ldN.x, ldN.y, ldN.z, 0.0f};
                cb->lightColor   = kParticleLightColor;
                cb->ambientColor = {kParticleAmbientBase.x, kParticleAmbientBase.y, kParticleAmbientBase.z, alphaRef};
                cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
                cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
                cb->materialFlags = {dl.material.unshaded ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
                gfx_->UnmapBuffer(cbPerFrame_);
            }
            cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame_);
            cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, cbPerFrame_);

            // Texture lookup — uses the model's gpuTextures map since textures
            // are still owned per-model by the ModelInstance.
            uint32_t wrapFlags = kWrapFlagsMask;
            bool hasModelTex = false;
            ModelInstance* owner = nullptr;
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                owner = getModel(dl.model);
                if (owner && dl.material.textureId >= 0) {
                    auto it = owner->gpuTextures.find(dl.material.textureId);
                    if (it != owner->gpuTextures.end() && it->second.tex != gfx::TextureHandle::Invalid) {
                        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, it->second.tex);
                        wrapFlags = it->second.wrapFlags & kWrapFlagsMask;
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

    // -----------------------------------------------------------------
    // Legacy path — draws ParticleSystem emitters that were NOT mirrored
    // into the service. Loops only over models where the service has no
    // emitters registered, so we avoid double-rendering the same content.
    // -----------------------------------------------------------------
    for (auto& [_mh, _mi] : models_) {
    auto* mi = _mi.get();

    // Snapshot all data under one lock
    float pitch, yaw;
    Matrix44f viewMat;
    ParticleSystem::BillboardResult bbResult;
    std::vector<ParticleEmitterConfig> configs;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!mi->particles.HasEmitters()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        // If the service owns any emitters for this model, the service
        // already rendered them above — don't duplicate via the legacy path.
        if (haveServiceParticles && particleService_.HasEmittersForModel(_mh)) continue;
        pitch   = camera_.GetPitch();
        yaw     = camera_.GetYaw();
        viewMat = camera_.GetViewMatrix();
        bbResult = mi->particles.BuildBillboards(pitch, yaw);

        for (int eid : bbResult.emitterIds) {
            auto* c = mi->particles.GetConfig(eid);
            configs.push_back(c ? *c : ParticleEmitterConfig{});
        }
    }
    // Lock released — safe to call DX11

    auto& verts = bbResult.vertices;
    auto& emitterIds = bbResult.emitterIds;
    auto& vertCounts = bbResult.vertCounts;
    if (verts.empty()) continue;

    int vertCount = (int)verts.size();

    // Grow particle VB if needed
    if (mi->particleVB == gfx::BufferHandle::Invalid || vertCount > mi->particleVBSize) {
        gfx_->Destroy(mi->particleVB);
        int newSize = (std::max)(vertCount, 1024);
        gfx::BufferDesc bd;
        bd.size  = (uint32_t)(sizeof(Vertex) * newSize);
        bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
        mi->particleVB = gfx_->CreateBuffer(bd);
        mi->particleVBSize = newSize;
    }

    // Upload vertex data
    void* mapped = gfx_->MapBuffer(mi->particleVB);
    if (!mapped) continue;
    memcpy(mapped, verts.data(), sizeof(Vertex) * vertCount);
    gfx_->UnmapBuffer(mi->particleVB);

    // Bind particle VB
    cmd->BindVertexBuffer(0, mi->particleVB, sizeof(Vertex));

    // Render per-emitter with correct PSO and texture
    int drawOffset = 0;
    for (int ei = 0; ei < (int)emitterIds.size(); ei++) {
        auto& cfg = configs[ei];
        int count = vertCounts[ei];
        if (count <= 0) { continue; }

        // Particles: always two-sided, always noWrite depth.
        // LookupMeshPSO with noDepthSet=true gives [filter][1=noCull][1=noWrite]
        cmd->BindPipeline(LookupMeshPSO(cfg.filterMode, true, false, true));

        // Update constant buffer
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

        // Draw this emitter's particles
        cmd->Draw(count, drawOffset);
        drawOffset += count;
    }
    } // end for each model
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

        if (cs.type == 0) {
            // Box: 12 edges
            Vector3f mn = cs.vmin, mx = cs.vmax;
            Vector3f corners[8] = {
                {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z},
                {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z}
            };
            int edges[24] = {0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7};
            for (int i = 0; i < 24; i += 2) {
                Vector3f pa = whiteout::transform_point(corners[edges[i]], cs.transform);
                Vector3f pb = whiteout::transform_point(corners[edges[i+1]], cs.transform);
                lines.push_back({pa, col});
                lines.push_back({pb, col});
            }
        } else if (cs.type == 1) {
            // Sphere: 3 circles (XY, XZ, YZ planes)
            const int segs = 24;
            for (int plane = 0; plane < 3; plane++) {
                for (int i = 0; i < segs; i++) {
                    float a0 = (float)i / segs * 6.28318530f;
                    float a1 = (float)(i+1) / segs * 6.28318530f;
                    Vector3f p0, p1;
                    float c0 = cs.radius * cosf(a0), s0 = cs.radius * sinf(a0);
                    float c1 = cs.radius * cosf(a1), s1 = cs.radius * sinf(a1);
                    if (plane == 0)      { p0 = {cs.vmin.x+c0, cs.vmin.y+s0, cs.vmin.z}; p1 = {cs.vmin.x+c1, cs.vmin.y+s1, cs.vmin.z}; }
                    else if (plane == 1) { p0 = {cs.vmin.x+c0, cs.vmin.y, cs.vmin.z+s0}; p1 = {cs.vmin.x+c1, cs.vmin.y, cs.vmin.z+s1}; }
                    else                 { p0 = {cs.vmin.x, cs.vmin.y+c0, cs.vmin.z+s0}; p1 = {cs.vmin.x, cs.vmin.y+c1, cs.vmin.z+s1}; }
                    Vector3f pa = whiteout::transform_point(p0, cs.transform);
                    Vector3f pb = whiteout::transform_point(p1, cs.transform);
                    lines.push_back({pa, col});
                    lines.push_back({pb, col});
                }
            }
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

    // Shaders + pipelines + default geometry (grid, view cube) are part of device init
    if (!CreateShaders())          { CleanupD3D(); return false; }
    if (!CreatePipelines())        { CleanupD3D(); return false; }
    if (!CreateDefaultResources()) { CleanupD3D(); return false; }

    return true;
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

        // Grid + ViewCube
        gfx_->Destroy(gridVB_);
        gfx_->Destroy(vcCubeVB_);  gfx_->Destroy(vcCubeIB_);
        gfx_->Destroy(vcOutlineVB_); gfx_->Destroy(vcFaceTex_);

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

void RenderService::RenderGeosets() {
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
    std::vector<GeosetRef> refs;
    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        for (int i = 0; i < (int)mi->gpuGeosets.size(); i++) {
            auto& geo = mi->gpuGeosets[i];
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

    // Front (+Y) — viewed from +Y: screen-right = -X, so swap X order
    addFace(0, {s,s,-s}, {-s,s,-s}, {-s,s,s}, {s,s,s}, {0,1,0});
    // Back (-Y) — viewed from -Y: screen-right = +X
    addFace(1, {-s,-s,-s}, {s,-s,-s}, {s,-s,s}, {-s,-s,s}, {0,-1,0});
    // Left (-X)
    addFace(2, {-s,-s,-s}, {-s,s,-s}, {-s,s,s}, {-s,-s,s}, {-1,0,0});
    // Right (+X)
    addFace(3, {s,s,-s}, {s,-s,-s}, {s,-s,s}, {s,s,s}, {1,0,0});
    // Top (+Z)
    addFace(4, {-s,s,s}, {s,s,s}, {s,-s,s}, {-s,-s,s}, {0,0,1});
    // Bottom (-Z)
    addFace(5, {-s,-s,-s}, {s,-s,-s}, {s,s,-s}, {-s,s,-s}, {0,0,-1});

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

        // House icon as dynamic lines (temp buffer)
        Vector4f hc = {0.7f, 0.7f, 0.7f, 1.0f};
        LineVertex house[] = {
            {{-0.4f, -0.6f, 0}, hc}, {{ 0.4f, -0.6f, 0}, hc}, // bottom
            {{-0.4f, -0.6f, 0}, hc}, {{-0.4f,  0.0f, 0}, hc}, // left wall
            {{ 0.4f, -0.6f, 0}, hc}, {{ 0.4f,  0.0f, 0}, hc}, // right wall
            {{-0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc}, // roof left
            {{ 0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc}, // roof right
            {{-0.5f,  0.0f, 0}, hc}, {{ 0.5f,  0.0f, 0}, hc}, // roof base
        };

        gfx::BufferDesc bd;
        bd.size  = sizeof(house);
        bd.usage = gfx::BufferUsage::Vertex;
        gfx::BufferHandle homeVB = gfx_->CreateBuffer(bd, house);
        if (homeVB != gfx::BufferHandle::Invalid) {
            cmd->BindVertexBuffer(0, homeVB, sizeof(LineVertex));
            cmd->Draw(12, 0);
            gfx_->Destroy(homeVB);
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
    constexpr float HALF_PI = std::numbers::pi_v<float> / 2.0f;
    // Face order: Front(+Y), Back(-Y), Left(-X), Right(+X), Top(+Z), Bottom(-Z)
    switch (faceIndex) {
        case 0: camera_.SetYaw(HALF_PI);   camera_.SetPitch(0.0f); break;  // Front
        case 1: camera_.SetYaw(-HALF_PI);  camera_.SetPitch(0.0f); break;  // Back
        case 2: camera_.SetYaw(std::numbers::pi_v<float>);      camera_.SetPitch(0.0f); break;  // Left
        case 3: camera_.SetYaw(0.0f);      camera_.SetPitch(0.0f); break;  // Right
        case 4: camera_.SetYaw(HALF_PI);   camera_.SetPitch(1.55f); break; // Top
        case 5: camera_.SetYaw(HALF_PI);   camera_.SetPitch(-1.55f); break;// Bottom
    }
}

} // namespace WhiteoutDex
