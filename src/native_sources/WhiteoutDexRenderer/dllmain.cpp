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
//   WhiteoutFlakesResync()            → Re-extract the whole scene in place
//   WhiteoutFlakesRefreshMaterials()  → Re-read material properties (hot reload)
//   WhiteoutFlakesIsRunning()         → Is the preview window up?
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
#include "wdx_ui_language.h" // Flakes i18n catalogs, driven by WhiteoutDex's setting
#include "whiteout/flakes/gfx_types.h"   // GfxApi
#include "whiteout/flakes/types.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// clang-format off
#include <max.h>
// After max.h (which has already pulled in <windows.h>) and before the
// maxscript block, whose macros these headers must not be compiled under.
#include "wdx_mpq_settings.h"
#include "wdx_scene_art_tier_max.h"
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

// Sequence ranges last handed over by WhiteoutFlakesPushSequences. Cached
// because a Resync mints a fresh adapter, and the ranges live on the adapter —
// without this the popcorn animVisibilityGuide gate would silently go dark
// after every rescan.
static std::vector<whiteout::flakes::SequenceRange> g_sequenceRanges;

// The same sequences again, this time carrying Max's own unit - ticks - which
// is what Interface::Get/SetAnimRange speaks. Kept alongside the millisecond
// copy above rather than converted back on demand: MaxTimeToMs rounds, and the
// toolbar's "is the timeline sitting on a sequence?" test has to be exact.
struct TimelineSequence {
    std::string name;
    TimeValue startTick = 0;
    TimeValue endTick = 0;
    i32 startMs = 0;
    i32 endMs = 0;
};
static std::vector<TimelineSequence> g_timelineSequences;

// Last index handed to the preview window, so the 20 ms sync tick only writes
// on an actual change. -2 is "nothing published yet" and forces the next tick
// to write whatever it finds, including -1.
static i32 g_publishedSequenceIdx = -2;

// Re-entrancy guard for the full rebuild: it parks the render thread and moves
// Max's time cursor, and neither timer may re-enter it while that runs.
static bool g_rebuilding = false;

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
// Animation combo <-> Max timeline
//
// The preview's Animation dropdown does not play anything: Max drives the
// clock, so the dropdown reflects which sequence Max's *animation range* spans
// and, when the user picks a different one, asks Max to move that range. Both
// halves are Max-UI-thread work and live on the sync timer; the render thread
// only reads an atomic and raises a request.
// ============================================================================

// Hand the preview window the list its Animation combo draws. The names double
// as the ImGui labels; the millisecond ranges ride along because RenderWindow
// stores SequenceInfo.
static void PublishTimelineSequencesToWindow() {
    if (!g_renderWindow)
        return;
    std::vector<std::string> names;
    std::vector<whiteout::flakes::renderer::model::SequenceInfo> infos;
    names.reserve(g_timelineSequences.size());
    infos.reserve(g_timelineSequences.size());
    for (const auto& ts : g_timelineSequences) {
        names.push_back(ts.name);
        whiteout::flakes::renderer::model::SequenceInfo si;
        si.name = ts.name;
        si.startMs = ts.startMs;
        si.endMs = ts.endMs;
        infos.push_back(std::move(si));
    }
    g_renderWindow->SetSequences(std::move(names), std::move(infos));
}

// Which pushed sequence is Max's animation range sitting on? -1 when it is
// nobody's. The match is exact on both ends because that is what setting one
// does - the Sequence Manager (and ApplySequenceRequest below) plant the range
// on precisely the sequence's bounds, so any other range really is one the
// user dragged out by hand.
static i32 MatchSequenceToAnimRange(const Interval& range) {
    for (usize i = 0; i < g_timelineSequences.size(); ++i) {
        if (g_timelineSequences[i].startTick == range.Start() &&
            g_timelineSequences[i].endTick == range.End())
            return static_cast<i32>(i);
    }
    return -1;
}

// Max timeline -> combo. Cheap enough to run every tick: a handful of integer
// compares, and it only touches the atomic when the answer changes.
static void PublishActiveSequence(Interface* ip) {
    if (!ip || !g_renderWindow)
        return;
    const i32 idx = MatchSequenceToAnimRange(ip->GetAnimRange());
    if (idx == g_publishedSequenceIdx)
        return;
    g_publishedSequenceIdx = idx;
    g_renderWindow->SetActiveSequenceIdx(idx);
}

// Combo -> Max timeline. A negative `idx` is the "free range" entry, which
// opens the timeline back up to the span every sequence lives in: the one
// range that is deliberately not any single sequence and still shows the
// whole model.
static void ApplySequenceRequest(Interface* ip, i32 idx) {
    if (!ip || g_timelineSequences.empty())
        return;

    TimeValue start = g_timelineSequences[0].startTick;
    TimeValue end = g_timelineSequences[0].endTick;
    if (idx >= 0 && idx < static_cast<i32>(g_timelineSequences.size())) {
        start = g_timelineSequences[idx].startTick;
        end = g_timelineSequences[idx].endTick;
    } else {
        for (const auto& ts : g_timelineSequences) {
            start = std::min(start, ts.startTick);
            end = std::max(end, ts.endTick);
        }
    }
    // A zero-length or inverted sequence would leave Max with a timeline it
    // cannot scrub; the Sequence Manager refuses to play those too.
    if (start >= end)
        return;

    ip->SetAnimRange(Interval(start, end));
    // Land on the first frame of what was just picked, the way the Sequence
    // Manager's playSequence does: the old cursor is very likely outside the
    // new range, and Max would otherwise leave it clamped at an end.
    ip->SetTime(start);
    // Publish straight away instead of waiting for the next tick, so the combo
    // does not flash "free range" on its way to the sequence just chosen.
    PublishActiveSequence(ip);
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
    if (!g_running || g_rebuilding)
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
// Shared IO configuration — read once at Start, and again on every Resync so a
// settings change lands without restarting the preview.
// ============================================================================

// The WC3 install root the Settings dialog wrote to
// `<plugcfg>\WhiteoutDex_Settings.ini`. Also hands back the plugcfg directory,
// which is where the MPQ load order lives. Empty when nothing is configured.
static std::string ReadUserInstallPath(std::wstring& plugcfgDirOut) {
    plugcfgDirOut.clear();
    Interface* ip = GetCOREInterface();
    if (!ip)
        return {};

    MSTR pcDir = ip->GetDir(APP_PLUGCFG_DIR);
    plugcfgDirOut = std::wstring(pcDir.data());
    std::wstring cascIni = plugcfgDirOut + L"\\WhiteoutDex_Settings.ini";
    if (GetFileAttributesW(cascIni.c_str()) == INVALID_FILE_ATTRIBUTES)
        return {};

    wchar_t w3buf[MAX_PATH] = {};
    // Disambiguate Win32 GetPrivateProfileStringW from the MaxSDK::Util overload
    // the 2026 SDK added (Util/IniUtil.h) by taking a function pointer to the
    // exact Win32 signature — the same trick the exporter and importer already
    // use for this conflict.
    static auto Win32_GetPrivateProfileStringW =
        static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
            &::GetPrivateProfileStringW);
    Win32_GetPrivateProfileStringW(L"CASC", L"W3Path", L"", w3buf, MAX_PATH, cascIni.c_str());
    if (!w3buf[0])
        return {};

    // SetInstallPath takes UTF-8 narrow; MDX install paths are ASCII-safe in
    // practice (Windows refuses to install WC3 under a non-ASCII path via
    // Battle.net), so the naive wide-to-narrow truncation here matches what the
    // importer does for the same setting.
    std::string out;
    out.reserve(MAX_PATH);
    for (wchar_t c : std::wstring_view(w3buf))
        out += static_cast<char>(c);
    return out;
}

// Apply the persisted IO overrides (Ignore Casc/Mpq toggles + MPQ load list)
// from the WhiteoutFlakes-side settings file sitting next to 3dsmax.exe.
//
// Its `installPath` field is deliberately ignored: the WhiteoutDex Settings
// dialog writes the install path to `WhiteoutDex_Settings.ini` under plugcfg
// (see ReadUserInstallPath) and that is the authoritative source for all three
// plug-ins. Letting LoadIoPathOverrides win here is what made the renderer treat
// the .dlx directory as the WC3 install for users who never created
// WhiteoutFlakes.ini — i.e. everyone.
static void ApplyIoOverridesTo(whiteout::flakes::io::FileContentProvider& provider,
                               const std::wstring& plugcfgDir) {
    auto overrides = whiteout::flakes::LoadIoPathOverrides();
    provider.SetIgnoreCasc(overrides.ignoreCasc);
    provider.SetIgnoreMpq(overrides.ignoreMpq);
    if (overrides.mpqListSet)
        provider.SetMpqList(std::move(overrides.mpqList));

    // The WhiteoutDex Settings dialog's own MPQ load order, applied last so it
    // wins: it is the one a user of this plug-in can actually edit, while the
    // WhiteoutFlakes ini above is a file almost none of them have. Empty means
    // "never customised" — the provider then keeps whichever list it already
    // had, which is the point of the distinction.
    //
    // Absolute paths, so StorageBuilder::Archives' `installPath / name` join
    // lands on the archive wherever it actually is. That join is also why this
    // needs an install path at all: with none set the builder opens nothing, but
    // a provider with no install path has no Warcraft III to draw either.
    auto listed = wdx::mpq::ResolvedArchivesUtf8(plugcfgDir);
    if (!listed.empty()) {
        mprintf(_M("WhiteoutDex: MPQ load order - %d archive(s), '%hs' first\n"),
                static_cast<int>(listed.size()), listed.front().c_str());
        provider.SetMpqList(std::move(listed));
    }
}

// ============================================================================
// BuildSceneAndSpawn — everything WhiteoutFlakesStart does between "the window
// is up" and "the timeline callback is hooked": mint an adapter, walk the Max
// scene at t=0, spawn the actor, pick HD vs SD and publish the host-side
// snapshots.
//
// Shared with the Resync path, which throws the outgoing actor away and runs
// this again against the same SceneManager / RenderService / RenderWindow.
// Max UI thread only.
// ============================================================================
static bool BuildSceneAndSpawn(Interface* ip) {
    if (!ip || !g_scene || !g_renderer || !g_renderWindow)
        return false;

    std::wstring plugcfgDir;
    const std::string userInstallPath = ReadUserInstallPath(plugcfgDir);

    g_adapter = std::make_shared<whiteout::flakes::MaxSceneAdapter>();
    // The adapter has its own FileContentProvider (independent of
    // SceneManager's), used during CollectScene for CASC/MPQ texture reads, so
    // it needs the same W3Path and the same overrides.
    if (!userInstallPath.empty())
        g_adapter->GetContentProvider().SetInstallPath(userInstallPath);
    ApplyIoOverridesTo(g_adapter->GetContentProvider(), plugcfgDir);
    // And the scene's art tier, saved on rootNode (wdx_scene_art_tier.h) - but
    // only when the scene names one. The collect below is what tells HD from
    // SD, so Auto ("follow the render mode") has no mode to follow yet; it
    // stays Classic, which is what this provider always read before the tier
    // existed.
    using whiteout::flakes::Wc3ArtTier;
    const wdx::scene::ArtTier sceneTier = wdx::scene::ReadSceneArtTier(ip->GetRootNode());
    const std::optional<Wc3ArtTier> pinnedTier =
        sceneTier == wdx::scene::ArtTier::Auto
            ? std::nullopt
            : std::optional<Wc3ArtTier>(static_cast<Wc3ArtTier>(static_cast<int>(sceneTier) - 1));
    g_renderWindow->PublishSceneArtTier(static_cast<i32>(sceneTier));
    g_adapter->GetContentProvider().SetArtTier(pinnedTier.value_or(Wc3ArtTier::Classic));
    // Cross-model dedup: skip BLP/CASC decode for textures another model
    // already uploaded. SpawnUnitFromSource sets this too, but setting it now
    // means CollectScene's incidental texture reads also benefit.
    g_adapter->SetTextureCacheQuery(
        [](std::string_view k) { return g_renderer->Assets().IsTextureCached(k); });
    // Ranges pushed by WdxSequenceManager outlive the adapter that held them.
    if (!g_sequenceRanges.empty())
        g_adapter->SetSequenceRanges(g_sequenceRanges);

    // Walk the Max scene at t=0 for stable bind-pose extraction.
    const TimeValue savedTime = ip->GetTime();
    ip->SetTime(0, FALSE);
    mprintf(_M("WhiteoutDex: Collecting scene...\n"));
    g_adapter->CollectScene();

    // ---- One-call spawn ----
    // Pulls all static data via IModelDataSource::Build, builds an inline
    // ModelTemplate, registers attachment + PE1 configs, and binds the actor's
    // AnimationDriver to the adapter (which is also an IAnimationSource).
    mprintf(_M("WhiteoutDex: Loading model...\n"));
    g_actor = g_renderer->Loader().SpawnUnitFromSource(g_adapter);
    if (!g_actor) {
        mprintf(_M("WhiteoutDex: ERROR - SpawnUnitFromSource failed\n"));
        ip->SetTime(savedTime, FALSE);
        return false;
    }
    // Max scrubs Max's timeline; the renderer's per-frame ticker must skip its
    // own evaluation pass and let EvalFromMax push the cursor instead.
    g_actor->role = whiteout::flakes::renderer::model::ActorRole::External;

    {
        using whiteout::flakes::ProductId;
        if (g_scene->Product() != ProductId::Wc3) {
            g_scene->SetProduct(ProductId::Wc3);
            g_renderer->Settings().MarkRenderModeDirty();
        }
        g_renderer->EnsureWc3GameData();
    }

    // Pick HD vs SD by walking the adapter's freshly extracted materials.
    // `Actor::PreferredRenderMode()` would be the natural choice, but the
    // ModelLoader::AddModel path (used by SpawnUnitFromSource) never populates
    // `Actor::sourceTemplate`, so that accessor short-circuits to SD regardless
    // of the layers' actual shaderIds. Reading the IR materials from the adapter
    // directly mirrors what the renderer's PreferredRenderMode would do if
    // sourceTemplate were wired: any layer with shaderId != 0 (HD = 1,
    // SD_on_HD = 2, Crystal = 24) promotes the whole model to HD.
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

        // Which files the scene reads is a separate question from how it
        // draws them: a Definitive model is HD *and* Definitive. The scene's
        // pin is its own override; Auto clears it, and now that the mode is
        // known, "follow the render mode" has an answer too. ClearArtTier
        // leaves the provider on whatever it last read, and the asset pump
        // only re-arms it per need, so arm it here for the readers that go
        // straight to it (sound, the DNC rig).
        if (pinnedTier)
            g_scene->SetArtTier(*pinnedTier);
        else
            g_scene->ClearArtTier();
        const Wc3ArtTier tier = g_renderer->EffectiveArtTier(*g_scene);
        if (auto* provider = g_scene->ActiveContentProviderIfAny())
            provider->SetArtTier(tier);
        const MCHAR* tierName = tier == Wc3ArtTier::Definitive ? _M("Definitive")
                                : tier == Wc3ArtTier::Reforged ? _M("Reforged")
                                                               : _M("Classic");
        mprintf(_M("WhiteoutDex: render mode = %s, art tier = %s%s\n"),
                mode == RenderMode::HD ? _M("HD") : _M("SD"), tierName,
                pinnedTier ? _M("") : _M(" (follows render mode)"));
    }
    g_renderWindow->SetFocusActor(g_actor->handle);

    // Push the actor's discovered sequences into the preview window so the
    // ImGui Animation dropdown can pick which sub-range Max's timeline scrubs
    // through.
    {
        auto seqs = g_actor->animation.Sequences();
        if (seqs.empty()) {
            // The normal case here: MaxSceneAdapter has no SEQS chunk to
            // report, so the sequences arrive from the Sequence Manager through
            // WhiteoutFlakesPushSequences instead. Re-publish that cache rather
            // than blanking the combo for the rest of a Resync.
            PublishTimelineSequencesToWindow();
        } else {
            std::vector<std::string> names;
            names.reserve(seqs.size());
            for (auto& s : seqs)
                names.push_back(s.name);
            g_renderWindow->SetSequences(std::move(names), std::move(seqs));
        }
    }

    // Restore Max's time and run an eval so the model is visible before
    // TimeChanged starts firing again.
    ip->SetTime(savedTime, FALSE);
    EvalFromMax(MaxTimeToMs(ip->GetTime()));
    return true;
}

// ============================================================================
// WhiteoutFlakesRebuild — the Resync: drop the actor and re-run the whole
// collect/spawn pass against the live scene, keeping the window, the graphics
// device and every renderer setting exactly where they were. Max UI thread only.
// ============================================================================
static bool WhiteoutFlakesRebuild() {
    if (!g_running || g_rebuilding || !g_renderer || !g_scene || !g_renderWindow)
        return false;
    if (!g_renderWindow->IsOpen())
        return false;
    Interface* ip = GetCOREInterface();
    if (!ip)
        return false;

    auto start = std::chrono::high_resolution_clock::now();
    g_rebuilding = true;
    // Park the render thread: the reap below and the spawn that follows both
    // mutate the actor map RenderFrame walks every frame.
    g_renderWindow->SuspendRendering();

    // Clearing g_actor first also disarms the TimeChanged callback for the
    // duration — BuildSceneAndSpawn moves Max's time cursor to 0 and back, and
    // an eval against a half-torn-down scene is exactly the crash we're
    // avoiding.
    g_actor = nullptr;
    // Asynchronous by contract: the outgoing actors are flagged here and reaped,
    // GPU resources and all, by the render thread's next CommitPendingUploads.
    // Dropping our own adapter handle is safe — the outgoing actor's
    // AnimationDriver keeps it alive until then. The freshly spawned actor is
    // not flagged, so it survives that reap.
    g_renderer->Loader().RequestClearAll();
    g_adapter.reset();

    const bool ok = BuildSceneAndSpawn(ip);

    g_renderWindow->ResumeRendering();
    g_rebuilding = false;
    g_lastTimeChangedTick = GetTickCount();

    auto end = std::chrono::high_resolution_clock::now();
    const i32 ms = (i32)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    if (ok) {
        mprintf(_M("WhiteoutDex: === RESYNCED in %d ms ===\n"), ms);
        mprintf(_M("  %d geosets, %d materials, %d collisions\n"),
                (i32)g_actor->render.gpuGeosets.size(), (i32)g_actor->render.surfaces.size(),
                (i32)g_actor->render.collisionShapes.size());
    } else {
        mprintf(_M("WhiteoutDex: ERROR - resync failed; the preview is now empty\n"));
    }
    return ok;
}

// Re-run the toolkit's sequence push so a Resync also picks up sequence edits
// made since the last one. Best-effort: the function lives in
// SequenceManager.ms, and a user whose scripts aren't installed simply keeps the
// ranges cached from the last push.
//
// Only ever called from the timer, never from the MaxScript primitive — the
// primitive already runs inside a MaxScript evaluation, and its caller pushes
// sequences itself the way WhiteoutDex_Renderer does after Start.
static void RefreshSequencesFromMaxScript() {
    const MCHAR* script = _M("try(WdxPushRendererSequences())catch()");
#if MAX_VERSION_MAJOR >= 24 // 3ds Max 2022 added the ScriptSource argument
    // NonEmbedded, not Dynamic: the string is a compile-time literal in this
    // .dlx with no caller-supplied input spliced into it, which is exactly the
    // case NonEmbedded describes. Dynamic would run it under Safe Scene Script
    // Execution's restricted rights for no reason.
    ExecuteMAXScriptScript(script, MAXScript::ScriptSource::NonEmbedded, TRUE);
#else
    ExecuteMAXScriptScript(script, TRUE);
#endif
}

// ============================================================================
// Viewport-sync timer — the Max-main-thread half of the two toolbar controls
// that need it. Both are raised on the render thread and can only be serviced
// here: reading a ViewExp and re-walking the scene are Max UI thread work, and
// the 500 ms material poll is far too coarse for a camera tracking an orbit.
// ============================================================================
static UINT_PTR g_syncTimerId = 0;

static void CALLBACK ViewportSyncTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_running || g_rebuilding || !g_renderWindow || !g_renderWindow->IsOpen())
        return;

    // The View menu's Art Tier pick: write it onto the scene through the same
    // MaxScript writer the Settings dialog and the importer use, then rebuild,
    // which is where BuildSceneAndSpawn reads it back and applies it.
    const bool artTierPicked = [] {
        i32 stored = 0;
        if (!g_renderWindow->ConsumeSceneArtTierRequest(stored))
            return false;
        // An integer from a fixed menu is all that is spliced in, so this is
        // still the NonEmbedded case RefreshSequencesFromMaxScript describes.
        wchar_t script[96];
        swprintf_s(script, L"try(::WdxSceneData.setArtTier %d resync:false)catch()", stored);
#if MAX_VERSION_MAJOR >= 24
        ExecuteMAXScriptScript(script, MAXScript::ScriptSource::NonEmbedded, TRUE);
#else
        ExecuteMAXScriptScript(script, TRUE);
#endif
        return true;
    }();

    // A rebuild is long enough that servicing the camera on the same tick would
    // just push a pose at a scene that is about to be replaced.
    if (g_renderWindow->ConsumeResyncRequest() || artTierPicked) {
        if (WhiteoutFlakesRebuild())
            RefreshSequencesFromMaxScript();
        return;
    }

    // Animation combo -> Max's timeline, then Max's timeline -> the combo.
    // SetAnimRange and GetAnimRange are both Max-UI-thread work, which is why
    // the render thread can only leave a request behind.
    Interface* ip = GetCOREInterface();
    if (i32 req = -1; g_renderWindow->ConsumeSequenceRequest(req))
        ApplySequenceRequest(ip, req);
    PublishActiveSequence(ip);

    if (!g_renderWindow->SyncCamera())
        return;
    // Re-read every tick rather than caching a ViewExp, which is what makes
    // this follow the user switching viewports or moving the camera.
    whiteout::flakes::ViewportCameraPose pose;
    if (whiteout::flakes::ReadActiveViewportCamera(pose)) {
        g_renderWindow->SetExternalCameraPose(pose.position, pose.target, pose.roll,
                                              pose.fovHorizontal);
    }
}

// ============================================================================
// Helpers
// ============================================================================
static void WhiteoutFlakesCleanup() {
    if (g_materialTimerId) {
        KillTimer(nullptr, g_materialTimerId);
        g_materialTimerId = 0;
    }
    if (g_syncTimerId) {
        KillTimer(nullptr, g_syncTimerId);
        g_syncTimerId = 0;
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
    // The ranges describe the scene we just let go of — File > Open routes
    // through here, so keeping them would hand the next model the previous
    // one's sequence gate. WhiteoutDex_Renderer re-pushes on every Start.
    g_sequenceRanges.clear();
    g_timelineSequences.clear();
    g_publishedSequenceIdx = -2;
    g_rebuilding = false;
    mprintf(_M("WhiteoutDex: === STOPPED ===\n"));
}

// ============================================================================
// Hooks for the asset picker (asset_picker_window.cpp), which lives in its own
// TU but needs two things only this one owns: the preview window, and the
// module handle.
// ============================================================================
#include "asset_picker_window.h"

namespace whiteout::flakes {

WdxPreviewPause::WdxPreviewPause() {
    if (!g_running || !g_renderWindow || !g_renderWindow->IsOpen())
        return;
    // Disable before parking: once the render thread stops drawing, a window
    // that still accepts clicks is a window that looks hung.
    if (HWND h = g_renderWindow->GetParentHWND()) {
        ::EnableWindow(h, FALSE);
        hwnd_ = h;
    }
    g_renderWindow->SuspendForModal();
    paused_ = true;
}

WdxPreviewPause::~WdxPreviewPause() {
    if (hwnd_)
        ::EnableWindow(static_cast<HWND>(hwnd_), TRUE);
    // Re-check the window: the user can close the preview while the picker is
    // up, and the poll timer will have run WhiteoutFlakesCleanup by now.
    if (paused_ && g_renderWindow)
        g_renderWindow->Resume();
}

HINSTANCE WdxPluginInstance() {
    return g_hInstance;
}

} // namespace whiteout::flakes

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInstance = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    // Deliberately nothing on DLL_PROCESS_DETACH. Calling
    // WhiteoutFlakesCleanup() here crashed Max on *every* exit: the loader
    // lock is held, other DLLs may already be detached, and the cleanup's
    // trailing mprintf() resolves MaxScript's context out of TLS
    // (thread_locals_index) and calls a virtual on it. By process-detach time
    // that slot is gone, so TlsGetValue returned null and the call
    // dereferenced 0x28 — the 0xC0000005 at RVA 0xa0d3 that WER kept
    // reporting against this module.
    //
    // Real teardown already has proper homes, all of which run while Max is
    // still alive: NOTIFY_SYSTEM_SHUTDOWN / PRE_RESET / FILE_PRE_OPEN /
    // FILE_PRE_MERGE (see OnMaxSceneEvent) and the explicit Stop primitive.
    // At process exit there is nothing left worth releasing by hand.
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

    // Point the renderer's disk file resolver at the .dlx directory and the
    // install root — the installer drops the one shared `shaders/` pack (BLS
    // bundles RenderPipeline::InitDevice loads via the content provider) at
    // the root; see wdx_install_layout.h.
    //
    // The base paths are for the disk-side FileResolver and are the ONLY
    // thing the BLS shader lookup cares about. Earlier this code also called
    // `SetInstallPath(dlxDir)` — that was wrong, InstallPath drives the
    // CASC/MPQ storage roots, and pinning it at the .dlx directory meant
    // the renderer treated the plug-in folder as the WC3 install no matter
    // what the user typed into the Settings dialog. The W3Path read below
    // restores the correct user-configured value.
    wdx::PointAtInstalledAssets(g_scene->GetContentProvider(), g_hInstance);

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
    // user only has to type the install path once. BuildSceneAndSpawn reads it
    // again for the MaxSceneAdapter's own provider — the adapter has its own
    // FileContentProvider instance used during scene collection, and it needs
    // the same root.
    //
    // plugcfgDir is kept because the MPQ load order lives in the same folder.
    std::wstring plugcfgDir;
    const std::string userInstallPath = ReadUserInstallPath(plugcfgDir);
    if (!userInstallPath.empty()) {
        g_scene->GetContentProvider().SetInstallPath(userInstallPath);
        mprintf(_M("WhiteoutDex: CASC W3Path = '%hs'\n"), userInstallPath.c_str());
    }

    // The Warcraft III art tier is the scene's, not a global preference:
    // BuildSceneAndSpawn reads it off rootNode on every rebuild. Clear any
    // global a previous session could have left, so an Auto scene really does
    // follow its render mode.
    g_renderer->Settings().SetArtTier(std::nullopt);

    // UI language. Done here rather than at DllMain time because plugcfgDir is
    // only known once GetCOREInterface() is usable, and re-reading it on every
    // start is what lets a language change in the Settings dialog take effect
    // the next time the renderer is opened — no 3ds Max restart.
    wdx::ui::InitLanguage(g_hInstance, plugcfgDir);

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

    ApplyIoOverridesTo(g_scene->GetContentProvider(), plugcfgDir);

    // Audio: the same cubeb-backed ISoundEmitter the standalone exe uses.
    // Borrows the scene's content provider for CASC/MPQ lookup so SND
    // EventObjects play during preview. Without this, the renderer's
    // default null emitter drops every fire.
    g_renderer->SwapSoundEmitter(
        std::make_unique<whiteout::flakes::CubebSoundEmitter>(g_scene->ActiveContentProvider()));

    // ---- Build the live adapter, walk the scene, spawn the actor ----
    // Shared verbatim with the Resync path; see BuildSceneAndSpawn.
    //
    // Camera presets are no longer plumbed into the renderer — Max owns its
    // own viewport, so MaxSceneAdapter::GetCameraPresets() is currently unused
    // by the plugin and the toolbar's Sync Camera checkbox reads the live
    // viewport instead.
    if (!BuildSceneAndSpawn(ip)) {
        WhiteoutFlakesCleanup();
        return Integer::intern(-1);
    }

    // Hook Max's timeline + start the polling timers: 500 ms for material
    // hot-reload, 20 ms for the viewport camera sync and the Resync request
    // the toolbar raises from the render thread.
    g_timeCallback = new WhiteoutFlakesTimeCallback();
    ip->RegisterTimeChangeCallback(g_timeCallback);
    g_running = true;
    g_materialTimerId = SetTimer(nullptr, 0, 500, MaterialPollTimer);
    g_syncTimerId = SetTimer(nullptr, 0, 20, ViewportSyncTimer);

    auto end = std::chrono::high_resolution_clock::now();
    i32 ms = (i32)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Diagnostic readout from the actor's render-side counts.
    mprintf(_M("\nWhiteoutDex: === STARTED in %d ms ===\n"), ms);
    // Materials moved behind the product ISurfaceTable; render.surfaces is
    // resized to Materials().size() on every upload, so it is the same count
    // without reaching through a Wc3SurfaceTable cast for a diagnostic.
    mprintf(_M("  %d geosets, %d materials\n"), (i32)g_actor->render.gpuGeosets.size(),
            (i32)g_actor->render.surfaces.size());
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
// WhiteoutFlakesIsRunning() -> true while the preview window is up.
//
// So MaxScript callers that want to keep the preview in step with an edit -
// the Sequence Manager re-pushing its list after a save, say - can skip the
// work, and the diagnostics that go with it, when nobody is watching.
// ============================================================================

def_visible_primitive(WhiteoutFlakesIsRunning, "WhiteoutFlakesIsRunning");
Value* WhiteoutFlakesIsRunning_cf(Value** /*arg_list*/, i32 count) {
    check_arg_count(WhiteoutFlakesIsRunning, 0, count);
    const bool up = g_running && g_renderWindow && g_renderWindow->IsOpen();
    return up ? &true_value : &false_value;
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
    std::vector<TimelineSequence> timeline;
    ranges.reserve(static_cast<usize>(n));
    timeline.reserve(static_cast<usize>(n));
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

        TimelineSequence ts;
        ts.name = r.name;
        ts.startTick = sTv;
        ts.endTick = eTv;
        ts.startMs = r.startMs;
        ts.endMs = r.endMs;
        timeline.push_back(std::move(ts));

        ranges.push_back(std::move(r));
    }
    // Cached as well as pushed: a Resync mints a fresh adapter, and this is the
    // only copy of the ranges on the C++ side.
    g_sequenceRanges = ranges;
    g_adapter->SetSequenceRanges(std::move(ranges));

    // Same data, Max's units, for the toolbar's Animation combo. Republish the
    // active index unconditionally - the list just changed underneath it, so
    // the index it was showing means nothing now.
    g_timelineSequences = std::move(timeline);
    PublishTimelineSequencesToWindow();
    g_publishedSequenceIdx = -2;
    PublishActiveSequence(GetCOREInterface());

    mprintf(_M("WhiteoutDex: pushed %d sequence ranges\n"), n);
    return Integer::intern(n);
}

// ============================================================================
// WhiteoutFlakesResync() — re-extract the entire scene into the running
// preview: geometry, materials, textures, bones, emitters, attachments and
// collision shapes, exactly as WhiteoutFlakesStart does. The window, the
// graphics device and every renderer setting survive.
//
// Callers should push sequences afterwards the same way WhiteoutDex_Renderer
// does after Start — this primitive is already running inside a MaxScript
// evaluation and does not re-enter the scripter to do it for them. (The
// toolbar's Resync button, which is not, does re-push them.)
// ============================================================================

def_visible_primitive(WhiteoutFlakesResync, "WhiteoutFlakesResync");
Value* WhiteoutFlakesResync_cf(Value** /*arg_list*/, i32 count) {
    check_arg_count(WhiteoutFlakesResync, 0, count);
    if (!g_running || !g_renderer || !g_renderWindow) {
        mprintf(_M("WhiteoutDex: Resync: the renderer is not running — start it first\n"));
        return &false_value;
    }
    return WhiteoutFlakesRebuild() ? &true_value : &false_value;
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
