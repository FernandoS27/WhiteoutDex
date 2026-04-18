#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Render Service
// Synchronous rendering API: InitDevice() + Tick() + RenderFrame()
// ============================================================================

#include "types.h"
#include "gfx/gfx.h"

#include "camera.h"
#include "animation.h"
#include "particle.h"
#include "particle/particle_service.h"
#include "ribbon.h"
#include "model_types.h"
#include "model_instance.h"
#include "content_provider.h"
#include "file_content_provider.h"
#include "render_target.h"

namespace WhiteoutDex::bls {
    class BlsShaderCache;
    class BlsProgramCatalog;
    class BlsPsoBuilder;
    struct BlsProgram;
}
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <deque>

namespace WhiteoutDex {

// Line vertex for grid/bone rendering
struct LineVertex {
    Vector3f position;
    Vector4f color;
};

// ============================================================================
// Render Service — synchronous rendering API
// ============================================================================

class RenderService {
public:
    RenderService();
    ~RenderService();

    // Camera control (thread-safe)
    void SetCamera(float pitch, float yaw, float distance,
                   float targetX, float targetY, float targetZ);
    Vector3f GetCameraPosition() const { return camera_.GetSource(); }

    // Model data (thread-safe — called from API/MaxScript thread)
    void ClearModel();

    // Multi-model API — returns handle for the loaded model
    uint32_t AddModel(const std::vector<MeshData>& meshes,
                      const std::vector<TextureData>& textures,
                      const std::vector<MaterialData>& materials,
                      const SkeletonData& skeleton,
                      const std::vector<SkinWeightData>& skinWeights,
                      const std::vector<ParticleEmitterConfig>& particles,
                      const std::vector<RibbonEmitterConfig>& ribbons,
                      const std::vector<CollisionShapeData>& collisions);
    void RemoveModel(uint32_t handle);
    void SetAttachmentConfigs(uint32_t handle, const std::vector<AttachmentConfig>& configs);
    void SetPE1Configs(uint32_t handle, const std::vector<PE1EmitterConfig>& configs);
    void SetPE1BasePath(const std::string& basePath);
    uint32_t GetFocusModelHandle() const { return focusModelHandle_; }

    // Access the unified file content provider (disk + CASC + MPQ)
    FileContentProvider& GetContentProvider() { return contentProvider_; }

    // Inject an external content provider (takes precedence over the built-in one)
    void SetContentProvider(std::shared_ptr<IContentProvider> provider);

    // ---- Device & render target management ----
    bool           InitDevice(gfx::GfxApi api = gfx::GfxApi::D3D12);  // Create gfx device + shaders + states (no window)
    RenderTargetId CreateSwapChainTarget(void* nativeWindowHandle, int width, int height);
    RenderTargetId CreateOffscreenTarget(int width, int height);
    void           DestroyRenderTarget(RenderTargetId id);
    void           ResizeRenderTarget(RenderTargetId id, int width, int height);
    void           RenderFrame(RenderTargetId targetId);
    void           Present(RenderTargetId targetId);
    bool           IsDeviceReady() const { return gfx_ != nullptr; }

    // Backward-compatible single-model API (operates on focus model)
    void LoadModel(const std::vector<MeshData>& meshes,
                   const std::vector<TextureData>& textures,
                   const std::vector<MaterialData>& materials,
                   const SkeletonData& skeleton,
                   const std::vector<SkinWeightData>& skinWeights,
                   const std::vector<ParticleEmitterConfig>& particles,
                   const std::vector<RibbonEmitterConfig>& ribbons,
                   const std::vector<CollisionShapeData>& collisions);

    // PE2 service path: register PlaneEmitter instances built from the MDX
    // adapter's GetPlaneEmitterInits() alongside the legacy ParticleSystem.
    // Simulation runs in Blizzard-native space. Passes each init through
    // particle::ApplyInit, then hands ownership to the internal
    // particle::ParticleService. Per-frame state (emissionRate, speed, width,
    // etc.) is fed via ApplyFrameState -> ApplyParticleFrameStates.
    void AddPlaneEmitters(uint32_t modelHandle,
                          const std::vector<particle::PlaneEmitterInit>& inits);

    // Apply pre-computed per-frame state
    void ApplyFrameState(uint32_t handle, const FrameState& state, int timeMs);
    void ApplyFrameState(const FrameState& state, int timeMs); // focus model

    // Update materials and textures without full model reload
    void UpdateMaterials(uint32_t handle, const std::vector<MaterialData>& materials,
                         const std::vector<TextureData>& textures);
    void UpdateMaterials(const std::vector<MaterialData>& materials,
                         const std::vector<TextureData>& textures); // focus model

    // Team color (RGB 0-255)
    void SetTeamColor(uint8_t r, uint8_t g, uint8_t b);

    // Camera presets (index 0 is always "Free Camera")
    void SetCameraPresets(const std::vector<CameraPreset>& presets);
    bool IsCameraLocked() const { return cameraLocked_; }
    void SetCameraLocked(bool locked) { cameraLocked_ = locked; }

    // Sequence picker (used by standalone viewer; safe to ignore from Max plugin)
    void SetSequences(const std::vector<std::string>& names);
    int  GetActiveSequenceIndex() const;
    void SetActiveSequence(int index) { activeSequence_ = index; }

    // ---- Camera manipulation (thread-safe, locks dataMutex_) ----
    void RotateCamera(int dx, int dy);
    void PanCamera(int dx, int dy);
    void ZoomCamera(int delta);
    void ZoomCameraSmooth(int dy);
    void ResetCamera();
    void SnapCameraToFace(int faceIndex);   // applies preset camera angle

    // ViewCube queries (called from RenderWindow message handlers)
    int  HitTestViewCube(int mx, int my);
    Rect GetViewCubeRect() const;
    void SetViewCubeHovered(bool hovered) { vcHovered_ = hovered; }

    // Display flags
    void SetDisplayFlags(const DisplayFlags& flags);
    DisplayFlags GetDisplayFlags() const;

    // Team color raw access (for platform swatch rendering). BGR-packed.
    uint32_t GetTeamColorRaw() const { return teamColor_; }
    // True if team color changed since last poll; clears the flag.
    bool ConsumeTeamColorDirty() { return teamColorDirty_.exchange(false); }

    // Pending data transfer (RenderWindow consumes from render thread)
    std::optional<std::vector<CameraPreset>> TakePendingCameraPresets();
    std::optional<std::vector<std::string>>  TakePendingSequences();

    // Resize the primary render target (called from WM_SIZE handler)
    void ResizePrimaryTarget(int width, int height);

    // Frame statistics (for title bar display)
    void GetFrameStats(int& geosets, int& textures, int& nodes,
                       int& particles, int& segments) const;

    // ---- Simulation ----
    // Call once per frame to advance simulation (particles, PE1, ribbons, etc.)
    void Tick(float dt);

    // Set animation time (ms) from external clock (Max timeline, game loop, etc.)
    void SetAnimationTime(int ms);
    int  GetAnimationTime() const;

    // Shut down device: release model GPU resources + gfx cleanup
    void ShutdownDevice();

    // Set the primary render target (used by ResizePrimaryTarget / RenderViewCube)
    void SetPrimaryTarget(RenderTargetId id) { primaryTargetId_ = id; }

private:
    void CleanupD3D();
    bool CreateShaders();
    bool CreatePipelines();
    bool CreateDefaultResources();

    // GPU resource management
    void ProcessStagedData();
    void UploadStagedTextures(ModelInstance& mi);
    void UploadStagedGeosets(ModelInstance& mi);
    void CreateNodePalette(ModelInstance& mi);
    void ReleaseModelGPU();

    // Animation update
    void UpdateAnimation();

    // Particle simulation + rendering
    void UpdateParticles(float dt);
    void RenderParticles();

    // Attachment model lifecycle
    void UpdateAttachments();

    // PE1: Model particle lifecycle
    void UpdatePE1(float dt);
    void EvaluatePE1Children();

    // Ribbon simulation + rendering
    void UpdateRibbons(float dt);
    void RenderRibbons();

    // Collision shape wireframes
    void RenderCollisions();

    // Debug light markers (one wireframe sphere per evaluated light at its
    // final world-space position; tinted by the light's diffuse colour).
    void RenderLightMarkers();

    // ApplyFrameState helpers
    void ApplyBoneMatrices(ModelInstance& mi, const FrameState& state);
    void ApplyGeosetStates(ModelInstance& mi, const FrameState& state);
    void ApplyLayerStates(ModelInstance& mi, const FrameState& state);
    void ApplyParticleFrameStates(ModelInstance& mi, const FrameState& state);
    void ApplyRibbonFrameStates(ModelInstance& mi, const FrameState& state);
    void ApplyPE1FrameStates(ModelInstance& mi, const FrameState& state);
    void ApplyAttachmentStates(ModelInstance& mi, const FrameState& state, int timeMs);

    // Team color
    void UpdateTeamColorTextures();

    // Rendering
    void RenderGrid();
    void RenderGeosets();
    gfx::PipelineHandle LookupMeshPSO(int filterMode, bool twoSided,
                                       bool noDepthTest, bool noDepthSet) const;

    // ViewCube (creation + rendering stay private; hit-test/snap/rect are public)
    bool CreateViewCube();
    void RenderViewCube();

    static int GetRenderOrder(int filterMode) {
        switch (filterMode) {
            case FILTER_NONE:        return 1;
            case FILTER_TRANSPARENT: return 2;
            case FILTER_BLEND:       return 3;
            default:                 return 4;
        }
    }

    // PE2 service — centralised registry for the new particle path. Coexists
    // with the legacy per-ModelInstance ParticleSystem until Phase 6 cut-over.
    particle::ParticleService particleService_;

    // Sync
    mutable std::mutex    dataMutex_;

    int                   width_ = 800;
    int                   height_ = 600;

    // Camera
    Camera                camera_;

    // ---- Display toggles ----
    bool                  showGrid_       = true;
    bool                  showParticles_  = true;
    bool                  showRibbons_    = true;
    bool                  showCollisions_ = false;  // off by default
    bool                  showLights_     = false;  // off by default

    // ---- Model instances ----
    uint32_t nextModelHandle_ = 1;
    std::unordered_map<uint32_t, std::unique_ptr<ModelInstance>> models_;
    uint32_t focusModelHandle_ = 0;

    // Helper: get focus model (may be null)
    ModelInstance* focusModel() const {
        auto it = models_.find(focusModelHandle_);
        return (it != models_.end()) ? it->second.get() : nullptr;
    }
    ModelInstance* getModel(uint32_t h) const {
        auto it = models_.find(h);
        return (it != models_.end()) ? it->second.get() : nullptr;
    }

    // ---- PE1 model template cache (PE1ModelTemplate defined in render_service.cpp) ----
    struct PE1ModelTemplate;
    std::unordered_map<std::string, std::shared_ptr<PE1ModelTemplate>> pe1TemplateCache_;
    static constexpr int kMaxPE1Depth = 3;
    static constexpr int kMaxPE1Instances = 256;
    int pe1InstanceCount_ = 0;

    std::string pe1BasePath_;  // root directory for resolving PE1 model + texture paths
    FileContentProvider contentProvider_; // unified file resolution (disk + CASC + MPQ)
    std::shared_ptr<IContentProvider> externalContentProvider_; // optional injected provider
    IContentProvider* activeContentProvider_ = nullptr;         // points to external or built-in
    std::shared_ptr<PE1ModelTemplate> getOrLoadTemplate(const std::string& modelPath);
    std::shared_ptr<PE1ModelTemplate> loadTemplateSync(const std::string& modelPath);
    void stageModelFromTemplate(ModelInstance* mi, const PE1ModelTemplate& tmpl);

    // ---- Async PE1 template loader ----
    void StartTemplateLoader();
    void StopTemplateLoader();
    void DrainTemplateResults();
    void TemplateLoaderFunc();

    std::thread                     templateLoaderThread_;
    std::atomic<bool>               templateLoaderRunning_{false};
    std::mutex                      templateQueueMutex_;
    std::condition_variable         templateQueueCV_;
    std::deque<std::string>         templateLoadQueue_;
    std::unordered_set<std::string> templateLoadPending_;   // paths queued or in-flight
    std::mutex                      templateResultMutex_;
    std::vector<std::pair<std::string, std::shared_ptr<PE1ModelTemplate>>> templateLoadResults_;

    // Team color (shared across all models). BGR-packed: 0x00BBGGRR.
    uint32_t teamColor_ = 0x000000FF;  // red
    std::atomic<bool> teamColorDirty_{false}; // polled by platform window for swatch repaint

    // Camera presets
    std::vector<CameraPreset> cameraPresets_;
    std::vector<CameraPreset> pendingCameraPresets_;
    bool cameraDirty_ = false;
    bool cameraLocked_ = false;

    // Sequence picker (standalone viewer)
    std::vector<std::string> pendingSequenceNames_;
    bool sequencesDirty_ = false;
    std::atomic<int> activeSequence_{0};

    // ---- GFX device ----
    std::unique_ptr<gfx::IGFXDevice> gfx_;

    // ---- Render targets ----
    std::unordered_map<RenderTargetId, RenderTarget> targets_;
    RenderTargetId nextTargetId_    = 1;
    RenderTargetId primaryTargetId_ = 0;  // set by RenderThread after creating the swap chain target

    // Helper: get primary target (may be null)
    RenderTarget* primaryTarget() {
        auto it = targets_.find(primaryTargetId_);
        return (it != targets_.end()) ? &it->second : nullptr;
    }

    // ---- GFX Shaders ----
    gfx::ShaderHandle meshVS_  = gfx::ShaderHandle::Invalid;
    gfx::ShaderHandle meshPS_  = gfx::ShaderHandle::Invalid;
    gfx::ShaderHandle lineVS_  = gfx::ShaderHandle::Invalid;
    gfx::ShaderHandle linePS_  = gfx::ShaderHandle::Invalid;
    gfx::ShaderHandle skinCS_  = gfx::ShaderHandle::Invalid;

    // ---- GFX Pipelines ----
    // Mesh: [7 filterModes][2 cull: 0=back, 1=none][3 depth: 0=default, 1=noWrite, 2=disabled]
    gfx::PipelineHandle meshPSO_[7][2][3] = {};
    gfx::PipelineHandle linePSO_  = gfx::PipelineHandle::Invalid;
    gfx::PipelineHandle skinPSO_  = gfx::PipelineHandle::Invalid;

    // ---- GFX Resources ----
    gfx::BufferHandle  cbPerFrame_     = gfx::BufferHandle::Invalid;
    gfx::SamplerHandle samplerLinear_  = gfx::SamplerHandle::Invalid;
    gfx::SamplerHandle samplerWrap_[4] = {};
    gfx::TextureHandle defaultTex_     = gfx::TextureHandle::Invalid;

    // Grid
    gfx::BufferHandle gridVB_ = gfx::BufferHandle::Invalid;
    int               gridVertCount_ = 0;

    // Global particle VB — used by the PE2 service's draw path.
    // Grows on demand; sized in Vertex units.
    gfx::BufferHandle particleServiceVB_     = gfx::BufferHandle::Invalid;
    int               particleServiceVBSize_ = 0;

    // ViewCube
    gfx::BufferHandle  vcCubeVB_     = gfx::BufferHandle::Invalid;
    gfx::BufferHandle  vcCubeIB_     = gfx::BufferHandle::Invalid;
    gfx::BufferHandle  vcOutlineVB_  = gfx::BufferHandle::Invalid;
    gfx::BufferHandle  vcHomeVB_     = gfx::BufferHandle::Invalid;
    gfx::TextureHandle vcFaceTex_    = gfx::TextureHandle::Invalid;
    static constexpr int kViewCubeSize = 120;
    bool               vcHovered_    = false;

    // Animation time (set from API thread via SetAnimationTime / ApplyFrameState,
    //                  read from render thread in Tick / EvaluatePE1Children)
    std::atomic<int>        animationTimeMs_{0};

    // ---- BLS shader pipeline (docs/BLS_ShaderABI.md) ----
    // Loaded lazily at InitDevice. If any .bls file is missing the program
    // stays null and the renderer falls back to the Slang path.
    std::unique_ptr<bls::BlsShaderCache>    blsShaderCache_;
    std::unique_ptr<bls::BlsProgramCatalog> blsPrograms_;
    std::unique_ptr<bls::BlsPsoBuilder>     blsPsoBuilder_;
    const bls::BlsProgram*                  blsSdProgram_     = nullptr; // SD_HighSpec VS + SD PS
    const bls::BlsProgram*                  blsSdOnHdProgram_ = nullptr; // SD_on_HD VS + SD_on_HD PS

    // Dynamic CBs, one-each for the full SD/SD_on_HD ABI. Sized for
    // numLights=8 worst case (720 B PS CB). Uploaded per draw.
    gfx::BufferHandle                       blsSdVsCb_ = gfx::BufferHandle::Invalid;
    gfx::BufferHandle                       blsSdPsCb_ = gfx::BufferHandle::Invalid;

    bool InitBlsShaders();
    void ShutdownBlsShaders();
    bool RenderParticlesBls();  // BLS path; returns false if program unavailable
    bool RenderGeosetsBls();    // ditto for mesh geosets
};

} // namespace WhiteoutDex
