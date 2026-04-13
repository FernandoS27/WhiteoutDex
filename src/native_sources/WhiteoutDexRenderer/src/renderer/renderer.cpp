// ============================================================================
// WhiteoutDex Real-Time Renderer — Core Implementation
// Phase 4: Matrix-based CPU Vertex Skinning
// ============================================================================

#include "renderer.h"
#include "shaders.h"
#include "resource.h"
#include "team_glow_data.h"
#include "mdx_model_adapter.h"
#include "file_content_provider.h"
#include <whiteout/models/mdx/parser.h>

// PE1 model template — full definition (uses MdxModelAdapter which is now fully included)
struct WhiteoutDex::Renderer::PE1ModelTemplate {
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
#include <windowsx.h>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

namespace WhiteoutDex {

static const wchar_t* WINDOW_CLASS = L"WhiteoutDexRendererClass";
static const wchar_t* WINDOW_TITLE = L"Whiteout Renderer";

// ============================================================================
// Constructor / Destructor
// ============================================================================

Renderer::Renderer() {}
Renderer::~Renderer() { Close(); }

// ============================================================================
// Lifecycle
// ============================================================================

bool Renderer::Open(int width, int height) {
    if (running_) return true;
    
    // Join any previous thread that exited (e.g. user closed the window)
    if (renderThread_.joinable()) renderThread_.join();
    
    running_ = true;
    initialized_ = false;
    renderThread_ = std::thread(&Renderer::RenderThread, this, width, height);
    for (int i = 0; i < 500 && !initialized_ && running_; ++i) Sleep(10);
    return initialized_;
}

void Renderer::Close() {
    running_ = false;
    if (renderThread_.joinable()) {
        if (hwnd_) PostMessage(hwnd_, WM_CLOSE, 0, 0);
        renderThread_.join();
    }
}

bool Renderer::IsOpen() const { return running_ && initialized_; }

void Renderer::SetCamera(float pitch, float yaw, float distance,
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

void Renderer::ClearModel() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : models_) {
        mi->stagedClear = true;
        mi->stagedDirty = true;
    }
    focusModelHandle_ = 0;
}

void Renderer::RemoveModel(uint32_t handle) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = models_.find(handle);
    if (it != models_.end()) {
        it->second->ReleaseGPU();
        models_.erase(it);
    }
    if (focusModelHandle_ == handle) {
        focusModelHandle_ = models_.empty() ? 0 : models_.begin()->first;
    }
}

void Renderer::SetAttachmentConfigs(uint32_t handle, const std::vector<AttachmentConfig>& configs) {
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

void Renderer::SetPE1ChildCoordSpace(CoordSpace space) {
    pe1ChildCoordSpace_ = space;
}

void Renderer::SetPE1BasePath(const std::string& basePath) {
    pe1BasePath_ = basePath;
    contentProvider_.SetBasePath(basePath);
}

void Renderer::SetPE1Configs(uint32_t handle, const std::vector<PE1EmitterConfig>& configs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;
    for (int i = 0; i < (int)configs.size(); i++)
        mi->pe1.AddEmitter(i, configs[i]);
}

// ============================================================================
// Attachment Model Loading
// ============================================================================

void Renderer::UpdateAttachments() {
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
            child->pe1BirthTimeMs = currentTimeMs_;
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

std::shared_ptr<Renderer::PE1ModelTemplate> Renderer::getOrLoadTemplate(const std::string& modelPath) {
    auto it = pe1TemplateCache_.find(modelPath);
    if (it != pe1TemplateCache_.end()) return it->second;

    // Try to read the model file via the content provider (disk → CASC → MPQ).
    auto fileData = contentProvider_.ReadFile(modelPath);
    if (!fileData || fileData->empty()) {
        pe1TemplateCache_[modelPath] = nullptr;  // cache miss
        return nullptr;
    }

    // Parse MDX from memory buffer
    whiteout::mdx::Parser mdxParser;
    whiteout::mdx::Model model;
    try {
        model = mdxParser.parse(std::span<const whiteout::u8>(fileData->data(), fileData->size()));
    } catch (...) {
        pe1TemplateCache_[modelPath] = nullptr;
        return nullptr;
    }

    // basePath for texture resolution: use pe1BasePath_ (war3 data root)
    // so textures like "Textures\Footprint00.blp" resolve correctly
    namespace fs = std::filesystem;
    fs::path texBasePath = pe1BasePath_.empty() ? fs::path(modelPath).parent_path() : fs::path(pe1BasePath_);

    auto tmpl = std::make_shared<PE1ModelTemplate>();
    auto adapter = std::make_shared<MdxModelAdapter>(
        std::move(model), texBasePath, pe1ChildCoordSpace_, &contentProvider_);
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

    pe1TemplateCache_[modelPath] = tmpl;
    return tmpl;
}

void Renderer::stageModelFromTemplate(ModelInstance* mi, const PE1ModelTemplate& tmpl) {
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
            sg.vertices[i].normal = (i < (int)mesh.normals.size()) ? mesh.normals[i] : XMFLOAT3{0,0,1};
            sg.vertices[i].uv = (i < (int)mesh.uvs.size()) ? mesh.uvs[i] : XMFLOAT2{0,0};
            sg.vertices[i].color = {1,1,1,1};
        }
        sg.indices = mesh.indices;
    }
    // Skeleton
    if (tmpl.skeleton.nodeCount > 0) {
        std::vector<float> invBind(tmpl.skeleton.nodeCount * 16);
        for (int i = 0; i < tmpl.skeleton.nodeCount; i++) {
            XMFLOAT4X4 f44; XMStoreFloat4x4(&f44, tmpl.skeleton.inverseBindMatrices[i]);
            float* dst = &invBind[i * 16];
            dst[0]=f44._11; dst[1]=f44._12; dst[2]=f44._13; dst[3]=f44._14;
            dst[4]=f44._21; dst[5]=f44._22; dst[6]=f44._23; dst[7]=f44._24;
            dst[8]=f44._31; dst[9]=f44._32; dst[10]=f44._33; dst[11]=f44._34;
            dst[12]=f44._41; dst[13]=f44._42; dst[14]=f44._43; dst[15]=f44._44;
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

void Renderer::UpdatePE1(float dt) {
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
            child->pe1BirthTimeMs = currentTimeMs_;

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
            it->second->ReleaseGPU();
            models_.erase(it);
            pe1InstanceCount_--;
        }
    }
}

void Renderer::EvaluatePE1Children() {
    struct ChildEval {
        uint32_t handle;
        std::shared_ptr<IModelSource> adapter;
        int localTimeMs;
        int seqIdx;
        int globalTimeMs;  // unclamped elapsed since birth (for global sequences)
    };
    std::vector<ChildEval> toEval;
    XMFLOAT3 camPos;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        camPos = camera_.GetSource();
        int timeMs = currentTimeMs_.load();
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

void Renderer::UpdateMaterials(uint32_t handle, const std::vector<MaterialData>& materials,
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

void Renderer::UpdateMaterials(const std::vector<MaterialData>& materials,
                               const std::vector<TextureData>& textures) {
    UpdateMaterials(focusModelHandle_, materials, textures);
}

// ============================================================================
// Multi-Model Loading API
// ============================================================================

uint32_t Renderer::AddModel(const std::vector<MeshData>& meshes,
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
            sg.vertices[i].normal   = (i < (int)mesh.normals.size()) ? mesh.normals[i] : XMFLOAT3{0,0,1};
            sg.vertices[i].uv       = (i < (int)mesh.uvs.size()) ? mesh.uvs[i] : XMFLOAT2{0,0};
            sg.vertices[i].color    = {1.0f, 1.0f, 1.0f, 1.0f};
        }
        sg.indices = mesh.indices;
    }

    // Skeleton
    if (skeleton.nodeCount > 0) {
        // Convert XMMATRIX array to flat float array for existing SkinningSystem
        std::vector<float> invBindFlat(skeleton.nodeCount * 16);
        for (int i = 0; i < skeleton.nodeCount; i++) {
            // XMMATRIX stores row-major, SkinningSystem expects row-major 16 floats
            const XMMATRIX& m = skeleton.inverseBindMatrices[i];
            XMFLOAT4X4 f44;
            XMStoreFloat4x4(&f44, m);
            float* dst = &invBindFlat[i * 16];
            dst[0]  = f44._11; dst[1]  = f44._12; dst[2]  = f44._13; dst[3]  = f44._14;
            dst[4]  = f44._21; dst[5]  = f44._22; dst[6]  = f44._23; dst[7]  = f44._24;
            dst[8]  = f44._31; dst[9]  = f44._32; dst[10] = f44._33; dst[11] = f44._34;
            dst[12] = f44._41; dst[13] = f44._42; dst[14] = f44._43; dst[15] = f44._44;
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
void Renderer::LoadModel(const std::vector<MeshData>& meshes,
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
// Apply Pre-computed Frame State
// ============================================================================

void Renderer::ApplyFrameState(uint32_t handle, const FrameState& state, int timeMs) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto* mi = getModel(handle);
    if (!mi) return;

    // Bone matrices → skinning system (with billboard adjustment)
    if (!state.boneWorldMatrices.empty()) {
        int bc = (int)state.boneWorldMatrices.size();
        XMFLOAT3 camPos = camera_.GetSource();
        XMVECTOR camP = XMLoadFloat3(&camPos);
        XMVECTOR worldUp = XMVectorSet(0, 0, 1, 0);

        std::vector<float> worldFlat(bc * 16);
        for (int i = 0; i < bc; i++) {
            XMMATRIX boneM = state.boneWorldMatrices[i];

            uint32_t bbFlags = (i < (int)mi->billboardFlags.size()) ? mi->billboardFlags[i] : 0;
            if (bbFlags != 0) {
                // For a billboarded bone we want vertices to rotate around the
                // bone's animated pivot world-position toward the camera. The
                // delta matrix's translation column is NOT the pivot (it's
                // pivot − R·pivot + t_local), so we instead:
                //   1) transform the rest pivot through the delta to get the
                //      animated world pivot position P_w
                //   2) build boneM = T(-P_rest) · R_cam · T(P_w)
                // which gives (v − P_rest) · R_cam + P_w — i.e. rotate vertex
                // around its rest pivot toward the camera, then place at the
                // animated world pivot. This ignores animated scale for
                // billboards, which matches typical Wc3 usage.
                XMFLOAT3 pivF = (i < (int)mi->nodePivots.size())
                                  ? mi->nodePivots[i] : XMFLOAT3{0, 0, 0};
                XMVECTOR pivRest = XMLoadFloat3(&pivF);
                pivRest = XMVectorSetW(pivRest, 1.0f);
                XMVECTOR pivWorld = XMVector3Transform(pivRest, boneM);

                XMVECTOR toCamera = XMVectorSubtract(camP, pivWorld);
                float dist = XMVectorGetX(XMVector3Length(toCamera));
                if (dist > 0.001f) {
                    XMMATRIX bbRot = XMMatrixIdentity();
                    bool haveRot = false;

                    if (bbFlags & BONE_BILLBOARD_FULL) {
                        // fwd points FROM camera TO bone so the front face faces the viewer
                        XMVECTOR fwd = XMVector3Normalize(XMVectorNegate(toCamera));
                        XMVECTOR right = XMVector3Cross(fwd, worldUp);
                        float rightLen = XMVectorGetX(XMVector3Length(right));
                        if (rightLen < 0.001f)
                            right = XMVectorSet(1, 0, 0, 0);
                        right = XMVector3Normalize(right);
                        XMVECTOR up = XMVector3Normalize(XMVector3Cross(right, fwd));
                        bbRot = XMMATRIX(right, fwd, up, XMVectorSet(0,0,0,1));
                        haveRot = true;
                    } else if (bbFlags & BONE_BILLBOARD_LOCK_Z) {
                        XMFLOAT3 tc; XMStoreFloat3(&tc, toCamera);
                        float yaw = atan2f(tc.y, tc.x);
                        bbRot = XMMatrixRotationZ(yaw);
                        haveRot = true;
                    } else if (bbFlags & BONE_BILLBOARD_LOCK_Y) {
                        XMFLOAT3 tc; XMStoreFloat3(&tc, toCamera);
                        float angle = atan2f(tc.z, tc.x);
                        bbRot = XMMatrixRotationY(angle);
                        haveRot = true;
                    } else if (bbFlags & BONE_BILLBOARD_LOCK_X) {
                        XMFLOAT3 tc; XMStoreFloat3(&tc, toCamera);
                        float angle = atan2f(tc.z, tc.y);
                        bbRot = XMMatrixRotationX(angle);
                        haveRot = true;
                    }

                    if (haveRot) {
                        XMMATRIX T_negRest = XMMatrixTranslation(-pivF.x, -pivF.y, -pivF.z);
                        XMFLOAT3 pwf; XMStoreFloat3(&pwf, pivWorld);
                        XMMATRIX T_world   = XMMatrixTranslation(pwf.x, pwf.y, pwf.z);
                        boneM = T_negRest * bbRot * T_world;
                    }
                }
            }

            XMFLOAT4X4 f44;
            XMStoreFloat4x4(&f44, boneM);
            float* dst = &worldFlat[i * 16];
            dst[0]  = f44._11; dst[1]  = f44._12; dst[2]  = f44._13; dst[3]  = f44._14;
            dst[4]  = f44._21; dst[5]  = f44._22; dst[6]  = f44._23; dst[7]  = f44._24;
            dst[8]  = f44._31; dst[9]  = f44._32; dst[10] = f44._33; dst[11] = f44._34;
            dst[12] = f44._41; dst[13] = f44._42; dst[14] = f44._43; dst[15] = f44._44;
        }
        mi->skinning.UpdateNodeMatrices(bc, worldFlat.data());
    }

    // Geoset world transforms (used for unskinned meshes; skinned ones ignore this)
    for (int i = 0; i < (int)state.geosetTransforms.size() && i < (int)mi->gpuGeosets.size(); i++) {
        mi->gpuGeosets[i].worldMatrix = state.geosetTransforms[i];
    }

    // Geoset visibility
    for (int i = 0; i < (int)state.geosetAlphas.size() && i < (int)mi->gpuGeosets.size(); i++) {
        mi->gpuGeosets[i].geosetAlpha = state.geosetAlphas[i];
    }

    // Geoset colors
    for (int i = 0; i < (int)state.geosetColors.size() && i < (int)mi->gpuGeosets.size(); i++) {
        mi->gpuGeosets[i].geosetColor = state.geosetColors[i];
    }

    // Particle emitter states
    for (auto& ps : state.particleStates) {
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
        mi->particles.UpdateEmitterState(ps.emitterId, st);
    }

    // Ribbon emitter states
    for (auto& rs : state.ribbonStates) {
        RibbonEmitterState st;
        st.transform   = rs.transform;
        st.above       = rs.above;
        st.below       = rs.below;
        st.alpha       = rs.alpha;
        st.color       = rs.color;
        st.visibility  = rs.visibility;
        st.slot        = rs.slot;
        mi->ribbons.UpdateEmitterState(rs.emitterId, st);
    }

    // Collision transforms
    for (int i = 0; i < (int)state.collisionTransforms.size() && i < (int)mi->collisionShapes.size(); i++) {
        mi->collisionShapes[i].transform = state.collisionTransforms[i];
    }

    // Texture animations (per-layer) — clear stale entries from previous frame
    mi->matTexAnim.clear();
    for (auto& ta : state.texAnims) {
        int key = ta.materialId * 1000 + ta.layerIndex;
        mi->matTexAnim[key] = {ta.uOff, ta.vOff, ta.uTile, ta.vTile, ta.rotation};
    }

    // Per-layer alpha animation (KMTA tracks)
    for (auto& la : state.layerAlphas) {
        if (la.materialId >= 0 && la.materialId < (int)mi->gpuMaterials.size()) {
            auto& layers = mi->gpuMaterials[la.materialId].cpu.layers;
            if (la.layerIndex >= 0 && la.layerIndex < (int)layers.size()) {
                layers[la.layerIndex].alpha = la.alpha;
            }
        }
    }

    // Per-layer texture ID animation (KMTF tracks)
    for (auto& lt : state.layerTextureIds) {
        if (lt.materialId >= 0 && lt.materialId < (int)mi->gpuMaterials.size()) {
            auto& layers = mi->gpuMaterials[lt.materialId].cpu.layers;
            if (lt.layerIndex >= 0 && lt.layerIndex < (int)layers.size()) {
                layers[lt.layerIndex].textureId = lt.textureId;
            }
        }
    }

    // PE1 (model particle emitter) states
    for (auto& ps : state.pe1States) {
        PE1EmitterState st;
        st.transform    = ps.transform;
        st.emissionRate = ps.emissionRate;
        st.speed        = ps.speed;
        st.latitude     = ps.latitude;
        st.longitude    = ps.longitude;
        st.gravity      = ps.gravity;
        st.visibility   = ps.visibility;
        mi->pe1.UpdateEmitterState(ps.emitterId, st);
    }

    // Update attachment child model transforms + visibility
    for (auto& as : state.attachmentStates) {
        if (as.attachmentIndex < 0 || as.attachmentIndex >= (int)mi->attachmentSlots.size()) continue;
        auto& slot = mi->attachmentSlots[as.attachmentIndex];
        if (slot.childModelHandle == 0) continue;
        auto* child = getModel(slot.childModelHandle);
        if (!child) continue;

        bool visible = (as.visibility > 0.02f);
        child->worldTransform = as.transform;

        // When becoming visible: pick a new random animation and restart from frame 0
        if (visible && !slot.wasVisible) {
            child->pe1BirthTimeMs = timeMs;
            // Pick a random sequence each time visibility turns on
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

    // Advance simulation clock — only from non-child models
    if (!mi->isPE1Child)
        currentTimeMs_ = timeMs;
}

void Renderer::ApplyFrameState(const FrameState& state, int timeMs) {
    ApplyFrameState(focusModelHandle_, state, timeMs);
}

// ============================================================================
// Team Color Texture Update
// ============================================================================

void Renderer::UpdateTeamColorTextures() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    uint8_t r = GetRValue(teamColor_);
    uint8_t g = GetGValue(teamColor_);
    uint8_t b = GetBValue(teamColor_);
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

void Renderer::SetTeamColor(uint8_t r, uint8_t g, uint8_t b) {
    teamColor_ = RGB(r, g, b);
    UpdateTeamColorTextures();
    if (btnTeamColor_) InvalidateRect(btnTeamColor_, nullptr, TRUE);
}

void Renderer::SetCameraPresets(const std::vector<CameraPreset>& presets) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    pendingCameraPresets_ = presets;
    cameraDirty_ = true;
}

int Renderer::GetActiveCameraIndex() const {
    return cmbCamera_ ? (int)SendMessageW(cmbCamera_, CB_GETCURSEL, 0, 0) : 0;
}

void Renderer::ProcessCameraPresets() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!cameraDirty_ || !cmbCamera_) return;
    SendMessageW(cmbCamera_, CB_RESETCONTENT, 0, 0);
    SendMessageW(cmbCamera_, CB_ADDSTRING, 0, (LPARAM)L"Free Camera");
    for (auto& p : pendingCameraPresets_)
        SendMessageW(cmbCamera_, CB_ADDSTRING, 0, (LPARAM)p.name.c_str());
    cameraPresets_ = std::move(pendingCameraPresets_);
    SendMessageW(cmbCamera_, CB_SETCURSEL, 0, 0);
    cameraLocked_ = false;
    cameraDirty_ = false;
}

void Renderer::SetSequences(const std::vector<std::string>& names) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    pendingSequenceNames_ = names;
    sequencesDirty_ = true;
}

int Renderer::GetActiveSequenceIndex() const {
    return activeSequence_.load();
}

void Renderer::ProcessSequences() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!sequencesDirty_ || !cmbSequence_) return;

    SendMessageW(cmbSequence_, CB_RESETCONTENT, 0, 0);
    for (auto& n : pendingSequenceNames_) {
        std::wstring wn(n.begin(), n.end());
        SendMessageW(cmbSequence_, CB_ADDSTRING, 0, (LPARAM)wn.c_str());
    }
    if (!pendingSequenceNames_.empty()) {
        // Reveal label + combo
        ShowWindow(lblSequence_, SW_SHOW);
        ShowWindow(cmbSequence_, SW_SHOW);
        SendMessageW(cmbSequence_, CB_SETCURSEL, 0, 0);
        activeSequence_ = 0;
    }
    pendingSequenceNames_.clear();
    sequencesDirty_ = false;
}

// ============================================================================
// Staged → GPU Resource Upload (render thread only)
// ============================================================================

void Renderer::ProcessStagedData() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    // Remove models marked for clear
    for (auto it = models_.begin(); it != models_.end(); ) {
        if (it->second->stagedClear) {
            it->second->ReleaseGPU();
            it = models_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (!mi->stagedDirty && !mi->skinDirty) continue;

    if (mi->stagedDirty) {
        // Upload textures (individual Texture2D)
        for (auto& [id, st] : mi->stagedTextures) {
            if (st.width <= 0 || st.height <= 0) continue;

            if (mi->gpuTextures.count(id)) mi->gpuTextures[id].Release();

            GPUTexture gt;
            D3D11_TEXTURE2D_DESC td = {};
            td.Width  = st.width;
            td.Height = st.height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA srd = {};
            srd.pSysMem = st.pixels.data();
            srd.SysMemPitch = st.width * 4;

            if (SUCCEEDED(device_->CreateTexture2D(&td, &srd, &gt.tex))) {
                device_->CreateShaderResourceView(gt.tex, nullptr, &gt.srv);
            }
            gt.wrapFlags = st.wrapFlags;
            mi->gpuTextures[id] = gt;
        }
        mi->stagedTextures.clear();

        // Copy materials (CPU data for render logic)
        for (auto& [id, sm] : mi->stagedMaterials) {
            if ((int)mi->gpuMaterials.size() <= id) mi->gpuMaterials.resize(id + 1);
            mi->gpuMaterials[id].cpu = sm;
        }
        mi->stagedMaterials.clear();

        // Upload geosets
        for (auto& [id, sg] : mi->stagedGeosets) {
            GPUGeoset gg;
            gg.geosetId    = id;
            gg.materialId  = sg.materialId;
            gg.indexCount   = (int)sg.indices.size();
            gg.vertexCount  = (int)sg.vertices.size();
            gg.baseVertices = sg.vertices;  // keep CPU copy for particle/ribbon reads
            gg.hasSkinning  = true; // all MDX geosets are skinned (v1200 weights or v800 vertex groups)

            // Copy priorityPlane from material for render sorting
            if (sg.materialId >= 0 && sg.materialId < (int)mi->gpuMaterials.size())
                gg.priorityPlane = mi->gpuMaterials[sg.materialId].cpu.priorityPlane;

            // ---- GPU compute skinning buffers ----
            UINT vbBytes = (UINT)(sizeof(Vertex) * sg.vertices.size());

            // 1. Base vertex buffer (immutable SRV)
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth           = vbBytes;
            bd.Usage               = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = sizeof(Vertex);
            D3D11_SUBRESOURCE_DATA srd = {};
            srd.pSysMem = sg.vertices.data();
            device_->CreateBuffer(&bd, &srd, &gg.baseVertBuf);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format              = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.NumElements  = gg.vertexCount;
            device_->CreateShaderResourceView(gg.baseVertBuf, &srvd, &gg.baseVertSRV);

            // 2. Weight buffer (immutable SRV) — pack VertexInfluence into uint4 + float4
            struct GPUWeight { uint32_t boneIdx[4]; float weight[4]; };
            static_assert(sizeof(GPUWeight) == 32, "GPUWeight must be 32 bytes");
            std::vector<GPUWeight> gpuWeights(gg.vertexCount);
            const GeosetSkinInfo* skinInfo = mi->skinning.GetGeosetWeights(id);
            if (skinInfo && (int)skinInfo->vertices.size() == gg.vertexCount) {
                for (int v = 0; v < gg.vertexCount; v++) {
                    const auto& inf = skinInfo->vertices[v];
                    for (int j = 0; j < 4; j++) {
                        gpuWeights[v].boneIdx[j] = (uint32_t)inf.boneIdx[j];
                        gpuWeights[v].weight[j]  = inf.weight[j];
                    }
                }
            }

            bd.ByteWidth           = (UINT)(sizeof(GPUWeight) * gg.vertexCount);
            bd.StructureByteStride = sizeof(GPUWeight);
            srd.pSysMem = gpuWeights.data();
            device_->CreateBuffer(&bd, &srd, &gg.weightBuf);

            srvd.Buffer.NumElements = gg.vertexCount;
            device_->CreateShaderResourceView(gg.weightBuf, &srvd, &gg.weightSRV);

            // 3. Skinned output buffer (compute UAV — structured, no VB bind)
            bd.ByteWidth           = vbBytes;
            bd.Usage               = D3D11_USAGE_DEFAULT;
            bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS;
            bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = sizeof(Vertex);
            bd.CPUAccessFlags      = 0;
            device_->CreateBuffer(&bd, nullptr, &gg.skinnedBuf);

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
            uavd.Format             = DXGI_FORMAT_UNKNOWN;
            uavd.ViewDimension      = D3D11_UAV_DIMENSION_BUFFER;
            uavd.Buffer.NumElements = gg.vertexCount;
            device_->CreateUnorderedAccessView(gg.skinnedBuf, &uavd, &gg.skinnedUAV);

            // 4. Vertex buffer for drawing (plain DEFAULT buffer — CopyResource target)
            D3D11_BUFFER_DESC vbDesc = {};
            vbDesc.ByteWidth = vbBytes;
            vbDesc.Usage     = D3D11_USAGE_DEFAULT;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            srd.pSysMem = sg.vertices.data(); // initial data = bind pose
            device_->CreateBuffer(&vbDesc, &srd, &gg.vb);

            // Index buffer (immutable, same for both paths)
            {
                D3D11_BUFFER_DESC bd = {};
                bd.ByteWidth      = (UINT)(sizeof(uint32_t) * sg.indices.size());
                bd.Usage          = D3D11_USAGE_IMMUTABLE;
                bd.BindFlags      = D3D11_BIND_INDEX_BUFFER;
                bd.CPUAccessFlags = 0;
                D3D11_SUBRESOURCE_DATA srd = {};
                srd.pSysMem = sg.indices.data();
                device_->CreateBuffer(&bd, &srd, &gg.ib);
            }

            mi->gpuGeosets.push_back(gg);
        }
        mi->stagedGeosets.clear();
        mi->stagedDirty = false;
    }

    // Phase 4: Update skinning flags + create node palette buffer
    if (mi->skinDirty) {
        for (auto& geo : mi->gpuGeosets)
            geo.hasSkinning = true;

        // Create node palette buffer (DYNAMIC StructuredBuffer<float4x4>)
        int nodeCount = mi->skinning.NodeCount();
        if (nodeCount > 0 && !mi->nodePaletteBuf) {

            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth           = (UINT)(sizeof(XMMATRIX) * nodeCount);
            bd.Usage               = D3D11_USAGE_DYNAMIC;
            bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
            bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
            bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = sizeof(XMMATRIX); // 64 bytes = float4x4
            device_->CreateBuffer(&bd, nullptr, &mi->nodePaletteBuf);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
            srvd.Format              = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.NumElements  = nodeCount;
            device_->CreateShaderResourceView(mi->nodePaletteBuf, &srvd, &mi->nodePaletteSRV);
        }

        mi->skinDirty = false;
    }
    } // end for each model
}

void Renderer::ReleaseModelGPU() {
    for (auto& [h, miPtr] : models_)
        miPtr->ReleaseGPU();
}

// Build a packed Texture2DArray for a material.
// All layer textures are CPU-resized (nearest-neighbor) to the max (w,h) found
// ============================================================================
// Phase 4: Animation Update (render thread)
// ============================================================================

void Renderer::UpdateAnimation() {
    // GPU compute skinning: upload bone palette, dispatch per geoset
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Quick check: any model needs skinning?
    bool anySkinned = false;
    for (auto& [h, miPtr] : models_) {
        if (miPtr->skinning.HasSkeleton() && miPtr->skinning.IsReady() && miPtr->nodePaletteBuf) {
            anySkinned = true;
            break;
        }
    }
    if (!anySkinned) return;

    UINT nullCounts[1] = { (UINT)-1 };
    context_->CSSetShader(skinComputeShader_, nullptr, 0);

    for (auto& [h, miPtr] : models_) {
        auto* mi = miPtr.get();
        if (!mi->skinning.HasSkeleton() || !mi->skinning.IsReady()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent — skip skinning
        if (!mi->nodePaletteBuf) continue;

        mi->skinning.ComputeOffsetMatrices();

        // Upload offset matrices to the node palette buffer
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context_->Map(mi->nodePaletteBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) continue;
        memcpy(mapped.pData, mi->skinning.OffsetMatrices(),
               sizeof(XMMATRIX) * mi->skinning.NodeCount());
        context_->Unmap(mi->nodePaletteBuf, 0);

        // Bind node palette SRV (slot 0) — shared across all geosets of this model
        ID3D11ShaderResourceView* srvs[3] = { mi->nodePaletteSRV, nullptr, nullptr };

        for (auto& geo : mi->gpuGeosets) {
            if (!geo.skinnedUAV) continue;

            // Slots: t0 = BonePalette, t1 = BaseVerts, t2 = Weights
            srvs[1] = geo.baseVertSRV;
            srvs[2] = geo.weightSRV;
            context_->CSSetShaderResources(0, 3, srvs);

            // UAV: u0 = OutVerts
            ID3D11UnorderedAccessView* uavs[1] = { geo.skinnedUAV };
            context_->CSSetUnorderedAccessViews(0, 1, uavs, nullCounts);

            // Dispatch: one thread per vertex, ceil(vertCount / 256)
            UINT groups = (geo.vertexCount + 255) / 256;
            context_->Dispatch(groups, 1, 1);

            // Unbind UAV before CopyResource (avoid resource hazard)
            ID3D11UnorderedAccessView* unbindUAV[1] = { nullptr };
            context_->CSSetUnorderedAccessViews(0, 1, unbindUAV, nullCounts);

            // Copy structured output to plain vertex buffer for drawing
            context_->CopyResource(geo.vb, geo.skinnedBuf);
        }
    }

    // Unbind compute resources to avoid hazards with vertex/pixel stages
    ID3D11ShaderResourceView* nullSRVs[3] = {};
    context_->CSSetShaderResources(0, 3, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[1] = {};
    context_->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullCounts);
    context_->CSSetShader(nullptr, nullptr, 0);
}

// ============================================================================
// Phase 5: Particle Simulation + Rendering (render thread)
// ============================================================================

void Renderer::UpdateParticles(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : models_) {
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        mi->particles.Simulate(dt);
    }
}

void Renderer::RenderParticles() {
    for (auto& [_mh, _mi] : models_) {
    auto* mi = _mi.get();
    std::vector<Vertex> verts;
    std::vector<int> emitterIds;
    std::vector<int> vertCounts;

    // Snapshot all data under one lock
    float pitch, yaw;
    XMMATRIX viewMat;
    std::vector<ParticleEmitterConfig> configs;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!mi->particles.HasEmitters()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        pitch   = camera_.GetPitch();
        yaw     = camera_.GetYaw();
        viewMat = camera_.GetViewMatrix();
        mi->particles.BuildBillboards(pitch, yaw, verts, emitterIds, vertCounts);

        for (int eid : emitterIds) {
            auto* c = mi->particles.GetConfig(eid);
            configs.push_back(c ? *c : ParticleEmitterConfig{});
        }
    }
    // Lock released — safe to call DX11

    if (verts.empty()) continue;

    int vertCount = (int)verts.size();

    // Grow particle VB if needed
    if (!mi->particleVB || vertCount > mi->particleVBSize) {
        SafeRelease(mi->particleVB);
        int newSize = (std::max)(vertCount, 1024);
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = (UINT)(sizeof(Vertex) * newSize);
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&bd, nullptr, &mi->particleVB);
        mi->particleVBSize = newSize;
    }

    // Upload vertex data
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context_->Map(mi->particleVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        continue;
    memcpy(mapped.pData, verts.data(), sizeof(Vertex) * vertCount);
    context_->Unmap(mi->particleVB, 0);

    // Bind particle VB
    UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, &mi->particleVB, &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set main shader + input layout
    context_->IASetInputLayout(inputLayout_);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);

    // Particles are always two-sided (billboards can face either way)
    context_->RSSetState(rsNoCull_);

    // Render per-emitter with correct blend state and texture
    int drawOffset = 0;
    for (int ei = 0; ei < (int)emitterIds.size(); ei++) {
        auto& cfg = configs[ei];
        int count = vertCounts[ei];
        if (count <= 0) { continue; }

        // Apply filter mode blend state (pass MAT_TWO_SIDED — particles are always two-sided)
        ApplyFilterMode(cfg.filterMode, MAT_TWO_SIDED);

        // Force depth write off for particles
        context_->OMSetDepthStencilState(dsNoWrite_, 0);

        // Update constant buffer
        {
            float alphaRef = (cfg.filterMode == FILTER_TRANSPARENT) ? 0.75f : 0.0f;
            D3D11_MAPPED_SUBRESOURCE cbMapped;
            context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped);
            CBPerFrame* cb = (CBPerFrame*)cbMapped.pData;
            cb->world      = XMMatrixTranspose(XMMatrixIdentity());

            float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            XMMATRIX proj = XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect, 1.0f, 10000.0f);
            cb->view       = XMMatrixTranspose(viewMat);
            cb->projection = XMMatrixTranspose(proj);

            XMVECTOR ld = XMVector3Normalize(XMVectorSet(0.0f, -0.3f, -0.8f, 0.0f));
            XMStoreFloat4(&cb->lightDir, ld);
            cb->lightColor   = {0.85f,0.85f,0.80f,1};
            cb->ambientColor = {0.35f,0.35f,0.40f,alphaRef};
            cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
            cb->materialFlags = {cfg.unshaded ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            context_->Unmap(cbPerFrame_, 0);
        }

        // Bind texture
        ID3D11ShaderResourceView* srv = defaultTexSRV_;
        uint32_t wrapFlags = 0x3;
        if (cfg.textureId >= 0 && mi->gpuTextures.count(cfg.textureId)) {
            auto& gt = mi->gpuTextures[cfg.textureId];
            srv = gt.srv;
            wrapFlags = gt.wrapFlags & 0x3;
        }
        if (!srv) srv = defaultTexSRV_;
        context_->PSSetShaderResources(0, 1, &srv);
        context_->PSSetSamplers(0, 1, &samplerWrap_[wrapFlags]);

        // Draw this emitter's particles
        context_->Draw(count, drawOffset);
        drawOffset += count;
    }
    } // end for each model
}

// ============================================================================
// Phase 5b: Ribbon Simulation + Rendering (render thread)
// ============================================================================

void Renderer::UpdateRibbons(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (auto& [h, mi] : models_) {
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        mi->ribbons.Simulate(dt);
    }
}

void Renderer::RenderRibbons() {
    for (auto& [_mh, _mi] : models_) {
    auto* mi = _mi.get();
    std::vector<Vertex> verts;
    std::vector<int> emitterIds;
    XMMATRIX viewMat;
    std::vector<RibbonEmitterConfig> configs;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!mi->ribbons.HasEmitters()) continue;
        if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
        viewMat = camera_.GetViewMatrix();
        mi->ribbons.BuildStrips(verts, emitterIds);
        for (int eid : emitterIds) {
            auto* c = mi->ribbons.GetConfig(eid);
            configs.push_back(c ? *c : RibbonEmitterConfig{});
        }
    }

    if (verts.empty()) continue;
    int vertCount = (int)verts.size();

    // Grow ribbon VB if needed
    if (!mi->ribbonVB || vertCount > mi->ribbonVBSize) {
        SafeRelease(mi->ribbonVB);
        int newSize = (std::max)(vertCount, 512);
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = (UINT)(sizeof(Vertex) * newSize);
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&bd, nullptr, &mi->ribbonVB);
        mi->ribbonVBSize = newSize;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context_->Map(mi->ribbonVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        continue;
    memcpy(mapped.pData, verts.data(), sizeof(Vertex) * vertCount);
    context_->Unmap(mi->ribbonVB, 0);

    UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, &mi->ribbonVB, &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->IASetInputLayout(inputLayout_);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);

    // Two-sided: disable backface culling
    context_->RSSetState(rsNoCull_);

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

        ApplyFilterMode(cfg.filterMode, 0);
        context_->OMSetDepthStencilState(dsNoWrite_, 0);
        if (cfg.twoSided) context_->RSSetState(rsNoCull_);

        {
            float alphaRef = (cfg.filterMode == FILTER_TRANSPARENT) ? 0.75f : 0.0f;
            D3D11_MAPPED_SUBRESOURCE cbMapped;
            context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped);
            CBPerFrame* cb = (CBPerFrame*)cbMapped.pData;
            cb->world      = XMMatrixTranspose(XMMatrixIdentity());
            float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            XMMATRIX proj = XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect, 1.0f, 10000.0f);
            cb->view       = XMMatrixTranspose(viewMat);
            cb->projection = XMMatrixTranspose(proj);
            XMVECTOR ld = XMVector3Normalize(XMVectorSet(0.0f, -0.3f, -0.8f, 0.0f));
            XMStoreFloat4(&cb->lightDir, ld);
            cb->lightColor   = {0.85f,0.85f,0.80f,1};
            cb->ambientColor = {0.35f,0.35f,0.40f,alphaRef};
            cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
            cb->materialFlags = {cfg.unshaded ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            context_->Unmap(cbPerFrame_, 0);
        }

        // Bind texture
        ID3D11ShaderResourceView* srv = defaultTexSRV_;
        uint32_t wrapFlags = 0x3;
        if (cfg.textureId >= 0 && mi->gpuTextures.count(cfg.textureId)) {
            auto& gt = mi->gpuTextures[cfg.textureId];
            srv = gt.srv;
            wrapFlags = gt.wrapFlags & 0x3;
        }
        if (!srv) srv = defaultTexSRV_;
        context_->PSSetShaderResources(0, 1, &srv);
        context_->PSSetSamplers(0, 1, &samplerWrap_[wrapFlags]);

        context_->Draw(count, drawOffset);
        drawOffset += count;
    }

    // Restore default rasterizer
    context_->RSSetState(rsDefault_);
    } // end for each model
}

// ============================================================================
// Collision Shape Wireframe Rendering
// ============================================================================

void Renderer::RenderCollisions() {
    std::vector<CollisionShape> shapes;
    XMMATRIX viewMat;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (auto& [h, mi] : models_) {
            if (mi->parentVisibility <= 0.02f) continue;  // hidden by parent
            shapes.insert(shapes.end(), mi->collisionShapes.begin(), mi->collisionShapes.end());
        }
        if (shapes.empty()) return;
        viewMat = camera_.GetViewMatrix();
    }

    // Use line shader
    context_->IASetInputLayout(lineInputLayout_);
    context_->VSSetShader(lineVertexShader_, nullptr, 0);
    context_->PSSetShader(linePixelShader_, nullptr, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

    float blend[] = {0,0,0,0};
    context_->OMSetBlendState(bsOpaque_, blend, 0xFFFFFFFF);
    context_->OMSetDepthStencilState(dsDefault_, 0);

    // Line color: green for collision shapes
    XMFLOAT4 col = {0.0f, 1.0f, 0.3f, 1.0f};

    for (auto& cs : shapes) {
        // Build line vertices in local space, then transform
        struct LV { XMFLOAT3 pos; XMFLOAT4 col; };
        std::vector<LV> lines;

        if (cs.type == 0) {
            // Box: 12 edges
            XMFLOAT3 mn = cs.vmin, mx = cs.vmax;
            XMFLOAT3 corners[8] = {
                {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z},
                {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z}
            };
            int edges[24] = {0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7};
            for (int i = 0; i < 24; i += 2) {
                XMVECTOR a = XMVector3Transform(XMLoadFloat3(&corners[edges[i]]), cs.transform);
                XMVECTOR b = XMVector3Transform(XMLoadFloat3(&corners[edges[i+1]]), cs.transform);
                XMFLOAT3 pa, pb; XMStoreFloat3(&pa, a); XMStoreFloat3(&pb, b);
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
                    XMFLOAT3 p0, p1;
                    float c0 = cs.radius * cosf(a0), s0 = cs.radius * sinf(a0);
                    float c1 = cs.radius * cosf(a1), s1 = cs.radius * sinf(a1);
                    if (plane == 0)      { p0 = {cs.vmin.x+c0, cs.vmin.y+s0, cs.vmin.z}; p1 = {cs.vmin.x+c1, cs.vmin.y+s1, cs.vmin.z}; }
                    else if (plane == 1) { p0 = {cs.vmin.x+c0, cs.vmin.y, cs.vmin.z+s0}; p1 = {cs.vmin.x+c1, cs.vmin.y, cs.vmin.z+s1}; }
                    else                 { p0 = {cs.vmin.x, cs.vmin.y+c0, cs.vmin.z+s0}; p1 = {cs.vmin.x, cs.vmin.y+c1, cs.vmin.z+s1}; }
                    XMVECTOR va = XMVector3Transform(XMLoadFloat3(&p0), cs.transform);
                    XMVECTOR vb = XMVector3Transform(XMLoadFloat3(&p1), cs.transform);
                    XMFLOAT3 pa, pb; XMStoreFloat3(&pa, va); XMStoreFloat3(&pb, vb);
                    lines.push_back({pa, col});
                    lines.push_back({pb, col});
                }
            }
        }

        if (lines.empty()) continue;

        // Upload to a temp dynamic buffer (reuse gridVB_ pattern)
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (UINT)(sizeof(LV) * lines.size());
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* tempVB = nullptr;
        device_->CreateBuffer(&bd, nullptr, &tempVB);
        if (!tempVB) continue;

        D3D11_MAPPED_SUBRESOURCE mapped;
        context_->Map(tempVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, lines.data(), sizeof(LV) * lines.size());
        context_->Unmap(tempVB, 0);

        // Update CB with identity world
        {
            D3D11_MAPPED_SUBRESOURCE cbm;
            context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbm);
            CBPerFrame* cb = (CBPerFrame*)cbm.pData;
            cb->world = XMMatrixTranspose(XMMatrixIdentity());
            float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
            cb->view = XMMatrixTranspose(viewMat);
            cb->projection = XMMatrixTranspose(XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect, 1.0f, 10000.0f));
            cb->lightDir = {0,0,0,0};
            cb->lightColor = {1,1,1,1};
            cb->ambientColor = {1,1,1,0};
            cb->extraParams = {1,1,1,1};
        cb->texAnimParams = {0,0,1,1};
            cb->materialFlags = {0,0,0,0};
            context_->Unmap(cbPerFrame_, 0);
        }

        UINT stride = sizeof(LV), off = 0;
        context_->IASetVertexBuffers(0, 1, &tempVB, &stride, &off);
        context_->Draw((UINT)lines.size(), 0);
        SafeRelease(tempVB);
    }
}

// ============================================================================
// Render Thread
// ============================================================================

void Renderer::RenderThread(int width, int height) {
    width_ = width;
    height_ = height;

    if (!CreateRenderWindow(width, height)) { running_ = false; return; }
    if (!InitD3D())                         { running_ = false; return; }
    if (!CreateShaders())                   { running_ = false; return; }
    if (!CreateDefaultResources())          { running_ = false; return; }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    initialized_ = true;

    MSG msg = {};
    LARGE_INTEGER freq, lastTime, now, fpsTimer;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);
    fpsTimer = lastTime;
    const double targetDt = 1.0 / 60.0;
    int frameCount = 0;
    int lastParentTimeMs = currentTimeMs_.load();

    while (running_) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_ = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running_) break;

        QueryPerformanceCounter(&now);
        double elapsed = (double)(now.QuadPart - lastTime.QuadPart) / freq.QuadPart;
        if (elapsed < targetDt) {
            DWORD sleepMs = (DWORD)((targetDt - elapsed) * 1000.0);
            if (sleepMs > 1) Sleep(sleepMs - 1);
            continue;
        }
        lastTime = now;

        // Particle/ribbon/PE1 simulation dt is derived from the parent's
        // animation clock so everything stays locked to Max's timeline:
        // when Max is paused or scrubbing, sims freeze; when playing, sims
        // advance at exactly the parent's playback rate.
        int curParentMs = currentTimeMs_.load();
        int parentDtMs  = curParentMs - lastParentTimeMs;
        if (parentDtMs < 0)   parentDtMs = 0;     // backward scrub → freeze
        if (parentDtMs > 100) parentDtMs = 100;   // clamp big jumps
        lastParentTimeMs = curParentMs;
        float parentDt = (float)parentDtMs / 1000.0f;

        // Process any staged data from API thread
        ProcessStagedData();

        // Process pending camera preset updates
        ProcessCameraPresets();
        ProcessSequences();

        // Load attachment child models (lazy, first frame only)
        UpdateAttachments();

        // PE1: Evaluate child model animations
        EvaluatePE1Children();

        // Phase 4: Skin vertices with current bone matrices
        UpdateAnimation();

        // Phase 5: Simulate particles (parent-clock dt)
        UpdateParticles(parentDt);
        UpdatePE1(parentDt);
        UpdateRibbons(parentDt);

        RenderFrame();
        frameCount++;

        double fpsDt = (double)(now.QuadPart - fpsTimer.QuadPart) / freq.QuadPart;
        if (fpsDt >= 1.0) {
            int nGeo = 0, nTex = 0, nNodes = 0, nParts = 0, nSegs = 0;
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                for (auto& [h, mi] : models_) {
                    nGeo += (int)mi->gpuGeosets.size();
                    nTex += (int)mi->gpuTextures.size();
                    nNodes += mi->skinning.NodeCount();
                    nParts += mi->particles.GetTotalParticleCount();
                    nSegs += mi->ribbons.GetTotalSegmentCount();
                }
            }
            wchar_t title[300];
            swprintf_s(title,
                L"Whiteout Renderer \u2014 %d FPS | %d geo, %d tex, %d nodes, %d parts, %d segs",
                frameCount, nGeo, nTex, nNodes, nParts, nSegs
            );
            SetWindowTextW(hwnd_, title);
            frameCount = 0;
            fpsTimer = now;
        }
    }

    ReleaseModelGPU();
    CleanupD3D();
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    if (icon_) { DestroyIcon(icon_); icon_ = nullptr; }
    UnregisterClassW(WINDOW_CLASS, GetModuleHandle(nullptr));
    initialized_ = false;
}

// ============================================================================
// Win32 Window
// ============================================================================

bool Renderer::CreateRenderWindow(int w, int h) {
    // Get HINSTANCE of the module containing this code (DLL or EXE)
    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCWSTR)&Renderer::WndProc, &hMod);
    HINSTANCE hInst = hMod ? (HINSTANCE)hMod : GetModuleHandle(nullptr);

    // Load icon from embedded resource
    icon_ = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_WHITEOUT_ICON));

    // Register parent window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = Renderer::WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon          = icon_;
    wc.hIconSm        = icon_;
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName  = WINDOW_CLASS;
    if (!RegisterClassExW(&wc))
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Register child (render surface) window class
    static const wchar_t* RENDER_CLASS = L"WhiteoutDexRenderSurface";
    WNDCLASSEXW rc = {};
    rc.cbSize        = sizeof(rc);
    rc.style         = CS_HREDRAW | CS_VREDRAW;
    rc.lpfnWndProc   = Renderer::RenderWndProc;
    rc.hInstance      = hInst;
    rc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    rc.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
    rc.lpszClassName  = RENDER_CLASS;
    if (!RegisterClassExW(&rc))
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Create parent window
    RECT adj = {0, 0, w, h + kToolbarH};
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST, WINDOW_CLASS, WINDOW_TITLE, WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            adj.right - adj.left, adj.bottom - adj.top,
                            nullptr, nullptr, hInst, this);
    if (!hwnd_) return false;

    // Create DX11 render child window (below toolbar)
    hwndRender_ = CreateWindowExW(0, RENDER_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                   0, kToolbarH, w, h,
                                   hwnd_, nullptr, hInst, this);
    if (!hwndRender_) return false;

    // Create toolbar controls in parent window
    int x = 8;
    auto mkChk = [&](const wchar_t* label, int id, bool checked) -> HWND {
        int labelW = (int)wcslen(label) * 7 + 28;
        HWND ctl = CreateWindowW(L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            x, 4, labelW, 20, hwnd_, (HMENU)(INT_PTR)id, hInst, nullptr);
        if (ctl && checked) SendMessage(ctl, BM_SETCHECK, BST_CHECKED, 0);
        x += labelW + 8;
        return ctl;
    };
    chkGrid_       = mkChk(L"Grid",       IDC_GRID,       showGrid_);
    chkParticles_  = mkChk(L"Particles",  IDC_PARTICLES,  showParticles_);
    chkRibbons_    = mkChk(L"Ribbons",    IDC_RIBBONS,    showRibbons_);
    chkCollisions_ = mkChk(L"Collisions", IDC_COLLISIONS, showCollisions_);

    // Separator
    x += 4;

    // Team color label + swatch
    CreateWindowW(L"STATIC", L"Team:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        x, 4, 36, 20, hwnd_, nullptr, hInst, nullptr);
    x += 38;
    btnTeamColor_ = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, 4, 22, 20, hwnd_, (HMENU)(INT_PTR)IDC_TEAMCOLOR, hInst, nullptr);
    x += 30;

    // Camera combo box
    CreateWindowW(L"STATIC", L"Camera:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        x, 4, 50, 20, hwnd_, nullptr, hInst, nullptr);
    x += 52;
    cmbCamera_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, 2, 150, 200, hwnd_, (HMENU)(INT_PTR)IDC_CAMERA, hInst, nullptr);
    SendMessageW(cmbCamera_, CB_ADDSTRING, 0, (LPARAM)L"Free Camera");
    SendMessageW(cmbCamera_, CB_SETCURSEL, 0, 0);
    x += 158;

    // Sequence combo box (hidden until SetSequences() is called — standalone viewer)
    lblSequence_ = CreateWindowW(L"STATIC", L"Animation:",
        WS_CHILD | SS_CENTERIMAGE,
        x, 4, 64, 20, hwnd_, nullptr, hInst, nullptr);
    x += 66;
    cmbSequence_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, 2, 180, 300, hwnd_, (HMENU)(INT_PTR)IDC_SEQUENCE, hInst, nullptr);

    return true;
}

// Child (DX11 render surface) WndProc — forwards mouse input
LRESULT CALLBACK Renderer::RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Renderer* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<Renderer*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Renderer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    // Forward mouse messages to parent WndProc logic
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_MOUSEMOVE:   case WM_MOUSEWHEEL:
        return Renderer::WndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK Renderer::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Renderer* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<Renderer*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Renderer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Renderer::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        int renderH = h - kToolbarH;
        if (w > 0 && renderH > 0) {
            // Resize child render window
            if (hwndRender_) MoveWindow(hwndRender_, 0, kToolbarH, w, renderH, TRUE);
            if (device_) { width_ = w; height_ = renderH; ResizeBuffers(w, renderH); }
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        int vcHit = HitTestViewCube(mx, my);
        if (vcHit >= 0) {
            std::lock_guard<std::mutex> lock(dataMutex_);
            if (vcHit == 6) camera_.Reset();       // Home
            else SnapCameraToFace(vcHit);           // Face click
            return 0;
        }
        lmbDown_ = true;
        lastMouse_ = {mx, my};
        SetCapture(hwnd); return 0;
    }
    case WM_LBUTTONUP:
        lmbDown_ = false;
        if (!rmbDown_ && !mmbDown_) ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN:
        rmbDown_ = true;
        lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        SetCapture(hwnd); return 0;
    case WM_RBUTTONUP:
        rmbDown_ = false;
        if (!lmbDown_ && !mmbDown_) ReleaseCapture(); return 0;
    case WM_MBUTTONDOWN:
        mmbDown_ = true;
        lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        SetCapture(hwnd); return 0;
    case WM_MBUTTONUP:
        mmbDown_ = false;
        if (!lmbDown_ && !rmbDown_) ReleaseCapture(); return 0;
    case WM_MOUSEMOVE: {
        POINT cur = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int dx = cur.x - lastMouse_.x, dy = cur.y - lastMouse_.y;
        lastMouse_ = cur;
        // Track ViewCube hover
        RECT vcr = GetViewCubeRect();
        vcHovered_ = (cur.x >= vcr.left && cur.x <= vcr.right && cur.y >= vcr.top && cur.y <= vcr.bottom);
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!cameraLocked_) {
            if (lmbDown_) camera_.Rotate(dx, dy);   // inverted Y like Magos
            if (rmbDown_) camera_.Pan(-dx, dy);
            if (mmbDown_) camera_.ZoomSmooth((float)dy * camera_.GetDistance() / Camera::kFactorRelDist);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!cameraLocked_) {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            std::lock_guard<std::mutex> lock(dataMutex_);
            camera_.Zoom(delta * 30);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_TEAMCOLOR) {
            HBRUSH brush = CreateSolidBrush(teamColor_);
            FillRect(dis->hDC, &dis->rcItem, brush);
            DeleteObject(brush);
            DrawEdge(dis->hDC, &dis->rcItem, EDGE_SUNKEN, BF_RECT);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        switch (id) {
            case IDC_GRID:       showGrid_       = (SendMessage(chkGrid_, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
            case IDC_PARTICLES:  showParticles_  = (SendMessage(chkParticles_, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
            case IDC_RIBBONS:    showRibbons_    = (SendMessage(chkRibbons_, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
            case IDC_COLLISIONS: showCollisions_ = (SendMessage(chkCollisions_, BM_GETCHECK, 0, 0) == BST_CHECKED); break;
            case IDC_TEAMCOLOR: {
                CHOOSECOLORW cc = {};
                static COLORREF customColors[16] = {};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd_;
                cc.rgbResult = teamColor_;
                cc.lpCustColors = customColors;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    teamColor_ = cc.rgbResult;
                    UpdateTeamColorTextures();
                    InvalidateRect(btnTeamColor_, nullptr, TRUE);
                }
                break;
            }
            case IDC_CAMERA: {
                if (code == CBN_SELCHANGE) {
                    int sel = (int)SendMessageW(cmbCamera_, CB_GETCURSEL, 0, 0);
                    if (sel == 0) {
                        // Free Camera — no action, user controls camera
                        cameraLocked_ = false;
                    } else {
                        int idx = sel - 1;
                        std::lock_guard<std::mutex> lock(dataMutex_);
                        if (idx >= 0 && idx < (int)cameraPresets_.size()) {
                            auto& p = cameraPresets_[idx];
                            camera_.SetPitch(p.pitch);
                            camera_.SetYaw(p.yaw);
                            camera_.SetDistance(p.distance);
                            camera_.SetTarget(p.target.x, p.target.y, p.target.z);
                            cameraLocked_ = p.isLive;
                        }
                    }
                }
                break;
            }
            case IDC_SEQUENCE: {
                if (code == CBN_SELCHANGE) {
                    int sel = (int)SendMessageW(cmbSequence_, CB_GETCURSEL, 0, 0);
                    if (sel >= 0) activeSequence_ = sel;
                }
                break;
            }
        }
        return 0;
    }
    case WM_DESTROY:
        hwnd_ = nullptr;  // prevent double DestroyWindow in thread cleanup
        running_ = false; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// DirectX 11 Initialization
// ============================================================================

bool Renderer::InitD3D() {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = width_;
    scd.BufferDesc.Height = height_;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate = {60, 1};
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwndRender_;
    scd.SampleDesc = {1, 0};
    scd.Windowed = TRUE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 1, D3D11_SDK_VERSION,
        &scd, &swapChain_, &device_, &featureLevel, &context_);
    if (FAILED(hr)) return false;
    if (!ResizeBuffers(width_, height_)) return false;

    // Rasterizer states
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_BACK;
        rd.FrontCounterClockwise = TRUE;  // Max uses CCW winding
        rd.DepthClipEnable = TRUE;
        rd.AntialiasedLineEnable = TRUE;
        device_->CreateRasterizerState(&rd, &rsDefault_);
        rd.CullMode = D3D11_CULL_NONE;
        device_->CreateRasterizerState(&rd, &rsNoCull_);
    }

    // Depth stencil states
    {
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;  // WC3 uses LEQUAL
        device_->CreateDepthStencilState(&dd, &dsDefault_);
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        device_->CreateDepthStencilState(&dd, &dsNoWrite_);
        dd.DepthEnable = FALSE;
        device_->CreateDepthStencilState(&dd, &dsDisabled_);
    }

    // Blend states — one per FilterMode (ported from Magos ModelMaterialLayer::UseMaterial)
    {
        D3D11_BLEND_DESC bd = {};
        auto& rt = bd.RenderTarget[0];
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        // Opaque (FILTER_NONE)
        rt.BlendEnable = FALSE;
        device_->CreateBlendState(&bd, &bsOpaque_);

        // Alpha test (FILTER_TRANSPARENT) — uses alpha blend + discard in shader
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_ZERO;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        device_->CreateBlendState(&bd, &bsAlphaTest_);

        // Alpha blend (FILTER_BLEND)
        // Same as alpha test but with depth write off (handled in render)
        device_->CreateBlendState(&bd, &bsAlphaBlend_);

        // Additive (FILTER_ADDITIVE) — SrcAlpha + One
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_ONE;
        device_->CreateBlendState(&bd, &bsAdditive_);

        // Add Alpha (FILTER_ADD_ALPHA) — SrcAlpha + One
        // out = src.rgb * src.a + dst. Alpha channel controls glow intensity.
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_ONE;
        device_->CreateBlendState(&bd, &bsAddAlpha_);

        // Modulate (FILTER_MODULATE) — Magos: Zero + SrcColor
        rt.SrcBlend = D3D11_BLEND_ZERO;
        rt.DestBlend = D3D11_BLEND_SRC_COLOR;
        device_->CreateBlendState(&bd, &bsModulate_);

        // Modulate 2x (FILTER_MODULATE_2X) — out = 2 * src * dst
        // via DestColor + SrcColor → src*dst + dst*src = 2*src*dst
        rt.SrcBlend = D3D11_BLEND_DEST_COLOR;
        rt.DestBlend = D3D11_BLEND_SRC_COLOR;
        device_->CreateBlendState(&bd, &bsModulate2x_);
    }

    // Sampler
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device_->CreateSamplerState(&sd, &samplerLinear_);

        // Per-texture wrap mode variants: index = wrapFlags (bit0=U, bit1=V)
        D3D11_TEXTURE_ADDRESS_MODE modes[2] = {
            D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP
        };
        for (int i = 0; i < 4; i++) {
            sd.AddressU = modes[(i >> 0) & 1];  // bit 0 = WrapWidth (U)
            sd.AddressV = modes[(i >> 1) & 1];  // bit 1 = WrapHeight (V)
            sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            device_->CreateSamplerState(&sd, &samplerWrap_[i]);
        }
    }

    // Constant buffers
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(CBPerFrame);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&bd, nullptr, &cbPerFrame_);
    }

    // 1x1 white default texture
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = td.Height = 1;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        UINT32 white = 0xFFFFFFFF;
        D3D11_SUBRESOURCE_DATA srd = {&white, 4, 0};
        device_->CreateTexture2D(&td, &srd, &defaultTex_);
        device_->CreateShaderResourceView(defaultTex_, nullptr, &defaultTexSRV_);
    }

    return true;
}

bool Renderer::ResizeBuffers(int w, int h) {
    if (!device_ || !swapChain_) return false;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    SafeRelease(rtv_); SafeRelease(dsv_); SafeRelease(depthBuffer_);

    swapChain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (!backBuffer) return false;
    device_->CreateRenderTargetView(backBuffer, nullptr, &rtv_);
    SafeRelease(backBuffer);

    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = w; dd.Height = h;
    dd.MipLevels = dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc = {1, 0};
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    device_->CreateTexture2D(&dd, nullptr, &depthBuffer_);
    device_->CreateDepthStencilView(depthBuffer_, nullptr, &dsv_);
    width_ = w; height_ = h;
    return true;
}

void Renderer::CleanupD3D() {
    if (context_) context_->ClearState();
    SafeRelease(defaultTexSRV_); SafeRelease(defaultTex_);
    SafeRelease(samplerLinear_);
    for (auto& s : samplerWrap_) SafeRelease(s);
    SafeRelease(bsModulate2x_); SafeRelease(bsModulate_); SafeRelease(bsAddAlpha_); SafeRelease(bsAdditive_);
    SafeRelease(bsAlphaBlend_); SafeRelease(bsAlphaTest_); SafeRelease(bsOpaque_);
    SafeRelease(dsDisabled_); SafeRelease(dsNoWrite_); SafeRelease(dsDefault_);
    SafeRelease(rsNoCull_); SafeRelease(rsDefault_);
    SafeRelease(cbPerFrame_);
    SafeRelease(gridVB_);
    SafeRelease(vcCubeVB_); SafeRelease(vcCubeIB_); SafeRelease(vcOutlineVB_);
    SafeRelease(vcFaceTexSRV_); SafeRelease(vcFaceTex_);
    SafeRelease(lineInputLayout_); SafeRelease(linePixelShader_); SafeRelease(lineVertexShader_);
    SafeRelease(skinComputeShader_);
    SafeRelease(inputLayout_);
    SafeRelease(pixelShader_); SafeRelease(vertexShader_);
    SafeRelease(dsv_); SafeRelease(depthBuffer_); SafeRelease(rtv_);
    SafeRelease(swapChain_); SafeRelease(context_); SafeRelease(device_);
}

// ============================================================================
// Shaders (same as Phase 1)
// ============================================================================

static ID3DBlob* CompileShader(const char* src, const char* entry, const char* target) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                            entry, target, flags, 0, &blob, &errors);
    if (FAILED(hr)) {
        if (errors) { OutputDebugStringA((char*)errors->GetBufferPointer()); errors->Release(); }
        return nullptr;
    }
    SafeRelease(errors);
    return blob;
}

bool Renderer::CreateShaders() {
    // Mesh shader (VS + particle/ribbon PS)
    {
        ID3DBlob* vs = CompileShader(g_vertexShaderSrc, "VSMain", "vs_5_0");
        ID3DBlob* ps = CompileShader(g_pixelShaderSrc,  "PSMain", "ps_5_0");
        if (!vs || !ps) return false;
        device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader_);
        device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader_);
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        device_->CreateInputLayout(layout, 4, vs->GetBufferPointer(), vs->GetBufferSize(), &inputLayout_);
        vs->Release(); ps->Release();
    }
    // Line shader
    {
        ID3DBlob* vs = CompileShader(g_lineVertexShaderSrc, "VSLine", "vs_5_0");
        ID3DBlob* ps = CompileShader(g_linePixelShaderSrc,  "PSLine", "ps_5_0");
        if (!vs || !ps) return false;
        device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &lineVertexShader_);
        device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &linePixelShader_);
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        device_->CreateInputLayout(layout, 2, vs->GetBufferPointer(), vs->GetBufferSize(), &lineInputLayout_);
        vs->Release(); ps->Release();
    }
    // Compute shader: GPU vertex skinning
    {
        ID3DBlob* cs = CompileShader(g_skinComputeShaderSrc, "CSSkin", "cs_5_0");
        if (!cs) return false;
        device_->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &skinComputeShader_);
        cs->Release();
    }
    return true;
}

// ============================================================================
// Default Resources (Grid only — test triangle removed in Phase 2)
// ============================================================================

bool Renderer::CreateDefaultResources() {
    std::vector<LineVertex> lines;
    const float extent = 500.0f;
    const float step   = 50.0f;
    XMFLOAT4 gridColor  = {0.45f, 0.45f, 0.46f, 1.0f};  // subtle, close to background
    XMFLOAT4 axisColorX = {0.75f, 0.2f,  0.2f,  1.0f};
    XMFLOAT4 axisColorY = {0.2f,  0.75f, 0.2f,  1.0f};
    XMFLOAT4 axisColorZ = {0.2f,  0.2f,  0.75f, 1.0f};

    for (float v = -extent; v <= extent; v += step) {
        XMFLOAT4 c = (v == 0.0f) ? axisColorY : gridColor;
        lines.push_back({{v, -extent, 0.0f}, c});
        lines.push_back({{v,  extent, 0.0f}, c});
        c = (v == 0.0f) ? axisColorX : gridColor;
        lines.push_back({{-extent, v, 0.0f}, c});
        lines.push_back({{ extent, v, 0.0f}, c});
    }
    lines.push_back({{0.0f, 0.0f, 0.0f},   axisColorZ});
    lines.push_back({{0.0f, 0.0f, extent},  axisColorZ});
    gridVertCount_ = (int)lines.size();

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(LineVertex) * lines.size());
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA srd = {lines.data()};
    device_->CreateBuffer(&bd, &srd, &gridVB_);

    CreateViewCube();
    return true;
}

// ============================================================================
// FilterMode Application (ported from Magos ModelMaterialLayer::UseMaterial)
// ============================================================================

void Renderer::ApplyFilterMode(int filterMode, int matFlags) {
    bool twoSided    = (matFlags & MAT_TWO_SIDED) != 0;
    bool unshaded    = (matFlags & MAT_UNSHADED) != 0;
    bool noDepthTest = (matFlags & MAT_NO_DEPTH_TEST) != 0;
    bool noDepthSet  = (matFlags & MAT_NO_DEPTH_SET) != 0;

    // Rasterizer (cull mode)
    context_->RSSetState(twoSided ? rsNoCull_ : rsDefault_);

    // Blend + Depth per FilterMode (exact Magos mapping)
    float blend[4] = {0, 0, 0, 0};
    switch (filterMode) {
    case FILTER_NONE:
        context_->OMSetBlendState(bsOpaque_, blend, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(dsDefault_, 0);
        break;
    case FILTER_TRANSPARENT:
        context_->OMSetBlendState(bsAlphaTest_, blend, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(dsDefault_, 0);
        break;
    case FILTER_BLEND:
        context_->OMSetBlendState(bsAlphaBlend_, blend, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(dsNoWrite_, 0);
        break;
    case FILTER_ADDITIVE:
        context_->OMSetBlendState(bsAdditive_, blend, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(dsNoWrite_, 0);
        break;
    case FILTER_ADD_ALPHA:
        context_->OMSetBlendState(bsAddAlpha_, blend, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(dsNoWrite_, 0);
        break;
    case FILTER_MODULATE:
        context_->OMSetBlendState(bsModulate_, blend, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(dsNoWrite_, 0);
        break;
    case FILTER_MODULATE_2X:
        context_->OMSetBlendState(bsModulate2x_, blend, 0xFFFFFFFF);
        context_->OMSetDepthStencilState(dsNoWrite_, 0);
        break;
    }

    // Override depth if material flags say so
    if (noDepthTest) context_->OMSetDepthStencilState(dsDisabled_, 0);
    else if (noDepthSet) context_->OMSetDepthStencilState(dsNoWrite_, 0);
}

// ============================================================================
// Frame Rendering
// ============================================================================

void Renderer::RenderFrame() {
    if (!rtv_ || !context_) return;

    float clearColor[4] = {0.39f, 0.39f, 0.40f, 1.0f};  // Magos-matched gray
    context_->ClearRenderTargetView(rtv_, clearColor);
    context_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width_; vp.Height = (float)height_;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);
    context_->OMSetRenderTargets(1, &rtv_, dsv_);

    XMMATRIX view, proj;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        view = camera_.GetViewMatrix();
    }
    float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    proj = XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect, 1.0f, 10000.0f);

    // Update constant buffer
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        CBPerFrame* cb = (CBPerFrame*)mapped.pData;
        cb->world      = XMMatrixTranspose(XMMatrixIdentity());
        cb->view       = XMMatrixTranspose(view);
        cb->projection = XMMatrixTranspose(proj);
        XMVECTOR ld = XMVector3Normalize(XMVectorSet(0.0f, -0.3f, -0.8f, 0.0f));
        XMStoreFloat4(&cb->lightDir, ld);
        cb->lightColor   = {0.50f, 0.50f, 0.48f, 1.0f};
        cb->ambientColor = {0.60f, 0.60f, 0.65f, 0.0f};  // .a=0 no alpha test
        cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};     // .x=geosetAlpha (1=visible)
        cb->materialFlags = {0.0f, 0.0f, 0.0f, 0.0f};
        context_->Unmap(cbPerFrame_, 0);
    }

    if (showGrid_) RenderGrid();
    RenderGeosets();
    if (showParticles_) RenderParticles();
    if (showRibbons_) RenderRibbons();
    if (showCollisions_) RenderCollisions();
    RenderViewCube();

    swapChain_->Present(1, 0);
}

void Renderer::RenderGrid() {
    if (!gridVB_) return;
    context_->IASetInputLayout(lineInputLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    UINT stride = sizeof(LineVertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, &gridVB_, &stride, &offset);
    context_->VSSetShader(lineVertexShader_, nullptr, 0);
    context_->PSSetShader(linePixelShader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->RSSetState(rsNoCull_);
    context_->OMSetDepthStencilState(dsDefault_, 0);
    float blend[] = {0,0,0,0};
    context_->OMSetBlendState(bsOpaque_, blend, 0xFFFFFFFF);
    context_->Draw(gridVertCount_, 0);
}

// ============================================================================
// Geoset Rendering (4-bucket sort by FilterMode, like Magos Model::Render)
// ============================================================================

void Renderer::RenderGeosets() {
    if (models_.empty()) return;

    context_->IASetInputLayout(inputLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->PSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->PSSetSamplers(0, 1, &samplerLinear_);

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

    XMMATRIX view2;
    { std::lock_guard<std::mutex> lock(dataMutex_); view2 = camera_.GetViewMatrix(); }
    float aspect2 = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    XMMATRIX proj2 = XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect2, 1.0f, 10000.0f);

    for (auto& ref : refs) {
        auto* mi = ref.mi;
        auto& geo = mi->gpuGeosets[ref.idx];
        if (!geo.vb || !geo.ib || geo.indexCount == 0) continue;

        int matId = geo.materialId;
        GPUMaterial* mat = nullptr;
        if (matId >= 0 && matId < (int)mi->gpuMaterials.size())
            mat = &mi->gpuMaterials[matId];

        float geoAlpha = geo.geosetAlpha * mi->parentVisibility;
        if (geoAlpha < 0.01f) continue;
        XMFLOAT3 geoColor = geo.geosetColor;

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
                UINT stride = sizeof(Vertex), offset = 0;
                context_->IASetVertexBuffers(0, 1, &geo.vb, &stride, &offset);
                context_->IASetIndexBuffer(geo.ib, DXGI_FORMAT_R32_UINT, 0);
                anyLayerThisPass = true;
            }

            ApplyFilterMode(layerFilter, layerFlags);

            float combinedAlpha = geoAlpha * layerAlpha;
            if (combinedAlpha < 0.004f) continue;

            if (combinedAlpha < 0.99f && layerFilter <= FILTER_TRANSPARENT) {
                float blend[] = {0,0,0,0};
                context_->OMSetBlendState(bsAlphaBlend_, blend, 0xFFFFFFFF);
                context_->OMSetDepthStencilState(dsNoWrite_, 0);
            }

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
                D3D11_MAPPED_SUBRESOURCE mapped;
                context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                CBPerFrame* cb = (CBPerFrame*)mapped.pData;
                XMMATRIX world = mi->worldTransform;
                cb->world      = XMMatrixTranspose(world);
                cb->view       = XMMatrixTranspose(view2);
                cb->projection = XMMatrixTranspose(proj2);

                XMVECTOR ld = XMVector3Normalize(XMVectorSet(0.0f, -0.3f, -0.8f, 0.0f));
                XMStoreFloat4(&cb->lightDir, ld);
                cb->lightColor    = {0.50f, 0.50f, 0.48f, 1.0f};
                cb->ambientColor  = {0.60f, 0.60f, 0.65f, alphaRef};
                cb->extraParams   = {combinedAlpha, geoColor.x, geoColor.y, geoColor.z};
                cb->texAnimParams = {uOff, vOff, uTile, vTile};
                cb->materialFlags = {
                    (layerFlags & MAT_UNSHADED)       ? 1.0f : 0.0f,
                    (layerFlags & MAT_CONSTANT_COLOR) ? 1.0f : 0.0f,
                    texRot, 0.0f
                };
                context_->Unmap(cbPerFrame_, 0);
            }

            ID3D11ShaderResourceView* srv = defaultTexSRV_;
            uint32_t wrapFlags = 0x3; // default: wrap both
            if (layerTexId >= 0 && mi->gpuTextures.count(layerTexId)) {
                auto& gt = mi->gpuTextures[layerTexId];
                srv = gt.srv;
                wrapFlags = gt.wrapFlags & 0x3;
            }
            if (!srv) srv = defaultTexSRV_;
            context_->PSSetShaderResources(0, 1, &srv);
            context_->PSSetSamplers(0, 1, &samplerWrap_[wrapFlags]);

            context_->DrawIndexed(geo.indexCount, 0, 0);
        }
    }
}

// ============================================================================
// ViewCube — 3D orientation cube in top-right corner with Home button
// ============================================================================

RECT Renderer::GetViewCubeRect() const {
    int s = kViewCubeSize;
    int margin = 10;
    int cubeTop = margin + 28;
    return { width_ - s - margin, margin, width_ - margin, cubeTop + s };
}

// Generate face label texture using GDI
static bool CreateFaceLabelTexture(ID3D11Device* device,
                                   ID3D11Texture2D** outTex,
                                   ID3D11ShaderResourceView** outSRV) {
    const int cellW = 64, cellH = 64;
    const int texW = cellW * 6, texH = cellH; // 6 faces in a row
    const wchar_t* labels[] = { L"FRONT", L"BACK", L"LEFT", L"RIGHT", L"TOP", L"BOT" };
    // Face colors (muted pastels)
    COLORREF faceColors[] = {
        RGB(100,140,190), // Front  — blue
        RGB(190,120,100), // Back   — red
        RGB(100,180,120), // Left   — green
        RGB(190,170,100), // Right  — yellow
        RGB(160,160,180), // Top    — gray-blue
        RGB(140,130,120), // Bottom — brown
    };

    // Create GDI bitmap
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = texW;
    bmi.bmiHeader.biHeight = -texH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(hdc, hbmp);

    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_BOLD, 0, 0, 0,
                               DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < 6; i++) {
        RECT rc = { i * cellW, 0, (i + 1) * cellW, cellH };
        // Fill background
        HBRUSH brush = CreateSolidBrush(faceColors[i]);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        // Draw border
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
        SelectObject(hdc, pen);
        MoveToEx(hdc, rc.left, rc.top, nullptr);
        LineTo(hdc, rc.right-1, rc.top);
        LineTo(hdc, rc.right-1, rc.bottom-1);
        LineTo(hdc, rc.left, rc.bottom-1);
        LineTo(hdc, rc.left, rc.top);
        DeleteObject(pen);
        // Draw text
        SetTextColor(hdc, RGB(240, 240, 240));
        DrawTextW(hdc, labels[i], -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    GdiFlush();

    // Convert BGR→RGBA
    std::vector<uint8_t> rgba(texW * texH * 4);
    auto* src = (uint8_t*)bits;
    for (int i = 0; i < texW * texH; i++) {
        rgba[i*4+0] = src[i*4+2]; // R
        rgba[i*4+1] = src[i*4+1]; // G
        rgba[i*4+2] = src[i*4+0]; // B
        rgba[i*4+3] = 230;        // slight transparency
    }

    DeleteObject(hFont);
    DeleteObject(hbmp);
    DeleteDC(hdc);

    // Create DX11 texture
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = texW; td.Height = texH;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA srd = { rgba.data(), (UINT)(texW * 4), 0 };
    if (FAILED(device->CreateTexture2D(&td, &srd, outTex))) return false;
    device->CreateShaderResourceView(*outTex, nullptr, outSRV);
    return true;
}

bool Renderer::CreateViewCube() {
    // Generate face label texture via GDI
    CreateFaceLabelTexture(device_, &vcFaceTex_, &vcFaceTexSRV_);

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
    auto addFace = [&](int face, XMFLOAT3 p0, XMFLOAT3 p1, XMFLOAT3 p2, XMFLOAT3 p3, XMFLOAT3 n) {
        for (int c = 0; c < 4; c++) {
            auto [u, v] = uv(face, c);
            XMFLOAT3 p = (c==0) ? p0 : (c==1) ? p1 : (c==2) ? p2 : p3;
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

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(Vertex) * verts.size());
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA srd = { verts.data() };
    device_->CreateBuffer(&bd, &srd, &vcCubeVB_);

    // Index buffer: 2 triangles per face
    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; f++) {
        uint32_t base = f * 4;
        idx.insert(idx.end(), {base, base+1, base+2, base, base+2, base+3});
    }
    bd.ByteWidth = (UINT)(sizeof(uint32_t) * idx.size());
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    srd.pSysMem = idx.data();
    device_->CreateBuffer(&bd, &srd, &vcCubeIB_);

    // Edge lines (12 edges of the cube)
    std::vector<LineVertex> edges;
    XMFLOAT4 ec = {0.2f, 0.2f, 0.2f, 1.0f};
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

    bd.ByteWidth = (UINT)(sizeof(LineVertex) * edges.size());
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    srd.pSysMem = edges.data();
    device_->CreateBuffer(&bd, &srd, &vcOutlineVB_);

    return true;
}

void Renderer::RenderViewCube() {
    if (!vcCubeVB_ || !vcCubeIB_) return;

    // Save main viewport, set ViewCube viewport (top-right corner)
    int s = kViewCubeSize;
    int margin = 10;
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (float)(width_ - s - margin);
    vp.TopLeftY = (float)(margin + 28);  // leave room for Home button above
    vp.Width = (float)s;
    vp.Height = (float)s;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    // Clear depth only in this viewport area
    // (We render on top of the scene background)
    context_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Build view matrix: same rotation as camera, but fixed distance, looking at origin
    XMMATRIX vcView;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        float dist = 3.5f; // fixed distance for the cube
        float cosP = cosf(camera_.GetPitch()), sinP = sinf(camera_.GetPitch());
        float cosY = cosf(camera_.GetYaw()),   sinY = sinf(camera_.GetYaw());
        XMFLOAT3 eye = { dist * cosP * cosY, dist * cosP * sinY, dist * sinP };
        XMFLOAT3 tgt = { 0, 0, 0 };
        XMFLOAT3 up  = { 0, 0, 1 };
        vcView = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMLoadFloat3(&tgt), XMLoadFloat3(&up));
    }
    XMMATRIX vcProj = XMMatrixPerspectiveFovRH(XM_PIDIV4, 1.0f, 0.1f, 100.0f);

    // Update constant buffer for ViewCube
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        CBPerFrame* cb = (CBPerFrame*)mapped.pData;
        cb->world      = XMMatrixTranspose(XMMatrixIdentity());
        cb->view       = XMMatrixTranspose(vcView);
        cb->projection = XMMatrixTranspose(vcProj);
        XMVECTOR ld = XMVector3Normalize(XMVectorSet(0.5f, 0.3f, -0.8f, 0.0f));
        XMStoreFloat4(&cb->lightDir, ld);
        cb->lightColor   = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->ambientColor = {0.5f, 0.5f, 0.5f, 1.0f};
        cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
        cb->materialFlags = {0.0f, 0.0f, 0.0f, 0.0f};
        context_->Unmap(cbPerFrame_, 0);
    }

    // Draw cube faces (textured)
    context_->IASetInputLayout(inputLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, &vcCubeVB_, &stride, &offset);
    context_->IASetIndexBuffer(vcCubeIB_, DXGI_FORMAT_R32_UINT, 0);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->PSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->PSSetSamplers(0, 1, &samplerLinear_);

    ID3D11ShaderResourceView* srv = vcFaceTexSRV_ ? vcFaceTexSRV_ : defaultTexSRV_;
    context_->PSSetShaderResources(0, 1, &srv);
    context_->RSSetState(rsNoCull_);
    context_->OMSetDepthStencilState(dsDefault_, 0);
    float blend[] = {0,0,0,0};
    context_->OMSetBlendState(bsOpaque_, blend, 0xFFFFFFFF);
    context_->DrawIndexed(36, 0, 0);

    // Draw edges
    context_->IASetInputLayout(lineInputLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    stride = sizeof(LineVertex);
    context_->IASetVertexBuffers(0, 1, &vcOutlineVB_, &stride, &offset);
    context_->VSSetShader(lineVertexShader_, nullptr, 0);
    context_->PSSetShader(linePixelShader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->Draw(24, 0); // 12 edges × 2 verts

    // --- Home button icon (above cube, only on hover) ---
    if (vcHovered_) {
        D3D11_VIEWPORT homeVp = {};
        homeVp.TopLeftX = vp.TopLeftX + (float)s * 0.35f;
        homeVp.TopLeftY = vp.TopLeftY - 24.0f;
        homeVp.Width = (float)s * 0.3f;
        homeVp.Height = 20.0f;
        homeVp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &homeVp);

        // Orthographic view for 2D drawing
        D3D11_MAPPED_SUBRESOURCE mapped;
        context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        CBPerFrame* cb = (CBPerFrame*)mapped.pData;
        cb->world = XMMatrixTranspose(XMMatrixIdentity());
        cb->view = XMMatrixTranspose(XMMatrixIdentity());
        cb->projection = XMMatrixTranspose(XMMatrixOrthographicRH(2.0f, 2.0f, -1, 1));
        cb->lightDir = {0,0,0,0};
        cb->lightColor = {1,1,1,1};
        cb->ambientColor = {1,1,1,1};
        cb->extraParams  = {1,0,0,0};
        cb->materialFlags = {0,0,0,0};
        context_->Unmap(cbPerFrame_, 0);

        // House icon as dynamic lines
        XMFLOAT4 hc = {0.7f, 0.7f, 0.7f, 1.0f};
        LineVertex house[] = {
            {{-0.4f, -0.6f, 0}, hc}, {{ 0.4f, -0.6f, 0}, hc}, // bottom
            {{-0.4f, -0.6f, 0}, hc}, {{-0.4f,  0.0f, 0}, hc}, // left wall
            {{ 0.4f, -0.6f, 0}, hc}, {{ 0.4f,  0.0f, 0}, hc}, // right wall
            {{-0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc}, // roof left
            {{ 0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc}, // roof right
            {{-0.5f,  0.0f, 0}, hc}, {{ 0.5f,  0.0f, 0}, hc}, // roof base
        };

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(house);
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA srd = { house };
        ID3D11Buffer* homeVB = nullptr;
        device_->CreateBuffer(&bd, &srd, &homeVB);
        if (homeVB) {
            UINT hStride = sizeof(LineVertex), hOff = 0;
            context_->IASetVertexBuffers(0, 1, &homeVB, &hStride, &hOff);
            context_->Draw(12, 0);
            homeVB->Release();
        }
    }

    // Restore main viewport
    D3D11_VIEWPORT mainVp = {};
    mainVp.Width = (float)width_;
    mainVp.Height = (float)height_;
    mainVp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &mainVp);

    // Restore main scene constant buffer
    XMMATRIX view, proj;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        view = camera_.GetViewMatrix();
    }
    float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    proj = XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect, 1.0f, 10000.0f);
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        CBPerFrame* cb = (CBPerFrame*)mapped.pData;
        cb->world      = XMMatrixTranspose(XMMatrixIdentity());
        cb->view       = XMMatrixTranspose(view);
        cb->projection = XMMatrixTranspose(proj);
        XMVECTOR ld = XMVector3Normalize(XMVectorSet(0.0f, -0.3f, -0.8f, 0.0f));
        XMStoreFloat4(&cb->lightDir, ld);
        cb->lightColor   = {0.50f, 0.50f, 0.48f, 1.0f};
        cb->ambientColor = {0.60f, 0.60f, 0.65f, 0.0f};  // .a=0 no alpha test
        cb->extraParams  = {1.0f, 1.0f, 1.0f, 1.0f};
        cb->texAnimParams = {0.0f, 0.0f, 1.0f, 1.0f};
        cb->materialFlags = {0.0f, 0.0f, 0.0f, 0.0f};
        context_->Unmap(cbPerFrame_, 0);
    }
}

// Hit test: returns face index 0-5, 6=Home, -1=none
int Renderer::HitTestViewCube(int mx, int my) {
    RECT r = GetViewCubeRect();

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

    XMMATRIX vcView;
    {
        float dist = 3.5f;
        float cosP = cosf(camera_.GetPitch()), sinP = sinf(camera_.GetPitch());
        float cosY = cosf(camera_.GetYaw()),   sinY = sinf(camera_.GetYaw());
        XMFLOAT3 eye = { dist*cosP*cosY, dist*cosP*sinY, dist*sinP };
        XMFLOAT3 up  = { 0, 0, 1 };
        vcView = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMVectorZero(), XMLoadFloat3(&up));
    }
    XMMATRIX vcProj = XMMatrixPerspectiveFovRH(XM_PIDIV4, 1.0f, 0.1f, 100.0f);
    XMMATRIX vp_mat = vcView * vcProj;

    // Face centers and normals
    XMFLOAT3 centers[] = {{0,.5f,0},{0,-.5f,0},{-.5f,0,0},{.5f,0,0},{0,0,.5f},{0,0,-.5f}};
    XMFLOAT3 normals[] = {{0,1,0},{0,-1,0},{-1,0,0},{1,0,0},{0,0,1},{0,0,-1}};

    // Camera direction for backface culling
    float cosP = cosf(camera_.GetPitch()), sinP = sinf(camera_.GetPitch());
    float cosY = cosf(camera_.GetYaw()),   sinY = sinf(camera_.GetYaw());
    XMFLOAT3 camDir = { -cosP*cosY, -cosP*sinY, -sinP }; // toward target

    int bestFace = -1;
    float bestDist = 1e9f;

    for (int i = 0; i < 6; i++) {
        // Backface cull: skip faces pointing AWAY from camera
        // (visible faces have normal opposing camDir → negative dot)
        float dot = normals[i].x*camDir.x + normals[i].y*camDir.y + normals[i].z*camDir.z;
        if (dot > -0.05f) continue;

        XMVECTOR p = XMLoadFloat3(&centers[i]);
        XMVECTOR proj = XMVector3Project(p, vcX, vcY, (float)s, (float)s, 0, 1, vcProj, vcView, XMMatrixIdentity());
        XMFLOAT3 sp;
        XMStoreFloat3(&sp, proj);

        float dx = sp.x - mx;
        float dy = sp.y - my;
        float d = dx*dx + dy*dy;
        if (d < bestDist && d < (s*s*0.06f)) { // tighter radius — only on the cube itself
            bestDist = d;
            bestFace = i;
        }
    }
    return bestFace;
}

void Renderer::SnapCameraToFace(int faceIndex) {
    const float PI = 3.14159265f;
    const float HALF_PI = PI / 2.0f;
    // Face order: Front(+Y), Back(-Y), Left(-X), Right(+X), Top(+Z), Bottom(-Z)
    switch (faceIndex) {
        case 0: camera_.SetYaw(HALF_PI);   camera_.SetPitch(0.0f); break;  // Front
        case 1: camera_.SetYaw(-HALF_PI);  camera_.SetPitch(0.0f); break;  // Back
        case 2: camera_.SetYaw(PI);        camera_.SetPitch(0.0f); break;  // Left
        case 3: camera_.SetYaw(0.0f);      camera_.SetPitch(0.0f); break;  // Right
        case 4: camera_.SetYaw(HALF_PI);   camera_.SetPitch(1.55f); break; // Top
        case 5: camera_.SetYaw(HALF_PI);   camera_.SetPitch(-1.55f); break;// Bottom
    }
}

} // namespace WhiteoutDex
