// ============================================================================
// WhiteoutDex — All-in-One Max SDK Plugin (.dlx)
//
// Adapter pattern (live source): MaxSceneAdapter implements both
// IModelDataSource (Build()) and IAnimationSource (Evaluate()), so a single
// SpawnActorFromLiveSource call covers both the static snapshot AND the
// per-frame animation feed. The adapter outlives the renderer because the
// actor's AnimationDriver holds a shared_ptr to it.
//
// MaxScript API:
//   WhiteoutFlakesStart()             → Extract scene + open renderer + start sync
//   WhiteoutFlakesStop()              → Stop everything + close window
//   WhiteoutFlakesRefreshMaterials()  → Re-read material properties (hot reload)
// ============================================================================

#include "cubeb_sound_emitter.h"
#include "max_scene_adapter.h"
#include "render_window.h"
#include "renderer/assets/asset_manager.h"
#include "renderer/assets/replaceable_texture_manager.h"
#include "renderer/model/model_instance.h" // Actor
#include "renderer/model/model_loader.h"
#include "renderer/render_service.h"
#include "renderer/scene_manager.h"
#include "settings_ini.h"
#include "whiteout/flakes/gfx_types.h"   // GfxApi
#include "whiteout/flakes/types.h"

#include <chrono>
#include <filesystem>
#include <memory>

// clang-format off
#include <max.h>
#include <notify.h>
#include <maxversion.h>
#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>
#include <maxscript/foundation/arrays.h>
#include <maxscript/foundation/numbers.h>
#include <maxscript/foundation/strings.h>
// MUST be the LAST maxscript header in the file. Other maxscript headers
// transitively pull in define_implementations.h which redefines
// def_visible_primitive to a no-op, so this include must land last to
// restore the GLOBAL-instantiation form of the macro. Otherwise our
// `Primitive WhiteoutFlakesStart_pf(...)` line silently expands to
// nothing and MaxScript never sees the primitive.
#include <maxscript/macros/define_instantiation_functions.h>
// clang-format on

using namespace whiteout::flakes;

// ============================================================================
// Global state
// ============================================================================
static std::shared_ptr<whiteout::flakes::MaxSceneAdapter> g_adapter;
static whiteout::flakes::renderer::model::Actor* g_actor = nullptr; // borrowed; owned by g_scene
static whiteout::flakes::renderer::SceneManager* g_scene = nullptr;
static whiteout::flakes::renderer::RenderService* g_renderer = nullptr;
static whiteout::flakes::RenderWindow* g_renderWindow = nullptr;
static bool g_running = false;
static HINSTANCE g_hInstance = nullptr;
static DWORD g_lastTimeChangedTick = 0;

// Convert Max time (TimeValue ticks) to milliseconds. Returns 0 if Max
// reports a missing tick rate (rare; tested WhiteoutFlakesStart paths).
static i32 MaxTimeToMs(TimeValue t) {
    i32 tpf = GetTicksPerFrame(), fps = GetFrameRate();
    return (tpf > 0 && fps > 0) ? (i32)((f32)t / (f32)tpf * 1000.0f / (f32)fps) : 0;
}

// Push Max's externally-driven time onto the actor + scene clock, then run
// eval+apply on Max's thread. MaxSceneAdapter::Evaluate reads live Max
// scene state (node TMs, modifier params, vertex paint, materials) which is
// only thread-safe to touch from Max's UI thread — the render-thread Tick
// can't do this for us.
static void EvalFromMax(i32 timeMs) {
    if (!g_actor || !g_renderer || !g_scene)
        return;
    g_actor->animation.SetTimeMs(timeMs);
    g_scene->SetAnimationTime(timeMs);
    g_actor->EvaluateAndApply(g_renderer->MakeActorEvalContext());
}

// ============================================================================
// TimeChange callback — Max scrubs the timeline; we re-evaluate the actor.
// ============================================================================
class WhiteoutFlakesTimeCallback : public TimeChangeCallback {
public:
    void TimeChanged(TimeValue t) override {
        if (!g_running || !g_renderer || !g_actor)
            return;
        if (!g_renderWindow || !g_renderWindow->IsOpen())
            return;
        g_lastTimeChangedTick = GetTickCount();
        EvalFromMax(MaxTimeToMs(t));
    }
};

static WhiteoutFlakesTimeCallback* g_timeCallback = nullptr;

// ============================================================================
// Material polling timer — detects property changes even without timeline scrub
// Also serves as the main-thread tripwire for "render window was closed via
// the X button": the render thread exited but everything else is still alive.
// ============================================================================
static UINT_PTR g_materialTimerId = 0;
static void WhiteoutFlakesCleanup(); // forward declaration

static void CALLBACK MaterialPollTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_running)
        return;
    // User closed the renderer window (X button → WM_DESTROY on the render
    // thread). The thread has exited, but the renderer/adapter/actor/scene
    // are still allocated on the Max side. Tear them down here, on Max's
    // main thread, so the next Start launches fresh and File > Reset
    // doesn't get to walk dangling pointers.
    if (g_renderWindow && !g_renderWindow->IsOpen()) {
        WhiteoutFlakesCleanup();
        return;
    }
    if (!g_renderer || !g_adapter || !g_actor || !g_renderWindow)
        return;

    // Hot-reload check.
    auto result = g_adapter->RefreshMaterials();
    if (result.changed) {
        g_renderer->Loader().UpdateMaterials(g_actor->handle, result.materials, result.textures);
    }

    // Re-evaluate when the timeline is idle — picks up non-animated
    // changes (vertex paint, modifier toggles, visibility flips). Skip
    // when TimeChanged is actively firing so playback isn't fighting a
    // duplicate eval on the same frame.
    DWORD now = GetTickCount();
    if (now - g_lastTimeChangedTick > 1000) {
        Interface* ip = GetCOREInterface();
        if (ip)
            EvalFromMax(MaxTimeToMs(ip->GetTime()));
    }
}

// ============================================================================
// Max scene-event notifications — tear the renderer down before Max discards
// the underlying scene out from under us.
// ============================================================================
static bool g_notificationsRegistered = false;

static void OnMaxSceneEvent(void* /*param*/, NotifyInfo* /*info*/) {
    // System Reset, File > New (also routes through Reset), File > Open,
    // File > Merge, and shutdown all invalidate the INode pointers we hold.
    // Stop the renderer cleanly so the next refresh isn't reading freed
    // scene data — that's the typical Max crash signature.
    if (g_running)
        WhiteoutFlakesCleanup();
}

static void EnsureSceneNotificationsRegistered() {
    if (g_notificationsRegistered)
        return;
    g_notificationsRegistered = true;
    // Idempotent — Max ignores duplicate registrations for the same
    // (callback, param, code) triple. Registering lazily on first Start
    // keeps the plug-in inert until the user actually wants the preview.
    RegisterNotification(OnMaxSceneEvent, nullptr, NOTIFY_SYSTEM_PRE_RESET);
    RegisterNotification(OnMaxSceneEvent, nullptr, NOTIFY_FILE_PRE_OPEN);
    RegisterNotification(OnMaxSceneEvent, nullptr, NOTIFY_FILE_PRE_MERGE);
    RegisterNotification(OnMaxSceneEvent, nullptr, NOTIFY_SYSTEM_SHUTDOWN);
}

// ============================================================================
// Helpers
// ============================================================================
static void WhiteoutFlakesCleanup() {
    if (g_materialTimerId) {
        KillTimer(nullptr, g_materialTimerId);
        g_materialTimerId = 0;
    }
    if (g_timeCallback) {
        Interface* ip = GetCOREInterface();
        if (ip)
            ip->UnRegisterTimeChangeCallback(g_timeCallback);
        delete g_timeCallback;
        g_timeCallback = nullptr;
    }
    g_running = false;
    if (g_renderer) {
        g_renderer->Loader().RequestClearAll();
    }
    if (g_renderWindow) {
        g_renderWindow->Close();
        delete g_renderWindow;
        g_renderWindow = nullptr;
    }
    // Order: actors die with the renderer's scene-clear/release; release the
    // renderer next so its non-owning scene pointer goes inert; drop the
    // adapter shared_ptr (the actor's AnimationDriver was the last other
    // holder, so this destroys the adapter); finally tear down scene.
    g_actor = nullptr;
    if (g_renderer) {
        delete g_renderer;
        g_renderer = nullptr;
    }
    g_adapter.reset();
    if (g_scene) {
        delete g_scene;
        g_scene = nullptr;
    }
    mprintf(_M("WhiteoutDex: === STOPPED ===\n"));
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInstance = hInst;
        DisableThreadLibraryCalls(hInst);
    } else if (reason == DLL_PROCESS_DETACH) {
        WhiteoutFlakesCleanup();
    }
    return TRUE;
}

// ============================================================================
// Max Plugin Descriptor
//
// Matches the pre-imgui working pattern exactly: Create returns nullptr.
// The MaxScript primitives below are registered at static-init time via
// def_visible_primitive; they don't need a GUP instance to be live.
// ============================================================================
class WhiteoutFlakesPluginClassDesc : public ClassDesc2 {
public:
    int IsPublic() override {
        return FALSE;
    }
    void* Create(BOOL) override {
        return nullptr;
    }
    const MCHAR* ClassName() override {
        return _M("WhiteoutFlakesRenderer");
    }
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    const MCHAR* NonLocalizedClassName() override {
        return _M("WhiteoutFlakesRenderer");
    }
#endif
    SClass_ID SuperClassID() override {
        return GUP_CLASS_ID;
    }
    // Class_ID matches the pre-imgui WhiteoutDex-native-rendering plug-in
    // (NDEX EXTR in ASCII). That ID is known to load cleanly on Max 2016
    // -> 2027; switching to a freshly generated ID coincided with Max 2016
    // silently rejecting the .dlx, so restore the known-good ID to remove
    // that variable from the load-failure investigation.
    Class_ID ClassID() override {
        return Class_ID(0x4e444558, 0x45585452);
    }
    const MCHAR* Category() override {
        return _M("WhiteoutFlakes");
    }
    const MCHAR* InternalName() override {
        return _M("WhiteoutFlakesRenderer");
    }
    HINSTANCE HInstance() override {
        return g_hInstance;
    }
};

static WhiteoutFlakesPluginClassDesc g_classDesc;

// Exports match the pre-imgui working pattern exactly: just the 4 required
// Max SDK callbacks. No LibInitialize / LibShutdown / CanAutoDefer — the
// working old plug-in didn't export those either and Max 2016 loaded it
// fine. Adding them seems to actually have caused Max to silently reject
// the plug-in after LibVersion in the new build.
extern "C" {
__declspec(dllexport) const MCHAR* LibDescription() {
    return _M("WhiteoutFlakes Renderer");
}
__declspec(dllexport) int LibNumberClasses() {
    return 1;
}
__declspec(dllexport) ClassDesc* LibClassDesc(int i) {
    return (i == 0) ? &g_classDesc : nullptr;
}
__declspec(dllexport) ULONG LibVersion() {
    return VERSION_3DSMAX;
}
}

// ============================================================================
// WhiteoutFlakesStart() — collect scene via adapter, load into renderer
// Returns extraction time in ms, or -1 on error
// ============================================================================

def_visible_primitive(WhiteoutFlakesStart, "WhiteoutFlakesStart");
Value* WhiteoutFlakesStart_cf(Value** arg_list, i32 count) {
    check_arg_count(WhiteoutFlakesStart, 0, count);
    auto start = std::chrono::high_resolution_clock::now();
    (void)arg_list;

    // Defense in depth: handle the "window was closed but cleanup hadn't
    // run yet" case too (the poll timer normally catches this within 500 ms).
    if (g_running || (g_renderWindow && !g_renderWindow->IsOpen()))
        WhiteoutFlakesCleanup();

    EnsureSceneNotificationsRegistered();

    // Host owns SceneManager + RenderService + RenderWindow.
    g_scene = new whiteout::flakes::renderer::SceneManager();
    g_renderer = new whiteout::flakes::renderer::RenderService(*g_scene);

    // Force D3D11 as the default backend when running inside Max. The engine
    // defaults to D3D12 (better for the standalone viewer) but Max's host
    // process inherits whatever DXGI state Max set up at boot, which has
    // tripped first-frame D3D12 device creation on older Max releases.
    // D3D11 is universally available and matches the backend the renderer
    // was originally shipped with in this repo.
    g_renderer->Settings().SetDefaultBackend(whiteout::flakes::gfx::GfxApi::D3D11);

    // Point the renderer's disk file resolver at the directory containing
    // this .dlx — that's where the installer drops `shaders/` (BLS bundles
    // RenderPipeline::InitDevice loads via the content provider).
    //
    // `SetBasePath` is for the disk-side FileResolver and is the ONLY thing
    // the BLS shader lookup cares about. Earlier this code also called
    // `SetInstallPath(dlxDir)` — that was wrong, InstallPath drives the
    // CASC/MPQ storage roots, and pinning it at the .dlx directory meant
    // the renderer treated the plug-in folder as the WC3 install no matter
    // what the user typed into the Settings dialog. The W3Path read below
    // restores the correct user-configured value.
    {
        wchar_t buf[MAX_PATH] = {};
        if (GetModuleFileNameW(g_hInstance, buf, MAX_PATH) > 0) {
            std::filesystem::path dlxDir = std::filesystem::path(buf).parent_path();
            g_scene->GetContentProvider().SetBasePath(dlxDir);
        }
    }

    // Pull the WC3 install root the user configured in the Settings dialog
    // (TextureBrowserHelper.ms / Toolkit-Settings.mcr both write to the same
    // INI). Without this the renderer's CASC/MPQ lookups go nowhere —
    // particle textures, DNC probe DDSes, ambient sound assets all fail to
    // load and the preview ends up running on the procedural fallback.
    //
    //   <plugcfg>\WhiteoutDex_Settings.ini → [CASC] W3Path
    //
    // The importer (mdlx_import_options.cpp) and TextureBrowserHelper read
    // from the same file; this keeps the three plug-ins consistent so the
    // user only has to type the install path once. The captured value is
    // re-applied to the MaxSceneAdapter's own provider further below — the
    // adapter has its own FileContentProvider instance used during scene
    // collection, and it needs the same root.
    std::string userInstallPath;
    {
        Interface* ipForPath = GetCOREInterface();
        if (ipForPath) {
            MSTR pcDir = ipForPath->GetDir(APP_PLUGCFG_DIR);
            std::wstring cascIni = std::wstring(pcDir.data()) + L"\\WhiteoutDex_Settings.ini";
            if (GetFileAttributesW(cascIni.c_str()) != INVALID_FILE_ATTRIBUTES) {
                wchar_t w3buf[MAX_PATH] = {};
                GetPrivateProfileStringW(L"CASC", L"W3Path", L"", w3buf, MAX_PATH, cascIni.c_str());
                if (w3buf[0]) {
                    // SetInstallPath takes UTF-8 narrow; MDX install paths
                    // are ASCII-safe in practice (Windows refuses to install
                    // WC3 under non-ASCII paths via Battle.net), so the
                    // naive wide-to-narrow truncation here matches what the
                    // importer does for the same setting.
                    userInstallPath.reserve(MAX_PATH);
                    for (wchar_t c : std::wstring_view(w3buf))
                        userInstallPath += static_cast<char>(c);
                    g_scene->GetContentProvider().SetInstallPath(userInstallPath);
                    mprintf(_M("WhiteoutDex: CASC W3Path = '%s'\n"), w3buf);
                }
            }
        }
    }

    g_renderWindow = new whiteout::flakes::RenderWindow(*g_renderer);
    // Open() defaults to D3D12 — Settings().SetDefaultBackend() above only
    // affects Settings::DefaultBackend() and isn't plumbed through to
    // RenderWindow::ThreadFunc, which passes its `api` arg verbatim into
    // RenderPipeline::InitDevice. D3D12 first-frame init has been observed
    // to fail inside the Max 2016 host process, so we pin D3D11 explicitly
    // here. D3D11 is the renderer's previous default and matches what the
    // pre-imgui WhiteoutDex binary shipped with.
    if (!g_renderWindow->Open(800, 600, whiteout::flakes::gfx::GfxApi::D3D11)) {
        mprintf(_M("WhiteoutDex: ERROR - Could not open renderer window\n"));
        delete g_renderWindow;
        g_renderWindow = nullptr;
        delete g_renderer;
        g_renderer = nullptr;
        delete g_scene;
        g_scene = nullptr;
        return Integer::intern(-1);
    }

    // Make renderer window float above Max. GA_ROOT walks up to a top-level
    // window so GWLP_HWNDPARENT doesn't accidentally re-parent the window
    // under a child viewport panel (which would clip the title bar to "W").
    Interface* ip = GetCOREInterface();
    HWND ndxWnd = FindWindowW(L"WhiteoutDexRendererClass", nullptr);
    if (ndxWnd && ip) {
        HWND maxHwnd = ip->GetMAXHWnd();
        HWND maxRoot = GetAncestor(maxHwnd, GA_ROOT);
        SetWindowLongPtrW(ndxWnd, GWLP_HWNDPARENT, (LONG_PTR)(maxRoot ? maxRoot : maxHwnd));
    }

    // PE1 base path = directory containing the loaded .max file. Set BEFORE
    // adapter collection so PE1 child-model paths in the scene resolve.
    if (const MCHAR* maxFile = ip->GetCurFilePath().data(); maxFile && maxFile[0]) {
        std::wstring wp(maxFile);
        auto pos = wp.find_last_of(L'\\');
        if (pos != std::wstring::npos)
            wp = wp.substr(0, pos + 1);
        g_scene->SetPE1BasePath(std::filesystem::path(wp));
    }

    // Apply persisted IO overrides (Ignore Casc/Mpq toggles + MPQ load list)
    // from the WhiteoutFlakes-side settings file sitting next to 3dsmax.exe.
    // We deliberately ignore its `installPath` field here — the WhiteoutDex
    // Settings dialog writes the install path to `WhiteoutDex_Settings.ini`
    // under plugcfg (read above), and that's the authoritative source for
    // all three plug-ins. Letting LoadIoPathOverrides override at this point
    // is what made the renderer treat the .dlx directory as the WC3 install
    // for users who never created WhiteoutFlakes.ini (i.e. everyone).
    auto applyIoOverrides = [](whiteout::flakes::io::FileContentProvider& provider) {
        auto overrides = whiteout::flakes::LoadIoPathOverrides();
        provider.SetIgnoreCasc(overrides.ignoreCasc);
        provider.SetIgnoreMpq(overrides.ignoreMpq);
        if (overrides.mpqListSet)
            provider.SetMpqList(std::move(overrides.mpqList));
    };
    applyIoOverrides(g_scene->GetContentProvider());

    // Audio: the same cubeb-backed ISoundEmitter the standalone exe uses.
    // Borrows the scene's content provider for CASC/MPQ lookup so SND
    // EventObjects play during preview. Without this, the renderer's
    // default null emitter drops every fire.
    g_renderer->SwapSoundEmitter(
        std::make_unique<whiteout::flakes::CubebSoundEmitter>(g_scene->ActiveContentProvider()));

    // ---- Build the live adapter ----
    g_adapter = std::make_shared<whiteout::flakes::MaxSceneAdapter>();

    // Same install path + IO overrides apply to the adapter's own provider —
    // it's used during CollectScene below for CASC/MPQ texture reads. The
    // adapter has its own FileContentProvider instance (independent of
    // SceneManager's), so it needs the W3Path set on it too.
    if (!userInstallPath.empty())
        g_adapter->GetContentProvider().SetInstallPath(userInstallPath);
    applyIoOverrides(g_adapter->GetContentProvider());
    // Cross-model dedup: skip BLP/CASC decode for textures that other models
    // already uploaded. SpawnActorFromLiveSource sets this too, but we set
    // it now so CollectScene's incidental texture reads also benefit.
    // The cache now lives on AssetManager (post-asset-manager refactor)
    // and Assets().IsTextureCached() returns true once Apply has run.
    g_adapter->SetTextureCacheQuery(
        [](std::string_view k) { return g_renderer->Assets().IsTextureCached(k); });

    // Walk the Max scene at t=0 for stable bind-pose extraction.
    TimeValue savedTime = ip->GetTime();
    ip->SetTime(0, FALSE);
    mprintf(_M("WhiteoutDex: Collecting scene...\n"));
    g_adapter->CollectScene();

    // ---- One-call spawn ----
    // Pulls all static data via IModelDataSource::Build, builds an inline
    // ModelTemplate, registers attachment + PE1 configs, and binds the
    // actor's AnimationDriver to the adapter (which is also an
    // IAnimationSource). Replaces ~15 lines of GetX + LoadModel + SetX boilerplate.
    mprintf(_M("WhiteoutDex: Loading model...\n"));
    g_actor = g_renderer->Loader().SpawnUnitFromSource(g_adapter);
    if (!g_actor) {
        mprintf(_M("WhiteoutDex: ERROR - SpawnUnitFromSource failed\n"));
        WhiteoutFlakesCleanup();
        return Integer::intern(-1);
    }
    // Max scrubs Max's timeline; the renderer's per-frame ticker must skip
    // its own evaluation pass and let EvalFromMax push the cursor instead.
    g_actor->role = whiteout::flakes::renderer::model::ActorRole::External;

    // Pick HD vs SD by walking the adapter's freshly extracted materials.
    // `Actor::PreferredRenderMode()` would be the natural choice, but the
    // ModelLoader::AddModel path (used by SpawnUnitFromSource) never
    // populates `Actor::sourceTemplate`, so that accessor short-circuits to
    // SD regardless of the layers' actual shaderIds. Reading the IR
    // materials from the adapter directly mirrors what the renderer's
    // PreferredRenderMode would do if sourceTemplate were wired:
    // any layer with shaderId != 0 (HD = 1, SD_on_HD = 2, Crystal = 24)
    // promotes the whole model to HD.
    {
        using whiteout::flakes::RenderMode;
        RenderMode mode = RenderMode::SD;
        for (const auto& md : g_adapter->GetMaterials()) {
            for (const auto& ld : md.layers) {
                if (ld.shaderId != 0) {
                    mode = RenderMode::HD;
                    break;
                }
            }
            if (mode == RenderMode::HD)
                break;
        }
        g_renderer->Settings().SetRenderMode(mode);
        mprintf(_M("WhiteoutDex: render mode = %s\n"),
                mode == RenderMode::HD ? _M("HD") : _M("SD"));
    }
    g_renderWindow->SetFocusActor(g_actor->handle);

    // Push the actor's discovered sequences into the preview window so the
    // ImGui Animation dropdown can pick which sub-range Max's timeline
    // scrubs through.
    {
        auto seqs = g_actor->animation.Sequences();
        std::vector<std::string> names;
        names.reserve(seqs.size());
        for (auto& s : seqs)
            names.push_back(s.name);
        g_renderWindow->SetSequences(std::move(names), std::move(seqs));
    }

    // Camera presets are no longer plumbed into the renderer — Max owns its
    // own viewport so MaxSceneAdapter::GetCameraPresets() is currently unused
    // by the plugin. Re-add a Max-side preset UI if needed.

    // Restore Max's time and run the initial eval so the model is visible
    // before TimeChanged starts firing.
    ip->SetTime(savedTime, FALSE);
    EvalFromMax(MaxTimeToMs(ip->GetTime()));

    // Hook Max's timeline + start the polling timer for hot-reload.
    g_timeCallback = new WhiteoutFlakesTimeCallback();
    ip->RegisterTimeChangeCallback(g_timeCallback);
    g_running = true;
    g_materialTimerId = SetTimer(nullptr, 0, 500, MaterialPollTimer);

    auto end = std::chrono::high_resolution_clock::now();
    i32 ms = (i32)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Diagnostic readout from the actor's render-side counts.
    mprintf(_M("\nWhiteoutDex: === STARTED in %d ms ===\n"), ms);
    mprintf(_M("  %d geosets, %d materials\n"), (i32)g_actor->render.gpuGeosets.size(),
            (i32)g_actor->render.gpuMaterials.size());
    mprintf(_M("  %d collisions\n"), (i32)g_actor->render.collisionShapes.size());

    return Integer::intern(ms);
}

// ============================================================================
// WhiteoutFlakesStop() — stop sync, close renderer
// ============================================================================

def_visible_primitive(WhiteoutFlakesStop, "WhiteoutFlakesStop");
Value* WhiteoutFlakesStop_cf(Value** /*arg_list*/, i32 count) {
    check_arg_count(WhiteoutFlakesStop, 0, count);
    WhiteoutFlakesCleanup();
    return &ok;
}

// ============================================================================
// WhiteoutFlakesPushSequences(names, startTicks, endTicks)
// Hand the adapter the per-sequence ranges from WdxSequenceManager so its
// Evaluate can resolve "what sequence is the timeline on" — Max owns the
// timeline, the renderer doesn't, so without this the popcorn animVisibility
// guide can't fire and every emitter spawns regardless of sequence.
// ============================================================================

def_visible_primitive(WhiteoutFlakesPushSequences, "WhiteoutFlakesPushSequences");
Value* WhiteoutFlakesPushSequences_cf(Value** arg_list, i32 count) {
    check_arg_count(WhiteoutFlakesPushSequences, 3, count);
    if (!g_adapter) {
        mprintf(_M("WhiteoutDex: PushSequences: g_adapter is null — start the renderer first\n"));
        return &false_value;
    }

    Array* names  = (Array*)arg_list[0];
    Array* starts = (Array*)arg_list[1];
    Array* ends   = (Array*)arg_list[2];
    if (!names || !starts || !ends) {
        mprintf(_M("WhiteoutDex: PushSequences: null array arg\n"));
        return &false_value;
    }

    const i32 n = names->size;
    if (starts->size != n || ends->size != n) {
        mprintf(_M("WhiteoutDex: PushSequences: array length mismatch (names=%d, starts=%d, "
                   "ends=%d)\n"), n, starts->size, ends->size);
        return &false_value;
    }

    std::vector<whiteout::flakes::SequenceRange> ranges;
    ranges.reserve(static_cast<usize>(n));
    for (i32 i = 0; i < n; ++i) {
        whiteout::flakes::SequenceRange r;
        // MaxScript strings come back as wide; convert to UTF-8 narrow
        // since the guide-matcher works on std::string.
        try {
            const wchar_t* w = names->data[i]->to_string();
            if (w) {
                const int u8len =
                    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
                if (u8len > 1) {
                    r.name.assign(static_cast<usize>(u8len - 1), '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, r.name.data(), u8len, nullptr,
                                          nullptr);
                }
            }
        } catch (...) {
            // to_string can throw on non-string values; treat as empty.
        }
        const TimeValue sTv = (TimeValue)starts->data[i]->to_int();
        const TimeValue eTv = (TimeValue)ends->data[i]->to_int();
        r.startMs = MaxTimeToMs(sTv);
        r.endMs   = MaxTimeToMs(eTv);
        mprintf(_M("WhiteoutDex:   seq[%d] '%hs' = [%d..%d ms]  (ticks %d..%d)\n"), i,
                r.name.c_str(), r.startMs, r.endMs, (i32)sTv, (i32)eTv);
        ranges.push_back(std::move(r));
    }
    g_adapter->SetSequenceRanges(std::move(ranges));
    mprintf(_M("WhiteoutDex: pushed %d sequence ranges\n"), n);
    return Integer::intern(n);
}

// ============================================================================
// WhiteoutFlakesRefreshMaterials() — force re-read of material properties
// Returns true if anything changed
// ============================================================================

def_visible_primitive(WhiteoutFlakesRefreshMaterials, "WhiteoutFlakesRefreshMaterials");
Value* WhiteoutFlakesRefreshMaterials_cf(Value** /*arg_list*/, i32 count) {
    check_arg_count(WhiteoutFlakesRefreshMaterials, 0, count);
    if (!g_running || !g_renderer || !g_adapter || !g_actor)
        return &false_value;

    auto result = g_adapter->RefreshMaterials();
    if (result.changed) {
        g_renderer->Loader().UpdateMaterials(g_actor->handle, result.materials, result.textures);
        mprintf(_M("WhiteoutDex: Materials refreshed (%d materials, %d textures)\n"),
                (i32)result.materials.size(), (i32)result.textures.size());
        return &true_value;
    }
    return &false_value;
}
