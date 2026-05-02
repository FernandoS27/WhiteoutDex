#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Render Service
// Synchronous rendering API: InitDevice() + Tick() + RenderFrame()
// ============================================================================

#include "types.h"
#include "gfx/gfx.h"
#include "../io/replaceable_paths.h"   // io::Tileset (SetTileset façade)

#include "camera.h"
#include "animation.h"
#include "particle.h"
#include "particle/particle_service.h"
#include "particle/splat_service.h"
#include "dnc/dnc_service.h"
#include "shadow/shadow_service.h"
#include "ribbon.h"
#include "sound_emitter.h"
#include "spn_spawner.h"
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
    struct BlsShader;
}

namespace WhiteoutDex {
    class SamplerAssetManager;
    class TextureAssetManager;
    class ReplaceableTextureManager;
    class ModelTemplateManager;
    class SceneManager;
    // Cross-instance model template (definition in renderer/model_template.h).
    struct ModelTemplate;
    namespace shadow { class ShadowPass; }
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
enum class GeosetBucket : uint8_t;
class DebugRenderer;  // debug/debug_renderer.h — overlay passes (grid, collisions, light markers, ViewCube)
class SpnSpawner;     // renderer/spn_spawner.h — sub-MDX EventObject spawns

class RenderService {
    template <class> friend class BlsGeosetPass;
    friend class GeosetPassBls;
    friend class GeosetPassHd;
    friend class DebugRenderer;
    friend class SpnSpawner;
    friend class shadow::ShadowPass;
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

    // ---- Tileset selector (drives canonical replaceable paths for ids 11..36) ----
    // Pushes the new tileset through io::SetCurrentTileset and triggers
    // re-bake of every per-model slot whose replaceableId falls in
    // 11..36. TeamColor / TeamGlow swatches are tileset-independent and
    // unaffected.
    void        SetTileset(io::Tileset ts);
    io::Tileset GetTileset() const;

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

    // Lighting mode. Drives how BuildLightPalette mixes the renderer's
    // baseline headlight with the model's authored MDX lights — see the
    // LightingMode enum in render_target.h. Default: InGame (baseline
    // always present; authored MDX lights stack on top).
    void         SetLightingMode(LightingMode m) { lightingMode_.store(static_cast<uint8_t>(m)); }
    LightingMode GetLightingMode() const         { return static_cast<LightingMode>(lightingMode_.load()); }

    // Scene clear color. Stored as sRGB bytes packed COLORREF-style
    // (0x00BBGGRR) for direct interop with the Win32 colour picker.
    // The render thread converts to linear floats before clearing the
    // HDR scene target. Default mirrors the previous hard-coded
    // (0.06, 0.07, 0.10) linear value.
    void     SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b);
    uint32_t GetBackgroundColorRaw() const { return backgroundColor_.load(); }

    // Swap the HD IBL probe at runtime. `relPath` is the CASC-relative
    // path ("environment/environmentmap/.../foo_ibl.dds"). Empty or a
    // load failure reverts to the built-in debug probe so HAS_IBL
    // draws keep sampling something valid.
    void SetEnvProbe(const std::string& relPath);

    // Load both Day and Night IBL cubemaps as a coherent pair. When
    // both succeed and LightingMode is InGame, the HD pass binds the
    // pair at t13/t14 (with the from/to order picked per current TOD)
    // and packs the DncService's transitionT into the env-map CB.
    // Either path empty / load failure leaves the legacy single-probe
    // behaviour in place. Default paths in `dnc::DncService` resolve
    // through the active content provider.
    void SetDayNightProbes(const std::string& dayPath,
                           const std::string& nightPath);

    // High-level switch between Portrait probe (single, isotropic,
    // default) and the engine's Day/Night pair (TOD-blended). Applies
    // the change immediately by calling SetEnvProbe or
    // SetDayNightProbes under the hood. Persists across InitBlsShaders
    // re-init. See render_target.h for the enum's docstring.
    void    SetIblMode(IblMode mode);
    IblMode GetIblMode() const { return iblMode_; }

private:
    // Issue the SetEnvProbe / SetDayNightProbes calls for the requested
    // mode. Shared between SetIblMode (runtime swap) and InitBlsShaders
    // (re-apply after device init / render-mode swap).
    void ApplyIblMode(IblMode mode);

public:

    // Format / texture used as the colour render target for the active
    // render mode. HD goes through `target.hdrColor` (R11G11B10F) and is
    // ACES-tonemapped onto `target.color` afterwards; SD writes directly
    // to `target.color` (R8G8B8A8_UNORM) and skips the tonemap pass.
    // The PSO builder hashes rtvFormat into its cache key so per-mode
    // switches transparently produce distinct PSOs on first use.
    gfx::Format         SceneTargetFormat() const {
        return renderMode_ == RenderMode::HD ? kHdrSceneFormat : kSdSceneFormat;
    }

    // Pick the static line PSO matching the current scene target. Used
    // by DebugRenderer's grid / wireframe / viewcube outline draws, and
    // by anything else that wants to draw line lists into the scene RT
    // without going through the BLS PSO cache.
    gfx::PipelineHandle CurrentLinePSO() const;

    // Tonemap exposure — pre-ACES multiplier the engine uploads at PS b1
    // (CGxDevice::ApplyTonemap @0x7ff609aff760 → tonemapPsCB1.exposure).
    // Larger = brighter image, smaller = darker. Engine default is 1.0;
    // we bias slightly lower to compensate for HD scenes that already
    // run hot under our IBL probe. Read by RunTonemapPass each frame.
    void  SetTonemapExposure(float exposure) { tonemapExposure_ = exposure; }
    float GetTonemapExposure() const         { return tonemapExposure_; }

    // Global "force NonLooping animations to loop" toggle. Stamps the
    // matching `Actor::ignoreNonLooping` flag onto every existing
    // top-level (non-PE1, non-attachment) actor and onto every actor
    // freshly spawned through Load/SpawnActor*. PE1 / attachment
    // children are intentionally skipped — their own per-actor flag
    // stays at its default false so a parent's loop choice doesn't
    // leak into emitted decals or particle children.
    void SetIgnoreNonLooping(bool on);
    bool GetIgnoreNonLooping() const { return ignoreNonLooping_; }

    // Day/Night-Cycle service. Constructed lazily once a content
    // provider is available (see InitBlsShaders). Hosts use it to
    // drive the TOD slider, query the current ambient/diffuse for
    // tooling, or override the active DNC MDL. Returns nullptr until
    // the service is wired up — callers should null-check.
    dnc::DncService*       GetDncService()       { return dncService_.get(); }
    const dnc::DncService* GetDncService() const { return dncService_.get(); }

    // Gfx device accessor for satellite passes (shadow pass) that
    // need to issue command-list ops without being friends of
    // RenderService. Returns nullptr before InitBlsShaders runs.
    gfx::IGFXDevice*       GetGfxDevice()       { return gfx_.get(); }
    const gfx::IGFXDevice* GetGfxDevice() const { return gfx_.get(); }

    // Shadow service. Constructed by InitBlsShaders; nullptr before
    // then. Driving the master toggle + cascade count from the host
    // is straightforward — see SetEnabled / SetParams on the service.
    shadow::ShadowService*       GetShadowService()       { return shadowService_.get(); }
    const shadow::ShadowService* GetShadowService() const { return shadowService_.get(); }

    // Host-supplied audio backend for MDX SND EventObjects. Default is
    // a NullSoundEmitter that drops every fire — the renderer library
    // itself ships no platform audio dependencies. Standalone exe and
    // Max plugin each install a Windows-specific implementation here
    // at startup. Pass nullptr to revert to the null backend (e.g.
    // when shutting an audio device down before destroying the host).
    void SetSoundEmitter(std::unique_ptr<ISoundEmitter> emitter);

    // Master gain for SND EventObjects (range [0, 1]). Forwarded to the
    // active ISoundEmitter via its SetVolume; the value is cached on
    // RenderService so SetSoundEmitter can re-apply it to a fresh
    // backend (the standalone swaps in WindowsSoundEmitter after the
    // service is constructed, well after LoadSettingsIni runs).
    void  SetSoundVolume(float v);
    float GetSoundVolume() const { return soundVolume_; }

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

    // Host-thread eval+apply for externally-driven actors. Live sources
    // (Max plugin's MaxSceneAdapter, etc.) cannot be evaluated from the
    // render thread because they touch host-thread-only state — the host
    // must drive Evaluate from its own thread on every relevant event
    // (timeline change, modifier edit, idle hot-reload). For
    // template/MDX-backed actors there's no need to call this: the
    // render-thread Tick auto-evaluates them via EvaluateTopLevelActors.
    void EvaluateAndApply(Actor& actor);

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
    // Mesh geoset draw split. The default `All` runs both opaque and
    // transparent in one sorted sweep (legacy behaviour); `Opaque` and
    // `Transparent` filter the sorted ref list so the caller can slot
    // splats / particles / ribbons between the two buckets — matching
    // the WC3 engine's separate "Opaque Models" and "Transparent
    // Models" labelled passes.
    void RenderGeosets(GeosetBucket bucket);

    // PE2 service — centralised registry for the new particle path. Coexists
    // with the legacy per-Actor ParticleSystem until Phase 6 cut-over.
    particle::ParticleService particleService_;

    // Day/Night-Cycle service — owns the DNC MDL cache + active TOD
    // value. Lazily constructed once a content provider is available
    // (see EnsureDncService); render passes pull a sampled
    // BaselineLights from it through GetDncService().
    std::unique_ptr<dnc::DncService>  dncService_;

    // Cascaded-shadow-map service — owns the per-cascade depth
    // targets + computed cascade VPs. Constructed in InitBlsShaders
    // when the gfx device is available; the per-frame Update + the
    // shadow render pass live in RenderFrame.
    std::unique_ptr<shadow::ShadowService> shadowService_;

    // EventObject infrastructure. Splats live alongside particles (same
    // VB / shader path); the SPN spawner manages sub-MDX lifecycles in
    // sync with the PE1 path; the sound service is a logging stub by
    // default and can be replaced with an XAudio2 backend by swapping
    // the unique_ptr.
    particle::SplatService    splatService_;
    std::unique_ptr<SpnSpawner>     spnSpawner_;
    std::unique_ptr<ISoundEmitter>  soundEmitter_;
    // Cached master gain so SetSoundEmitter can re-apply across backend
    // swaps. The active emitter holds its own copy too (atomic for the
    // render-thread Play); this field is the source of truth the UI
    // queries via GetSoundVolume() and the INI persists. Default of
    // 0.2 keeps WC3 SND samples (often authored hot for mixing in the
    // game's own bus) from blowing out a model preview's listening
    // level — users can crank it up via the Settings slider.
    float                           soundVolume_ = 0.2f;

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
    bool                  showEvents_     = true;   // MDX EventObjects (SPN/SPL/UBR/FPT/SND)
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
    // LOD override: -1 = auto (screen-size), 0..3 = force that LOD.
    // Default forces LOD 0 — preview tooling almost always wants the
    // highest-detail mesh regardless of viewport size.
    std::atomic<int>      lodOverride_{0};
    // Lighting mode: 0=InGame, 1=Glue, 2=Dynamic. Stored as uint8 to
    // keep the atomic lock-free across all ABIs while the public API
    // exposes the LightingMode enum.
    std::atomic<uint8_t>  lightingMode_{static_cast<uint8_t>(LightingMode::InGame)};
    // sRGB clear colour packed COLORREF-style (0x00BBGGRR). Render
    // thread reads it once per frame and converts to linear. The prior
    // default of linear (0.06, 0.07, 0.10) was tuned alongside the old
    // 0.6 default exposure; raising exposure to 1.0 (engine-faithful)
    // would brighten the background ~67%, so we pre-attenuate the
    // linear values by 0.6 to keep the post-tonemap appearance the
    // same. Linear (0.036, 0.042, 0.06) → sRGB (53, 58, 69) → COLORREF
    // 0x00453A35.
    std::atomic<uint32_t> backgroundColor_{0x00453A35u};

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
    // Slang sources: line.slang (debug overlay), viewcube.slang
    // (DebugRenderer-owned). MDX-material rendering is entirely BLS;
    // the tonemap pass pairs the engine's Sprite VS (Shaders/vs/sprite.bls)
    // with Tonemap PS (Shaders/ps/tonemap.bls), exactly like Blizzard's
    // m_materialShaders[14] in CGxDevice::ILoadShaders @0x7ff609b40820.
    gfx::ShaderHandle lineVS_     = gfx::ShaderHandle::Invalid;
    gfx::ShaderHandle linePS_     = gfx::ShaderHandle::Invalid;

    // ---- GFX Pipelines ----
    // Two line PSO variants — D3D12 PSOs are baked against a specific
    // RTV format, so we keep one per scene-target format and pick the
    // matching one each frame via CurrentLinePSO(). Both are built at
    // device init; switching render modes never has to rebuild them.
    gfx::PipelineHandle linePSOHdr_  = gfx::PipelineHandle::Invalid;
    gfx::PipelineHandle linePSOSd_   = gfx::PipelineHandle::Invalid;
    gfx::PipelineHandle tonemapPSO_  = gfx::PipelineHandle::Invalid;

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

    // Splats need their own VB. They render BEFORE particles in the
    // frame's draw order; sharing particleServiceVB_ caused the
    // particle pass to overwrite splat verts mid-frame. The GPU
    // reads the buffer's contents at submit time (not record time),
    // so the shared layout produced flickering wrong-texture renders
    // — splats ended up drawing particle geometry against their own
    // bound splat textures.
    gfx::BufferHandle splatServiceVB_     = gfx::BufferHandle::Invalid;
    int               splatServiceVBSize_ = 0;

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
    // VS b1: ShadowCascades — three world→light-clip matrices. Used
    // by hd_vs.slang / sd_on_hd_vs.slang when HAS_SHADOWS=1.
    gfx::BufferHandle                       blsHdShadowCb_    = gfx::BufferHandle::Invalid;
    // PS b1: ShadowCascadeCount — single float numCascades. Read by
    // sdSampleShadowCascades when HAS_EXTRA_VERTS=1; without this
    // bound the shader reads 0 and the cascade selector returns 1.0
    // at every pixel (fully lit), making shadows totally invisible.
    gfx::BufferHandle                       blsHdShadowCountCb_ = gfx::BufferHandle::Invalid;

    // Depth-only PSO for the shadow render pass. Built once at
    // InitBlsShaders time using HD VS perm 4 (FourBoneSkinning,
    // no tangent / colour / uv, prepass=0, shadows=0 — i.e. the
    // standard skinned VS the renderer's own HD draw path uses
    // every frame, so the bytecode is known-good and matches our
    // MeshHDSkinnedNoTangent input layout). Null PS + rtvFormat =
    // Format::Unknown gives a depth-only attachment writing to
    // ShadowService's cascade depth maps. Slope-scaled bias mirrors
    // the engine's avoid-acne settings (see ShadowParams).
    gfx::PipelineHandle                     shadowPSO_        = gfx::PipelineHandle::Invalid;
    // Sibling PSO for non-skinned geosets (buildings, doodads).
    // HD VS perm 0 (Rigid, no tangent / colour / uv) + kParticleSD
    // input layout — same combination the renderer's own HD draw
    // path uses for static no-tangent meshes, so it's known-good
    // bytecode + layout. Without this, building / doodad geosets
    // (which have no boneVb) never write to the cascade depth map
    // and never cast shadows.
    gfx::PipelineHandle                     shadowPSORigid_   = gfx::PipelineHandle::Invalid;
    // Per-draw VS CB for the shadow pass. Only the first 192 B
    // (world / worldView / worldViewProj) are populated — the
    // depth-only render only needs SV_POSITION; the rest of the
    // VSOutput interpolants compile out to writes the null PS
    // never reads.
    gfx::BufferHandle                       shadowVsCb_       = gfx::BufferHandle::Invalid;
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
    // Day/Night-Cycle IBL pair. When both are populated AND the
    // LightingMode is InGame, the HD pass binds these (in the right
    // order) at t13/t14 instead of the single-probe path. The user
    // hits this by leaving SetEnvProbe alone (or after this opt-in
    // call); SetEnvProbe's single-probe override clears these and
    // reverts to the legacy from/to behaviour.
    static constexpr const char* kIblDayProbeName    = "ibl.dayProbe";
    static constexpr const char* kIblNightProbeName  = "ibl.nightProbe";
    float                                   iblProbeMipEnd_   = 0.0f;
    float                                   iblDayMipEnd_     = 0.0f;
    float                                   iblNightMipEnd_   = 0.0f;
    bool                                    iblDayNightLoaded_ = false;
    IblMode                                 iblMode_           = IblMode::Portrait;

    // Dynamic CBs, one-each for the full SD/SD_on_HD ABI. Sized for
    // numLights=8 worst case (720 B PS CB). Uploaded per draw.
    gfx::BufferHandle                       blsSdVsCb_ = gfx::BufferHandle::Invalid;
    gfx::BufferHandle                       blsSdPsCb_ = gfx::BufferHandle::Invalid;  // 48 B SDClassicPSPerDraw

    // ---- HDR / tonemap pass ----
    // All 3D draws (HD/SD mesh, particles, ribbons, debug grid + viewcube)
    // render into the active target's `hdrColor` (R11G11B10F). After 3D
    // work we run a fullscreen tonemap pass that samples the HDR target
    // and writes the LDR back-buffer using Blizzard's ps/tonemap.bls
    // (Narkowicz ACES filmic). Sourced from war3.w3mod just like
    // hd.bls / sd.bls.
    //
    // Both stages come from the BLS cache: Sprite VS (passthrough — the
    // VB carries clip-space positions directly) + Tonemap PS (single
    // permute, ACES filmic).
    //
    // Format choice — this matters for ACES correctness:
    //   The engine defaults to GxTex_R11G11B10F (=12, see Preview RE
    //   GBuffer::SetRenderTargetFormat @0x7ff609b00a80; runtime value of
    //   s_renderTargetFormat is 0x0C). It's an *unsigned* packed float
    //   so any sub-zero pixel the HD PS happens to emit gets clamped to
    //   0 by hardware on write. We previously used RGBA16F (signed) and
    //   negative HDR values survived into ACES, where
    //   `saturate((x*(A*x+B))/(x*(C*x+D)+E))` produces unrelated bright
    //   LDR pixels for negative x — visible as misplaced highlights /
    //   bloom-like ghosting. Matching the engine's format restores the
    //   "negative-clamped to zero" invariant the tonemap was designed
    //   against.
    static constexpr gfx::Format kHdrSceneFormat = gfx::Format::R11G11B10_FLOAT;

    // SD pipeline renders straight to the LDR back-buffer (no HDR
    // intermediate, no tonemap). Engine parity: classic SD textures
    // (albedo / _Diffuse usage) are sRGB-promoted by ApplySrgbPolicy
    // and therefore arrive in the shader as LINEAR values — the SD PS
    // does its multiplications in linear space and writes a linear
    // result. The RTV view must therefore be sRGB-encoding so the
    // hardware encodes the linear store back to a display-ready sRGB
    // byte. Mirrors preview.exe's classic-mode rendering, which the
    // user verified does an sRGB conversion before display.
    static constexpr gfx::Format kSdSceneFormat  = gfx::Format::R8G8B8A8_UNORM_SRGB;
    bls::BlsShader*         blsSpriteVs_     = nullptr; // shared fullscreen VS (Sprite.bls)
    bls::BlsShader*         blsTonemapPs_    = nullptr;
    gfx::BufferHandle       tonemapVB_       = gfx::BufferHandle::Invalid; // 3 verts: clip-space pos + uv
    gfx::BufferHandle       tonemapPsCb_     = gfx::BufferHandle::Invalid; // b1: { float exposure; pad×3 }
    gfx::SamplerHandle      tonemapSampler_  = gfx::SamplerHandle::Invalid; // s0: linear-clamp
    // Default 1.0 — matches the engine's s_tonemapParams initial value
    // (Preview RE: bytes 00 00 80 3F = 1.0f at 0x7ff60acb54a8). The
    // Settings window's slider exposes a runtime knob via
    // SetTonemapExposure() if a model needs the brights pulled into
    // ACES's rolloff range.
    float                   tonemapExposure_ = 1.0f;
    // Global "force NonLooping → loop" toggle (Settings window).
    // Stamped onto every fresh top-level actor + fanned out to live
    // actors through SetIgnoreNonLooping. PE1 / attachment children
    // ignore this — they keep their own default-false flag.
    // Default true: this is a model-preview tool, not the in-game
    // engine; users almost always want Death / climax poses to loop
    // so they can see the clip without re-selecting the sequence.
    // The Actor-side per-instance flag still defaults to false, so
    // children spawned by an emitter remain engine-faithful.
    bool                    ignoreNonLooping_ = true;
    void RunTonemapPass(const RenderTarget& target);

    bool InitBlsShaders();
    void ShutdownBlsShaders();
    bool RenderParticlesBls();  // BLS path; returns false if program unavailable
    bool RenderSplatsBls();     // EventObject SPL/UBR/FPT decals (BLS path)
    bool RenderGeosetsBls(GeosetBucket bucket);    // SD-mode mesh geosets (Path A: SD_HighSpec + SD)
    bool RenderGeosetsHd(GeosetBucket bucket);     // HD-mode mesh geosets (Path B: HD / SD_on_HD programs)
};

} // namespace WhiteoutDex
