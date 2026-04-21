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
#include "model_source.h"
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

// Forward declarations for the CRTP pass classes (render_pass.h).
// The base reads RenderService members directly via friendship; the two
// derived classes live in render_service.cpp where the concrete bodies
// need to sit next to ApplyBoneMatrices et al.
template <class> class BlsGeosetPass;
class GeosetPassBls;
class GeosetPassHd;
class DebugRenderer;  // debug/debug_renderer.h — overlay passes (grid, collisions, light markers, ViewCube)

class RenderService {
    template <class> friend class BlsGeosetPass;
    friend class GeosetPassBls;
    friend class GeosetPassHd;
    friend class DebugRenderer;
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

    // Activate an MDX preset (Direct mode). idx = -1 reverts to orbital.
    void ActivateCameraPreset(int idx);

    // Sequence picker (standalone viewer; Max plugin ignores).
    void SetSequences(const std::vector<std::string>& names);
    // Required for MDX camera animation playback — without frame
    // ranges the animator bails to keyframe 0.
    void SetSequenceRanges(const std::vector<IModelSource::SequenceInfo>& ranges);
    int  GetActiveSequenceIndex() const;
    void SetActiveSequence(int index) { activeSequence_ = index; }

    // ---- Camera manipulation (thread-safe, locks dataMutex_) ----
    void RotateCamera(int dx, int dy);
    void PanCamera(int dx, int dy);
    void ZoomCamera(int delta);
    void ZoomCameraSmooth(int dy);
    void ResetCamera();
    void SnapCameraToFace(int faceIndex);   // applies preset camera angle

    // ViewCube queries (called from RenderWindow message handlers).
    // Implemented in render_service.cpp — they forward to debug_.
    int  HitTestViewCube(int mx, int my);
    Rect GetViewCubeRect() const;
    void SetViewCubeHovered(bool hovered);

    // Display flags
    void SetDisplayFlags(const DisplayFlags& flags);
    DisplayFlags GetDisplayFlags() const;

    // Team color raw access (for platform swatch rendering). BGR-packed.
    uint32_t GetTeamColorRaw() const { return teamColor_; }
    // True if team color changed since last poll; clears the flag.
    bool ConsumeTeamColorDirty() { return teamColorDirty_.exchange(false); }
    // True if LoadModel auto-flipped renderMode_ (e.g. HD on non-SD
    // materials); the UI polls this to re-sync its HD checkbox.
    bool ConsumeRenderModeDirty() { return renderModeDirty_.exchange(false); }

    // HD debug visualisation. Mode 0 = normal render (default),
    // 1 = albedo, 2 = world normal, 3 = LOD heatmap, 4 = light count.
    // Only takes effect on HD-program draws. Matches the HAS_DEBUG_VIS
    // permute's `psCB3.debugMode` switch in ps_post.slang::debugVisualize.
    void SetHdDebugMode(int mode) { hdDebugMode_.store(mode); }
    int  GetHdDebugMode() const   { return hdDebugMode_.load(); }

    // LOD override. -1 = auto (screen-size driven, mirrors Previewd's
    // CalculateLOD @0x140305ae0). 0..3 = force that LOD level. Geosets
    // with lod = 0xFFFFFFFF are always drawn regardless. Applies to all
    // render paths (HD / BLS / legacy).
    void SetLodOverride(int lod) { lodOverride_.store(lod); }
    int  GetLodOverride() const  { return lodOverride_.load(); }

    // Swap the HD IBL probe at runtime. `relPath` is the CASC-relative
    // path ("environment/environmentmap/.../foo_ibl.dds"). Empty or a
    // load failure reverts to the built-in debug probe so HAS_IBL
    // draws keep sampling something valid.
    void SetEnvProbe(const std::string& relPath);

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

    // ---- Static helpers (public so free-function render helpers can reuse) ----
    // LOD test: geosets flagged with the always-draw sentinel pass every level;
    // otherwise only the geoset whose lod matches the currently selected level.
    static bool GeosetPassesLod(uint32_t geosetLod, int selectedLod) {
        return geosetLod == 0xFFFFFFFFu || (int)geosetLod == selectedLod;
    }
    // Render-order bucket for the global sort: opaque → masked → blend → other.
    static int GetRenderOrder(int filterMode) {
        switch (filterMode) {
            case FILTER_NONE:        return 1;
            case FILTER_TRANSPARENT: return 2;
            case FILTER_BLEND:       return 3;
            default:                 return 4;
        }
    }

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

    // LOD selection: -1 override -> screen-size computation (mirrors
    // Previewd CalculateLOD @0x140305ae0). Returns 0..3. Models without
    // LOD data always get 0 — caller checks mi.hasLods to decide which
    // to use, then tests each geoset with GeosetPassesLod.
    int  ComputeSelectedLod() const;

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
    // Refresh/create the RenderService-owned 1x1 team-colour swatch.
    // Called lazily on the render thread whenever the picker changes.
    void UpdateTeamColorSwatch();

    // Rendering
    void RenderGeosets();
    gfx::PipelineHandle LookupMeshPSO(int filterMode, bool twoSided,
                                       bool noDepthTest, bool noDepthSet) const;

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
    // Global render pipeline selector. Mirrors Previewd's GxDevRenderMode():
    // flipping to HD causes MatSelect-style canonicalisation in the mesh draw
    // path (SD/SD_on_HD route through sd_on_hd.bls, HD/Crystal through hd.bls).
    RenderMode            renderMode_     = RenderMode::SD;
    // Set by LoadModel when it auto-activates HD; consumed by the UI
    // loop to re-sync the HD checkbox. Atomic so the UI can poll
    // without locking the data mutex.
    std::atomic<bool>     renderModeDirty_{false};
    // 0 = off (normal render). See SetHdDebugMode() for the palette.
    std::atomic<int>      hdDebugMode_{0};
    // LOD override: -1 = auto (screen-size), 0..3 = force that LOD
    std::atomic<int>      lodOverride_{-1};

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
    int  activeCameraPresetIdx_ = -1;  // -1 = free camera

    // Sequence picker (standalone viewer)
    std::vector<std::string> pendingSequenceNames_;
    bool sequencesDirty_ = false;
    std::atomic<int> activeSequence_{0};
    std::vector<IModelSource::SequenceInfo> sequenceRanges_;

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
    gfx::TextureHandle defaultTex_     = gfx::TextureHandle::Invalid;  // 1x1 white (t0 albedo fallback)
    gfx::TextureHandle defaultBlack_   = gfx::TextureHandle::Invalid;  // 1x1 RGBA(0,0,0,0) — t3 emissive / t4 teamColor fallback (zero contribution).
    gfx::TextureHandle defaultOrm_     = gfx::TextureHandle::Invalid;  // 1x1 ORM neutral: occlusion=1, roughness=1, metalness=0, teamBlend=0. Roughness=1 matters — a 0 there turns every unauthored-ORM HD material into a perfect mirror and the IBL cubemap's horizon shows up as a sharp reflection line through the view centre (exactly the "seam" we've been chasing).
    gfx::TextureHandle defaultNormal_  = gfx::TextureHandle::Invalid;  // 1x1 flat normal. Shader does nx = 2*r*a - 1 -> pack r=0.5, a=1.0 so decodeNormalMap yields (0,0,1).

    // Dynamic team-colour swatch (1x1 RGBA8). Bound at t4 for every HD
    // draw whose layer authors a TeamColor subtexture — the engine does
    // the same: it ignores the MDX's referenced team-colour texture and
    // swaps in a per-player tint. Recreated on SetTeamColor so the UI
    // picker drives it directly without depending on replaceableId=1.
    gfx::TextureHandle teamColorTex_   = gfx::TextureHandle::Invalid;
    uint32_t           teamColorTexColor_ = 0xFFFFFFFFu;

    // Debug-overlay passes (grid, collision wireframes, light markers,
    // ViewCube). Owns its own GPU resources; accesses shared RenderService
    // state (cbPerFrame_, samplers, linePSO_, ...) via friendship.
    std::unique_ptr<DebugRenderer> debug_;

    // Global particle VB — used by the PE2 service's draw path.
    // Grows on demand; sized in Vertex units.
    gfx::BufferHandle particleServiceVB_     = gfx::BufferHandle::Invalid;
    int               particleServiceVBSize_ = 0;

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
    const bls::BlsProgram*                  blsHdProgram_     = nullptr; // HD VS + HD PS (full PBR)

    // HD CB pool (path B). Sized for 8-light worst case like Path A; the
    // actual upload size is dynamic via HdPsCbSize/SdOnHdPsCbSize.
    gfx::BufferHandle                       blsHdVsCb_        = gfx::BufferHandle::Invalid;
    gfx::BufferHandle                       blsHdPsCb_        = gfx::BufferHandle::Invalid;
    gfx::BufferHandle                       blsSdOnHdPsCb_    = gfx::BufferHandle::Invalid;
    // PS b3 debug-vis CB; only bound when the HAS_DEBUG_VIS permute
    // is active (rs.debugShader = true in the HD draw path).
    gfx::BufferHandle                       blsHdDebugVisCb_  = gfx::BufferHandle::Invalid;

    // IBL resources for the HD path.
    //   iblSplitSumLut_ : 128x128 BRDF pre-integral at t15 (CPU-generated).
    //   iblFromProbe_   : TextureCubeArray at t13 -- "from" probe, loaded
    //                     from the game's Day_IBL.dds when available;
    //                     falls back to a small procedural grey cube.
    //   iblToProbe_     : TextureCubeArray at t14 -- "to" probe, same
    //                     loader but pointed at Night_IBL.dds. If loading
    //                     fails we reuse iblFromProbe_ so t14 still binds.
    //   iblProbeMipEnd_ : actual loaded mip count minus one; feeds
    //                     envFromMipEnd / envToMipEnd so the PS's
    //                     roughness->mip remap clamps correctly.
    gfx::TextureHandle                      iblSplitSumLut_   = gfx::TextureHandle::Invalid;
    gfx::TextureHandle                      iblFromProbe_     = gfx::TextureHandle::Invalid;
    gfx::TextureHandle                      iblToProbe_       = gfx::TextureHandle::Invalid;
    float                                   iblProbeMipEnd_   = 0.0f;

    // Dynamic CBs, one-each for the full SD/SD_on_HD ABI. Sized for
    // numLights=8 worst case (720 B PS CB). Uploaded per draw.
    gfx::BufferHandle                       blsSdVsCb_ = gfx::BufferHandle::Invalid;
    gfx::BufferHandle                       blsSdPsCb_ = gfx::BufferHandle::Invalid;  // 48 B SDClassicPSPerDraw

    bool InitBlsShaders();
    void ShutdownBlsShaders();
    bool RenderParticlesBls();  // BLS path; returns false if program unavailable
    bool RenderGeosetsBls();    // SD-mode mesh geosets (Path A: SD_HighSpec + SD)
    bool RenderGeosetsHd();     // HD-mode mesh geosets (Path B: HD / SD_on_HD programs)
};

} // namespace WhiteoutDex
