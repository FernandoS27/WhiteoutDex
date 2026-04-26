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
#include "actor_manager.h"
#include "scene_manager.h"
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

namespace WhiteoutDex {
    class SamplerAssetManager;
    class TextureAssetManager;
    class ReplaceableTextureManager;
    class ModelTemplateManager;
    class SceneManager;
    // Cross-instance model template (definition in renderer/model_template.h).
    struct ModelTemplate;
}
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
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
    // Default ctor: creates an internal SceneManager (back-compat).
    RenderService();
    // Host-owned scene: pass a SceneManager that outlives the renderer.
    // The renderer holds a non-owning pointer; lifetime contract is the host's.
    explicit RenderService(SceneManager& scene);
    ~RenderService();

    // ---- Device & render target management ----
    bool           InitDevice(gfx::GfxApi api = gfx::GfxApi::D3D12);
    RenderTargetId CreateSwapChainTarget(void* nativeWindowHandle, int width, int height);
    RenderTargetId CreateOffscreenTarget(int width, int height);
    void           DestroyRenderTarget(RenderTargetId id);
    void           ResizeRenderTarget(RenderTargetId id, int width, int height);
    void           RenderFrame(RenderTargetId targetId);
    void           Present(RenderTargetId targetId);
    bool           IsDeviceReady() const { return gfx_ != nullptr; }

    // ---- Per-actor state feed (focus actor) ----
    //
    // Per-frame FrameState evaluation + apply happens automatically inside
    // Tick() via EvaluateTopLevelActors / EvaluatePE1Children. The host loop
    // just drives `SceneManager::Update(dt)` (or, for externally-timed hosts
    // like the Max plugin, writes `actor.animation.SetTimeMs(...)` directly
    // when the timeline ticks). UpdateMaterials stays as a focus-actor
    // forwarder for the Max plugin's hot-reload path.
    void UpdateMaterials(const std::vector<MaterialData>& materials,
                         const std::vector<TextureData>& textures);

    // ---- Lifecycle ----
    void ClearModel();   // marks every actor for clear; ProcessStagedData destroys them next tick

    // ---- Team color (UI inbox under dataMutex_; re-bakes per-model SD slots) ----
    // Component-direct alternative: Replaceables().SetTeamColor() — but it's
    // not currently thread-safe for cross-actor bakes, so this forwarder
    // takes the renderer's mutex around the whole operation.
    void SetTeamColor(uint8_t r, uint8_t g, uint8_t b);

    // ---- Camera presets + sequence picker UI inbox (RenderWindow + dataMutex_) ----
    // These wrap SceneManager calls in the renderer's dataMutex_ so the
    // RenderWindow's UI thread doesn't race the render thread. A scene-side
    // thread-safe API is the long-term fix.
    void ActivateCameraPreset(int idx);
    int  GetActiveSequenceIndex() const;
    void SetActiveSequence(int index);
    std::optional<std::vector<CameraPreset>> TakePendingCameraPresets();
    std::optional<std::vector<std::string>>  TakePendingSequences();

    // ---- Camera manipulation (thread-safe; same dataMutex_ rationale) ----
    void RotateCamera(int dx, int dy);
    void PanCamera(int dx, int dy);
    void ZoomCamera(int delta);
    void ZoomCameraSmooth(int dy);
    void ResetCamera();
    void SnapCameraToFace(int faceIndex);

    // ---- Display + render-policy state ----
    void SetDisplayFlags(const DisplayFlags& flags);
    DisplayFlags GetDisplayFlags() const;
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

    // Resize the primary render target (called from WM_SIZE handler)
    void ResizePrimaryTarget(int width, int height);

    // Frame statistics (for title bar display)
    void GetFrameStats(int& geosets, int& textures, int& nodes,
                       int& particles, int& segments) const;

    // ---- Simulation ----
    // Call once per frame to advance simulation (particles, PE1, ribbons, etc.)
    void Tick(float dt);

    // Shut down device: release model GPU resources + gfx cleanup
    void ShutdownDevice();

    // Set the primary render target (used by ResizePrimaryTarget / RenderViewCube)
    void SetPrimaryTarget(RenderTargetId id) { primaryTargetId_ = id; }

    // ---- Component accessors (preferred entry points) ----
    //
    // The renderer is a thin facade over component subsystems. Most of the
    // legacy `RenderService::SetCamera/SetTeamColor/...` forwarders call into
    // these — calling the components directly skips the indirection and lets
    // each subsystem evolve its own API without churning RenderService too.
    //
    // Lifetime: every accessor returns a reference to a member that lives for
    // the renderer's lifetime. Asset-manager accessors may dereference null
    // before `InitDevice` succeeds — the device-related ones (Samplers,
    // Textures, Replaceables, Debug, Gfx) require the device to be ready.
    SceneManager&              Scene()       { return *scene_; }
    const SceneManager&        Scene() const { return *scene_; }
    SamplerAssetManager&       Samplers()    { return *samplers_; }
    TextureAssetManager&       Textures()    { return *textures_; }
    ReplaceableTextureManager& Replaceables(){ return *replaceables_; }
    DebugRenderer&             Debug()       { return *debug_; }
    gfx::IGFXDevice&           Gfx()         { return *gfx_; }

    // ---- High-level Actor spawn (preferred over AddModel/LoadModel) ----
    //
    // SpawnActorFromMdx: parses + caches the MDX (no-op on cache hit), creates
    // an Actor, returns it. Returns nullptr on parse/load failure. Sets focus
    // if no focus actor exists yet.
    //
    // LoadActorFromMdx: clears existing actors first, sets focus on the new
    // one, and auto-flips renderMode to HD when any layer ships a non-SD
    // shader. Mirrors the LoadModelByPath semantics.
    //
    // SpawnActorFromLiveSource: for adapters that drive both static data AND
    // animation through one object (the Max plugin's MaxSceneAdapter is the
    // only one today). Calls source->Build() once for the static snapshot,
    // adopts it as a template, and binds the actor's AnimationDriver to the
    // source. The host owns the source and keeps it alive — actor holds a
    // shared_ptr to it through AnimationDriver.
    Actor* SpawnActorFromMdx(const std::string& mdxPath);
    Actor* LoadActorFromMdx(const std::string& mdxPath);
    Actor* SpawnActorFromLiveSource(std::shared_ptr<IModelSource> source);

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
    // Granular static-snapshot path — used by SpawnActorFromLiveSource (the
    // Max plugin's adapter feeds an already-built ModelData through here)
    // and by the path-based spawners after parsing an MDX into ModelData.
    uint32_t AddModel(const std::vector<MeshData>& meshes,
                      const std::vector<TextureData>& textures,
                      const std::vector<MaterialData>& materials,
                      const SkeletonData& skeleton,
                      const std::vector<SkinWeightData>& skinWeights,
                      const std::vector<ParticleEmitterConfig>& particleConfigs,
                      const std::vector<RibbonEmitterConfig>& ribbonConfigs,
                      const std::vector<CollisionShapeData>& collisions);

    // Per-handle config setters — applied right after AddModel by the spawn
    // helpers. External callers don't need them: SpawnActorFromLiveSource
    // folds attachments + PE1 emitters into the spawn, and the path-based
    // spawners pull the data straight from the ModelTemplate.
    void SetAttachmentConfigs(uint32_t handle,
                              const std::vector<AttachmentConfig>& configs);
    void SetPE1Configs(uint32_t handle,
                       const std::vector<PE1EmitterConfig>& configs);

    // Path-based load — internal helpers backing the public Spawn/Load
    // *FromMdx methods. Return uint32_t handles; the public API converts
    // those into Actor* via scene_->Actors().Find(h).
    uint32_t AddModelByPath(const std::string& mdxPath);
    uint32_t LoadModelByPath(const std::string& mdxPath);

    // ApplyFrameState by handle — used internally by EvaluatePE1Children
    // (each PE1 child gets its own FrameState via its IAnimationSource).
    // Public callers go through ApplyFrameState(state, time) on the focus.
    void ApplyFrameState(uint32_t handle, const FrameState& state, int timeMs);

    // UpdateMaterials by handle — used by the focus-form forwarder.
    void UpdateMaterials(uint32_t handle, const std::vector<MaterialData>& materials,
                         const std::vector<TextureData>& textures);

    // Cross-model texture-cache query for the template manager's adapter
    // wiring. External callers (Max plugin) use Textures().IsCachedShared()
    // directly.
    bool IsTextureCached(std::string_view key) const;

    void CleanupD3D();
    bool CreateShaders();
    bool CreatePipelines();
    bool CreateDefaultResources();

    // GPU resource management
    void ProcessStagedData();
    void UploadStagedTextures(Actor& mi);
    void UploadStagedGeosets(Actor& mi);
    void CreateNodePalette(Actor& mi);
    void ReleaseModelGPU();

    // Animation update
    void UpdateAnimation();

    // Particle simulation + rendering
    void UpdateParticles(float dt);

    // Attachment model lifecycle
    void UpdateAttachments();

    // PE1: Model particle lifecycle
    void UpdatePE1(float dt);
    void EvaluatePE1Children();

    // Top-level (non-PE1) actor evaluation. SceneManager::Update has already
    // looped each actor's animation cursor; this just snapshots, evaluates,
    // and applies the resulting FrameState through ApplyFrameState(handle).
    void EvaluateTopLevelActors();

    // Ribbon simulation + rendering
    void UpdateRibbons(float dt);
    void RenderRibbons();

    // LOD selection: -1 override -> screen-size computation (mirrors
    // Previewd CalculateLOD @0x140305ae0). Returns 0..3. Models without
    // LOD data always get 0 — caller checks mi.render.hasLods to decide which
    // to use, then tests each geoset with GeosetPassesLod.
    int  ComputeSelectedLod() const;

    // ApplyFrameState helpers — only the externally-coupled ones live on
    // RenderService; pure per-actor methods (Geoset/Layer/Ribbon/PE1) are
    // RenderModel members. Bones need camera_, particles need particleService_,
    // attachments need cross-actor lookups.
    void ApplyBoneMatrices(Actor& mi, const FrameState& state);
    void ApplyParticleFrameStates(Actor& mi, const FrameState& state);
    void ApplyAttachmentStates(Actor& mi, const FrameState& state, int timeMs);

    // Team color logic moved into ReplaceableTextureManager. The HD draw
    // path obtains the live swatch through replaceables_->GetHdSwatchTexture().

    // Rendering. SD mesh draws flow through BLS (blsSdProgram_ / blsHdProgram_
    // / blsSdOnHdProgram_); particles and ribbons do the same; no legacy
    // Slang mesh PSO exists any more.
    void RenderGeosets();

    // PE2 service — centralised registry for the new particle path. Coexists
    // with the legacy per-Actor ParticleSystem until Phase 6 cut-over.
    particle::ParticleService particleService_;

    // Sync
    mutable std::mutex    dataMutex_;

    int                   width_ = 800;
    int                   height_ = 600;

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

    // ---- Scene state ----
    // Phase 5: SceneManager owns actors, focus, camera, camera presets,
    // sequence picker UI inbox, the animation clock, content providers,
    // template manager, and PE1 spawn state. RenderService forwards through
    // `scene_->...` for any access.
    //
    // Phase 5 v3 split storage: host can pass its own SceneManager (owns
    // it externally) — `ownedScene_` stays null and `scene_` aims at the
    // host's instance. Default ctor allocates `ownedScene_` and aims `scene_`
    // at it; same external API either way.
    std::unique_ptr<SceneManager> ownedScene_;
    SceneManager*                 scene_ = nullptr;

    // Helper: get focus model (may be null). The free function form survives
    // because most call sites are written as `auto* mi = focusModel(); ...`.
    Actor* focusModel() const;
    Actor* getModel(uint32_t h) const;

    // ---- Asset resolution + template cache + PE1 spawn limits ----
    // All moved into SceneManager in Phase 5 v2 — access via `scene_->...`.

    void stageModelFromTemplate(Actor* mi,
                                std::shared_ptr<ModelTemplate> tmpl);
    // Lazy upload of a template's shared geometry buffers. Idempotent: the
    // first call uploads, every subsequent call is a no-op flag check.
    // Render-thread only (called from UploadStagedGeosets).
    void uploadTemplateGpu(ModelTemplate& tmpl);
    // Replaceable-texture registration moved to ReplaceableTextureManager.
    // Call sites use replaceables_->RegisterModelSlot(*mi, textureId, kind).

    // Team-colour state lives in ReplaceableTextureManager (see replaceables_).
    //
    // Camera, camera presets, sequence picker UI inbox, and animation clock
    // moved to SceneManager (`scene_`) in Phase 5.

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
    // Only line.slang (debug overlay) and viewcube.slang (DebugRenderer-owned)
    // Slang shaders survive; SD mesh rendering is entirely BLS now.
    gfx::ShaderHandle lineVS_  = gfx::ShaderHandle::Invalid;
    gfx::ShaderHandle linePS_  = gfx::ShaderHandle::Invalid;

    // ---- GFX Pipelines ----
    gfx::PipelineHandle linePSO_  = gfx::PipelineHandle::Invalid;

    // ---- GFX Resources ----
    gfx::BufferHandle  cbPerFrame_     = gfx::BufferHandle::Invalid;
    // Sampler + texture ownership has moved into the asset managers.
    // SamplerAssetManager hands out wrap-flag + linear samplers on demand.
    // TextureAssetManager owns the 5 default 1x1 fallbacks (white/black/
    // flat-normal/neutral-orm/missing-magenta); see GetDefaults().
    std::unique_ptr<SamplerAssetManager>       samplers_;
    std::unique_ptr<TextureAssetManager>       textures_;
    // ReplaceableTextureManager owns the team-colour state, the per-model
    // SD-slot registry, and the live HD swatch texture bound at t4. See
    // its header for the threading model.
    std::unique_ptr<ReplaceableTextureManager> replaceables_;

    // Debug-overlay passes (grid, collision wireframes, light markers,
    // ViewCube). Owns its own GPU resources; accesses shared RenderService
    // state (cbPerFrame_, samplers, linePSO_, ...) via friendship.
    std::unique_ptr<DebugRenderer> debug_;

    // Global particle VB — used by the PE2 service's draw path.
    // Grows on demand; sized in Vertex units.
    gfx::BufferHandle particleServiceVB_     = gfx::BufferHandle::Invalid;
    int               particleServiceVBSize_ = 0;

    // Animation clock moved to SceneManager (`scene_->GetAnimationTime()`).

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

    // IBL resources for the HD path. Owned by TextureAssetManager via
    // RegisterOwned under the names below; bind sites resolve them with
    // GetOwned at draw time. Aliasing convention: when the "to" probe load
    // fails we leave kIblToName unregistered and the bind site reuses the
    // "from" handle so t14 still binds.
    //   kIblSplitSumLutName : 128x128 BRDF pre-integral at t15 (CPU-generated)
    //   kIblFromProbeName   : TextureCubeArray at t13 (Day_IBL.dds | debug cube)
    //   kIblToProbeName     : TextureCubeArray at t14 (Night_IBL.dds; optional)
    //   iblProbeMipEnd_     : actual loaded mip count minus one; feeds
    //                         envFromMipEnd / envToMipEnd so the PS's
    //                         roughness→mip remap clamps correctly.
    static constexpr const char* kIblSplitSumLutName = "ibl.splitSumLut";
    static constexpr const char* kIblFromProbeName   = "ibl.fromProbe";
    static constexpr const char* kIblToProbeName     = "ibl.toProbe";
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
