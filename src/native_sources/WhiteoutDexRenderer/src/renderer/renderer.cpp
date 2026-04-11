// ============================================================================
// WhiteoutDex Real-Time Renderer — Core Implementation
// Phase 4: Matrix-based CPU Vertex Skinning
// ============================================================================

#include "renderer.h"
#include "shaders.h"
#include "resource.h"
#include <windowsx.h>
#include <commdlg.h>
#include <execution>   // std::execution::par for parallel skinning

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
    stagedGeosets_.clear();
    stagedMaterials_.clear();
    stagedTextures_.clear();
    stagedClear_ = true;
    stagedDirty_ = true;
    skinning_.Clear();
    skinDirty_ = false;
    particles_.Clear();
    ribbons_.Clear();
    collisionShapes_.clear();
    matTexAnim_.clear();
    replaceableTexMap_.clear();
}

// ============================================================================
// Update Materials (hot-reload without full model rebuild)
// ============================================================================

void Renderer::UpdateMaterials(const std::vector<MaterialData>& materials,
                               const std::vector<TextureData>& textures) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Re-stage textures
    for (auto& tex : textures) {
        StagedTexture& st = stagedTextures_[tex.textureId];
        st.width  = tex.width;
        st.height = tex.height;
        st.replaceableId = tex.replaceableId;
        st.pixels = tex.rgba;
        if (tex.replaceableId == 1 || tex.replaceableId == 2)
            replaceableTexMap_[tex.textureId] = tex.replaceableId;
    }

    // Re-stage materials
    for (auto& mat : materials) {
        StagedMaterial& sm = stagedMaterials_[mat.materialId];
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

    stagedDirty_ = true;
}

// ============================================================================
// NEW: Typed Model Loading API (Phase A — adapter pattern)
// ============================================================================

void Renderer::LoadModel(const std::vector<MeshData>& meshes,
                         const std::vector<TextureData>& textures,
                         const std::vector<MaterialData>& materials,
                         const SkeletonData& skeleton,
                         const std::vector<SkinWeightData>& skinWeights,
                         const std::vector<ParticleEmitterConfig>& particleConfigs,
                         const std::vector<RibbonEmitterConfig>& ribbonConfigs,
                         const std::vector<CollisionShapeData>& collisions) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Textures → staged
    for (auto& tex : textures) {
        StagedTexture& st = stagedTextures_[tex.textureId];
        st.width  = tex.width;
        st.height = tex.height;
        st.replaceableId = tex.replaceableId;
        st.pixels = tex.rgba;
        // Track replaceable textures for team color updates
        if (tex.replaceableId == 1 || tex.replaceableId == 2)
            replaceableTexMap_[tex.textureId] = tex.replaceableId;
    }

    // Materials → staged
    for (auto& mat : materials) {
        StagedMaterial& sm = stagedMaterials_[mat.materialId];
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
        StagedGeoset& sg = stagedGeosets_[mesh.geosetId];
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
    if (skeleton.boneCount > 0) {
        // Convert XMMATRIX array to flat float array for existing SkinningSystem
        std::vector<float> invBindFlat(skeleton.boneCount * 16);
        for (int i = 0; i < skeleton.boneCount; i++) {
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
        skinning_.SetSkeleton(skeleton.boneCount, invBindFlat.data());
        skinDirty_ = true;
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
        skinning_.SetGeosetWeights(sw.geosetId, vc, boneIdx.data(), weights.data());
    }
    if (!skinWeights.empty()) skinDirty_ = true;

    // Particles
    for (size_t i = 0; i < particleConfigs.size(); i++) {
        particles_.AddEmitter((int)i, particleConfigs[i]);
    }

    // Ribbons
    for (size_t i = 0; i < ribbonConfigs.size(); i++) {
        ribbons_.AddEmitter((int)i, ribbonConfigs[i]);
    }

    // Collision shapes
    for (auto& cs : collisions) {
        CollisionShape shape;
        shape.type   = cs.type;
        shape.vmin   = cs.vertices[0];
        shape.vmax   = cs.vertices[1];
        shape.radius = cs.radius;
        collisionShapes_.push_back(shape);
    }

    stagedDirty_ = true;
}

// ============================================================================
// NEW: Apply Pre-computed Frame State (Phase A — adapter pattern)
// ============================================================================

void Renderer::ApplyFrameState(const FrameState& state, int timeMs) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    // Bone matrices → skinning system
    if (!state.boneWorldMatrices.empty()) {
        int bc = (int)state.boneWorldMatrices.size();
        std::vector<float> worldFlat(bc * 16);
        for (int i = 0; i < bc; i++) {
            XMFLOAT4X4 f44;
            XMStoreFloat4x4(&f44, state.boneWorldMatrices[i]);
            float* dst = &worldFlat[i * 16];
            dst[0]  = f44._11; dst[1]  = f44._12; dst[2]  = f44._13; dst[3]  = f44._14;
            dst[4]  = f44._21; dst[5]  = f44._22; dst[6]  = f44._23; dst[7]  = f44._24;
            dst[8]  = f44._31; dst[9]  = f44._32; dst[10] = f44._33; dst[11] = f44._34;
            dst[12] = f44._41; dst[13] = f44._42; dst[14] = f44._43; dst[15] = f44._44;
        }
        skinning_.UpdateBoneMatrices(bc, worldFlat.data());
    }

    // Geoset world transforms (used for unskinned meshes; skinned ones ignore this)
    for (int i = 0; i < (int)state.geosetTransforms.size() && i < (int)gpuGeosets_.size(); i++) {
        gpuGeosets_[i].worldMatrix = state.geosetTransforms[i];
    }

    // Geoset visibility
    for (int i = 0; i < (int)state.geosetAlphas.size() && i < (int)gpuGeosets_.size(); i++) {
        gpuGeosets_[i].geosetAlpha = state.geosetAlphas[i];
    }

    // Geoset colors
    for (int i = 0; i < (int)state.geosetColors.size() && i < (int)gpuGeosets_.size(); i++) {
        gpuGeosets_[i].geosetColor = state.geosetColors[i];
    }

    // Particle emitter states
    for (auto& ps : state.particleStates) {
        ParticleEmitterState st;
        st.transform    = ps.transform;
        st.emissionRate = ps.emissionRate;
        st.speed        = ps.speed;
        st.variation    = ps.variation;
        st.coneAngle    = ps.coneAngle;
        st.longitude    = ps.longitude;
        st.gravity      = ps.gravity;
        st.width        = ps.width;
        st.length       = ps.length;
        st.visibility   = ps.visibility;
        particles_.UpdateEmitterState(ps.emitterId, st);
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
        ribbons_.UpdateEmitterState(rs.emitterId, st);
    }

    // Collision transforms
    for (int i = 0; i < (int)state.collisionTransforms.size() && i < (int)collisionShapes_.size(); i++) {
        collisionShapes_[i].transform = state.collisionTransforms[i];
    }

    // Texture animations (per-layer) — clear stale entries from previous frame
    matTexAnim_.clear();
    for (auto& ta : state.texAnims) {
        int key = ta.materialId * 1000 + ta.layerIndex;
        matTexAnim_[key] = {ta.uOff, ta.vOff, ta.uTile, ta.vTile, ta.rotation};
    }

    // Per-layer alpha animation (KMTA tracks)
    for (auto& la : state.layerAlphas) {
        if (la.materialId >= 0 && la.materialId < (int)gpuMaterials_.size()) {
            auto& layers = gpuMaterials_[la.materialId].cpu.layers;
            if (la.layerIndex >= 0 && la.layerIndex < (int)layers.size()) {
                layers[la.layerIndex].alpha = la.alpha;
            }
        }
    }

    // Advance simulation clock (replaces old SetTime)
    currentTimeMs_ = timeMs;
}

// ============================================================================
// Team Color Texture Update
// ============================================================================

void Renderer::UpdateTeamColorTextures() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    uint8_t r = GetRValue(teamColor_);
    uint8_t g = GetGValue(teamColor_);
    uint8_t b = GetBValue(teamColor_);
    for (auto& [texId, replId] : replaceableTexMap_) {
        StagedTexture& st = stagedTextures_[texId];
        st.width = 4; st.height = 4;
        st.replaceableId = replId;
        st.pixels.resize(64);
        uint8_t tr = r, tg = g, tb = b;
        for (int j = 0; j < 16; j++) {
            st.pixels[j * 4 + 0] = tr;
            st.pixels[j * 4 + 1] = tg;
            st.pixels[j * 4 + 2] = tb;
            st.pixels[j * 4 + 3] = 255;
        }
    }
    if (!replaceableTexMap_.empty()) stagedDirty_ = true;
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

// ============================================================================
// Staged → GPU Resource Upload (render thread only)
// ============================================================================

void Renderer::ProcessStagedData() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!stagedDirty_ && !skinDirty_) return;

    if (stagedClear_) {
        ReleaseModelGPU();
        stagedClear_ = false;
    }

    if (stagedDirty_) {
        // Upload textures (individual Texture2D)
        for (auto& [id, st] : stagedTextures_) {
            if (st.width <= 0 || st.height <= 0) continue;

            if (gpuTextures_.count(id)) gpuTextures_[id].Release();

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
            gpuTextures_[id] = gt;
        }
        stagedTextures_.clear();

        // Copy materials (CPU data for render logic)
        for (auto& [id, sm] : stagedMaterials_) {
            if ((int)gpuMaterials_.size() <= id) gpuMaterials_.resize(id + 1);
            gpuMaterials_[id].cpu = sm;
        }
        stagedMaterials_.clear();

        // Upload geosets
        for (auto& [id, sg] : stagedGeosets_) {
            GPUGeoset gg;
            gg.geosetId    = id;
            gg.materialId  = sg.materialId;
            gg.indexCount   = (int)sg.indices.size();
            gg.vertexCount  = (int)sg.vertices.size();
            gg.baseVertices = sg.vertices;  // keep CPU copy for skinning
            gg.hasSkinning  = skinning_.HasWeights(id);

            // Copy priorityPlane from material for render sorting
            if (sg.materialId >= 0 && sg.materialId < (int)gpuMaterials_.size())
                gg.priorityPlane = gpuMaterials_[sg.materialId].cpu.priorityPlane;

            // Vertex buffer — DYNAMIC for skinning updates
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth      = (UINT)(sizeof(Vertex) * sg.vertices.size());
            bd.Usage          = D3D11_USAGE_DYNAMIC;
            bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            D3D11_SUBRESOURCE_DATA srd = {};
            srd.pSysMem = sg.vertices.data();
            device_->CreateBuffer(&bd, &srd, &gg.vb);

            // Index buffer (immutable)
            bd.ByteWidth      = (UINT)(sizeof(uint32_t) * sg.indices.size());
            bd.Usage          = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags      = D3D11_BIND_INDEX_BUFFER;
            bd.CPUAccessFlags = 0;
            srd.pSysMem = sg.indices.data();
            device_->CreateBuffer(&bd, &srd, &gg.ib);

            gpuGeosets_.push_back(gg);
        }
        stagedGeosets_.clear();
        stagedDirty_ = false;
    }

    // Phase 4: Update skinning flags (runs independently of mesh uploads)
    if (skinDirty_) {
        for (auto& geo : gpuGeosets_)
            geo.hasSkinning = skinning_.HasWeights(geo.geosetId);
        skinDirty_ = false;
    }
}

void Renderer::ReleaseModelGPU() {
    for (auto& g : gpuGeosets_) g.Release();
    gpuGeosets_.clear();
    for (auto& [id, t] : gpuTextures_) t.Release();
    gpuTextures_.clear();
    gpuMaterials_.clear();
}

// Build a packed Texture2DArray for a material.
// All layer textures are CPU-resized (nearest-neighbor) to the max (w,h) found
// ============================================================================
// Phase 4: Animation Update (render thread)
// ============================================================================

void Renderer::UpdateAnimation() {
    // ================================================================
    // Phase 1: Compute offset matrices (single-threaded, under lock)
    // offset = inverseBind × currentWorld — must happen before skinning
    // ================================================================
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!skinning_.HasSkeleton() || !skinning_.IsReady()) return;
        skinning_.ComputeOffsetMatrices();
    }
    // After this point, offsetMatrices_ and geosetWeights_ are immutable
    // until the next ComputeOffsetMatrices call (next frame).
    // The Max thread only writes to currentMatrices_ (via UpdateBones),
    // which we don't read during skinning. So parallel access is safe.

    // ================================================================
    // Phase 2: Parallel CPU skinning — each geoset independently
    // Each geoset reads from shared (immutable) bone data and writes
    // to its own output vector. No shared mutable state = no races.
    // ================================================================
    struct SkinJob {
        int geosetId;
        const std::vector<Vertex>* baseVerts;
        std::vector<Vertex> skinned;
        ID3D11Buffer* vb;
        bool ok = false;
    };

    std::vector<SkinJob> jobs;
    jobs.reserve(gpuGeosets_.size());
    for (auto& geo : gpuGeosets_) {
        if (!geo.hasSkinning || geo.baseVertices.empty() || !geo.vb) continue;
        SkinJob j;
        j.geosetId = geo.geosetId;
        j.baseVerts = &geo.baseVertices;
        j.vb = geo.vb;
        jobs.push_back(std::move(j));
    }
    if (jobs.empty()) return;

    // Parallel for_each: skin all geosets across available CPU cores
    // SkinVertices is const — reads offsetMatrices_ + geosetWeights_ (immutable)
    // Each job writes to its own 'skinned' vector — zero contention
    std::for_each(std::execution::par, jobs.begin(), jobs.end(),
        [this](SkinJob& j) {
            j.ok = skinning_.SkinVertices(j.geosetId, *j.baseVerts, j.skinned);
        });

    // ================================================================
    // Phase 3: Upload results to GPU (single-threaded — DX11 requirement)
    // ================================================================
    for (auto& j : jobs) {
        if (!j.ok) continue;
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context_->Map(j.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped.pData, j.skinned.data(), sizeof(Vertex) * j.skinned.size());
            context_->Unmap(j.vb, 0);
        }
    }
}

// ============================================================================
// Phase 5: Particle Simulation + Rendering (render thread)
// ============================================================================

void Renderer::UpdateParticles(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    particles_.Simulate(dt);
}

void Renderer::RenderParticles() {
    std::vector<Vertex> verts;
    std::vector<int> emitterIds;

    // Snapshot all data under one lock
    float pitch, yaw;
    XMMATRIX viewMat;
    std::vector<ParticleEmitterConfig> configs;  // config per emitter in emitterIds order
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!particles_.HasEmitters()) return;
        pitch   = camera_.GetPitch();
        yaw     = camera_.GetYaw();
        viewMat = camera_.GetViewMatrix();
        particles_.BuildBillboards(pitch, yaw, verts, emitterIds);

        // Snapshot configs for each emitter
        for (int eid : emitterIds) {
            auto* c = particles_.GetConfig(eid);
            configs.push_back(c ? *c : ParticleEmitterConfig{});
        }
    }
    // Lock released — safe to call DX11

    if (verts.empty()) return;

    int vertCount = (int)verts.size();

    // Grow particle VB if needed
    if (!particleVB_ || vertCount > particleVBSize_) {
        SafeRelease(particleVB_);
        int newSize = (std::max)(vertCount, 1024);
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = (UINT)(sizeof(Vertex) * newSize);
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&bd, nullptr, &particleVB_);
        particleVBSize_ = newSize;
    }

    // Upload vertex data
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context_->Map(particleVB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, verts.data(), sizeof(Vertex) * vertCount);
    context_->Unmap(particleVB_, 0);

    // Bind particle VB
    UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, &particleVB_, &stride, &offset);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set main shader + input layout
    context_->IASetInputLayout(inputLayout_);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);

    // Particles are always two-sided (billboards can face either way)
    context_->RSSetState(rsNoCull_);

    // Render per-emitter with correct blend state and texture
    // BuildBillboards generates 6 verts per particle, in emitter order
    int drawOffset = 0;

    for (int ei = 0; ei < (int)emitterIds.size(); ei++) {
        auto& cfg = configs[ei];

        // Count particles for this emitter by scanning verts
        // (they're contiguous per emitter)
        // Actually, we need to know how many particles each emitter contributed.
        // Re-derive from the configs/particles — simpler: track counts during BuildBillboards
        // For now, scan forward for matching vertex color patterns or just divide evenly.
        // BETTER: count non-dead particles per emitter.
    }

    // Simpler approach: just draw all particles grouped by emitter
    // We know emitters are in emitterIds order, 6 verts per particle
    // Re-count from particle system under lock
    std::vector<int> vertCounts;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (int eid : emitterIds) {
            int start = 0, count = 0;
            particles_.GetEmitterVertexRange(eid, vertCount, emitterIds, start, count);
            vertCounts.push_back(count);
        }
    }

    drawOffset = 0;
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
        if (cfg.textureId >= 0 && gpuTextures_.count(cfg.textureId))
            srv = gpuTextures_[cfg.textureId].srv;
        if (!srv) srv = defaultTexSRV_;
        context_->PSSetShaderResources(0, 1, &srv);

        // Draw this emitter's particles
        context_->Draw(count, drawOffset);
        drawOffset += count;
    }
}

// ============================================================================
// Phase 5b: Ribbon Simulation + Rendering (render thread)
// ============================================================================

void Renderer::UpdateRibbons(float dt) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    ribbons_.Simulate(dt);
}

void Renderer::RenderRibbons() {
    std::vector<Vertex> verts;
    std::vector<int> emitterIds;
    XMMATRIX viewMat;
    std::vector<RibbonEmitterConfig> configs;

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (!ribbons_.HasEmitters()) return;
        viewMat = camera_.GetViewMatrix();
        ribbons_.BuildStrips(verts, emitterIds);
        for (int eid : emitterIds) {
            auto* c = ribbons_.GetConfig(eid);
            configs.push_back(c ? *c : RibbonEmitterConfig{});
        }
    }

    if (verts.empty()) return;
    int vertCount = (int)verts.size();

    // Grow ribbon VB if needed
    if (!ribbonVB_ || vertCount > ribbonVBSize_) {
        SafeRelease(ribbonVB_);
        int newSize = (std::max)(vertCount, 512);
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = (UINT)(sizeof(Vertex) * newSize);
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device_->CreateBuffer(&bd, nullptr, &ribbonVB_);
        ribbonVBSize_ = newSize;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context_->Map(ribbonVB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    memcpy(mapped.pData, verts.data(), sizeof(Vertex) * vertCount);
    context_->Unmap(ribbonVB_, 0);

    UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, &ribbonVB_, &stride, &offset);
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
            vertCounts.push_back(ribbons_.GetEmitterVertCount(eid));
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
        if (cfg.textureId >= 0 && gpuTextures_.count(cfg.textureId))
            srv = gpuTextures_[cfg.textureId].srv;
        if (!srv) srv = defaultTexSRV_;
        context_->PSSetShaderResources(0, 1, &srv);

        context_->Draw(count, drawOffset);
        drawOffset += count;
    }

    // Restore default rasterizer
    context_->RSSetState(rsDefault_);
}

// ============================================================================
// Collision Shape Wireframe Rendering
// ============================================================================

void Renderer::RenderCollisions() {
    std::vector<CollisionShape> shapes;
    XMMATRIX viewMat;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (collisionShapes_.empty()) return;
        shapes = collisionShapes_;
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

        // Process any staged data from API thread
        ProcessStagedData();

        // Process pending camera preset updates
        ProcessCameraPresets();

        // Phase 4: Skin vertices with current bone matrices
        UpdateAnimation();

        // Phase 5: Simulate particles
        UpdateParticles((float)elapsed);
        UpdateRibbons((float)elapsed);

        RenderFrame();
        frameCount++;

        double fpsDt = (double)(now.QuadPart - fpsTimer.QuadPart) / freq.QuadPart;
        if (fpsDt >= 1.0) {
            int nGeo = (int)gpuGeosets_.size();
            int nTex = (int)gpuTextures_.size();
            int nBones = 0;
            int nParts = 0;
            int nSegs = 0;
            { std::lock_guard<std::mutex> lock(dataMutex_);
              nBones = skinning_.BoneCount();
              nParts = particles_.GetTotalParticleCount();
              nSegs = ribbons_.GetTotalSegmentCount();
            }
            wchar_t title[300];
            swprintf_s(title,
                L"Whiteout Renderer \u2014 %d FPS | %d geo, %d tex, %d bones, %d parts, %d segs",
                frameCount, nGeo, nTex, nBones, nParts, nSegs
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
    if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);

    // Forward mouse messages to parent WndProc logic
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_MOUSEMOVE:   case WM_MOUSEWHEEL:
        return Renderer::WndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
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
    return DefWindowProc(hwnd, msg, wParam, lParam);
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
        }
        return 0;
    }
    case WM_DESTROY:
        hwnd_ = nullptr;  // prevent double DestroyWindow in thread cleanup
        running_ = false; PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
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
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION,
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
    SafeRelease(bsModulate2x_); SafeRelease(bsModulate_); SafeRelease(bsAddAlpha_); SafeRelease(bsAdditive_);
    SafeRelease(bsAlphaBlend_); SafeRelease(bsAlphaTest_); SafeRelease(bsOpaque_);
    SafeRelease(dsDisabled_); SafeRelease(dsNoWrite_); SafeRelease(dsDefault_);
    SafeRelease(rsNoCull_); SafeRelease(rsDefault_);
    SafeRelease(cbPerFrame_);
    SafeRelease(gridVB_); SafeRelease(particleVB_); SafeRelease(ribbonVB_);
    SafeRelease(vcCubeVB_); SafeRelease(vcCubeIB_); SafeRelease(vcOutlineVB_);
    SafeRelease(vcFaceTexSRV_); SafeRelease(vcFaceTex_);
    SafeRelease(lineInputLayout_); SafeRelease(linePixelShader_); SafeRelease(lineVertexShader_);
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
    if (gpuGeosets_.empty()) return;

    // Multi-pass layer-by-layer rendering (matches WC3 engine)
    context_->IASetInputLayout(inputLayout_);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertexShader_, nullptr, 0);
    context_->PSSetShader(pixelShader_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->PSSetConstantBuffers(0, 1, &cbPerFrame_);
    context_->PSSetSamplers(0, 1, &samplerLinear_);

    // Sort geosets by (renderPass of layer 0, priorityPlane, geosetId)
    std::vector<int> sortedIdx(gpuGeosets_.size());
    for (int i = 0; i < (int)sortedIdx.size(); ++i) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(), [&](int a, int b) {
        auto& ga = gpuGeosets_[a];
        auto& gb = gpuGeosets_[b];
        int matIdA = ga.materialId, matIdB = gb.materialId;
        int roA = 1, roB = 1;
        if (matIdA >= 0 && matIdA < (int)gpuMaterials_.size() && !gpuMaterials_[matIdA].cpu.layers.empty())
            roA = GetRenderOrder(gpuMaterials_[matIdA].cpu.layers[0].filterMode);
        if (matIdB >= 0 && matIdB < (int)gpuMaterials_.size() && !gpuMaterials_[matIdB].cpu.layers.empty())
            roB = GetRenderOrder(gpuMaterials_[matIdB].cpu.layers[0].filterMode);
        if (roA != roB) return roA < roB;
        if (ga.priorityPlane != gb.priorityPlane) return ga.priorityPlane < gb.priorityPlane;
        return ga.geosetId < gb.geosetId;
    });

    XMMATRIX view2;
    { std::lock_guard<std::mutex> lock(dataMutex_); view2 = camera_.GetViewMatrix(); }
    float aspect2 = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    XMMATRIX proj2 = XMMatrixPerspectiveFovRH(XM_PIDIV4, aspect2, 1.0f, 10000.0f);

    // Per-layer texture animation lookup
    auto getTexAnim = [&](int matId, int layerIdx, float& uOff, float& vOff, float& uTile, float& vTile, float& rot) {
        uOff = vOff = 0; uTile = vTile = 1; rot = 0;
        int key = matId * 1000 + layerIdx;
        auto it = matTexAnim_.find(key);
        if (it != matTexAnim_.end()) {
            uOff = it->second.uOff; vOff = it->second.vOff;
            uTile = it->second.uTile; vTile = it->second.vTile;
            rot = it->second.rotation;
        }
    };

    for (int si : sortedIdx) {
        auto& geo = gpuGeosets_[si];
        if (!geo.vb || !geo.ib || geo.indexCount == 0) continue;

        int matId = geo.materialId;
        GPUMaterial* mat = nullptr;
        if (matId >= 0 && matId < (int)gpuMaterials_.size())
            mat = &gpuMaterials_[matId];

        float geoAlpha = geo.geosetAlpha;
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

            // Each layer draws in the pass matching its own filterMode
            // if (GetRenderOrder(layerFilter) != pass) continue;

            // Bind VB/IB on first layer drawn this pass
            if (!anyLayerThisPass) {
                UINT stride = sizeof(Vertex), offset = 0;
                context_->IASetVertexBuffers(0, 1, &geo.vb, &stride, &offset);
                context_->IASetIndexBuffer(geo.ib, DXGI_FORMAT_R32_UINT, 0);
                anyLayerThisPass = true;
            }

            // Apply this layer's blend/depth state
            // ApplyFilterMode sets depth write ON for None/Transparent,
            // OFF for Blend/Additive/AddAlpha/Modulate/Modulate2x.
            // This is correct for all layers — an opaque or alpha-tested
            // layer should write depth even if it's not layer 0.
            ApplyFilterMode(layerFilter, layerFlags);

            // Combined opacity (geoset visibility * layer opacity)
            float combinedAlpha = geoAlpha * layerAlpha;
            if (combinedAlpha < 0.004f) continue;  // skip invisible layers

            // Force alpha blend when combined alpha < 1
            if (combinedAlpha < 0.99f && layerFilter <= FILTER_TRANSPARENT) {
                float blend[] = {0,0,0,0};
                context_->OMSetBlendState(bsAlphaBlend_, blend, 0xFFFFFFFF);
                context_->OMSetDepthStencilState(dsNoWrite_, 0);
            }

            // Alpha test threshold — always active regardless of opacity.
            // The shader tests against texture alpha (not final alpha),
            // so the threshold works correctly at any opacity level.
            float alphaRef = 0.0f;
            if (layerFilter == FILTER_TRANSPARENT) alphaRef = 0.75f;
            else if (layerFilter >= FILTER_MODULATE) alphaRef = 0.02f;

            // Texture animation (per-layer)
            float uOff, vOff, uTile, vTile, texRot;
            getTexAnim(geo.materialId, li, uOff, vOff, uTile, vTile, texRot);

            // Update constant buffer
            {
                D3D11_MAPPED_SUBRESOURCE mapped;
                context_->Map(cbPerFrame_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                CBPerFrame* cb = (CBPerFrame*)mapped.pData;
                cb->world      = XMMatrixTranspose(geo.hasSkinning ? XMMatrixIdentity() : geo.worldMatrix);
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

            // Bind this layer's texture
            ID3D11ShaderResourceView* srv = defaultTexSRV_;
            if (layerTexId >= 0 && gpuTextures_.count(layerTexId))
                srv = gpuTextures_[layerTexId].srv;
            if (!srv) srv = defaultTexSRV_;
            context_->PSSetShaderResources(0, 1, &srv);

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
