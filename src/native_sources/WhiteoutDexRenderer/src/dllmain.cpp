// ============================================================================
// WhiteoutDex — All-in-One Max SDK Plugin (.dlx)
// Adapter-Pattern: MaxSceneAdapter → IModelSource → Renderer
//
// MaxScript API:
//   ndxStart()   → Extract scene + open renderer + start sync
//   ndxStop()    → Stop everything + close window
// ============================================================================

#include "max_scene_adapter.h"
#include "renderer/renderer.h"

#include <max.h>
#include <maxversion.h>
#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>
#include <maxscript/foundation/numbers.h>
#include <maxscript/macros/define_instantiation_functions.h>

#include <chrono>

// ============================================================================
// Global state
// ============================================================================
static WhiteoutDex::MaxSceneAdapter* g_adapter  = nullptr;
static WhiteoutDex::Renderer*        g_renderer = nullptr;
static bool                           g_running  = false;
static HINSTANCE                      g_hInstance = nullptr;

// ============================================================================
// TimeChange callback — asks adapter to Evaluate, passes result to renderer
// ============================================================================
class NdxTimeCallback : public TimeChangeCallback {
public:
    void TimeChanged(TimeValue t) override {
        if (!g_running || !g_renderer || !g_adapter) return;
        if (!g_renderer->IsOpen()) { /* will be cleaned up by ndxStop */ return; }
        int tpf = GetTicksPerFrame(), fps = GetFrameRate();
        int timeMs = (tpf > 0 && fps > 0)
            ? (int)((float)t / (float)tpf * 1000.0f / (float)fps)
            : 0;

        g_lastTimeChangedTick = GetTickCount();
        WhiteoutDex::FrameState state = g_adapter->Evaluate(timeMs);
        g_renderer->ApplyFrameState(state, timeMs);
    }
};

static NdxTimeCallback* g_timeCallback = nullptr;
static DWORD g_lastTimeChangedTick = 0;

// ============================================================================
// Material polling timer — detects property changes even without timeline scrub
// ============================================================================
static UINT_PTR g_materialTimerId = 0;

static void CALLBACK MaterialPollTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_running || !g_renderer || !g_adapter) return;
    if (!g_renderer->IsOpen()) return;

    // Check for material property changes
    auto result = g_adapter->RefreshMaterials();
    if (result.changed) {
        g_renderer->UpdateMaterials(result.materials, result.textures);
    }

    // Re-evaluate frame state only when the timeline is idle — this picks up
    // non-animated changes (vertex colors, modifiers, visibility toggles).
    // Skip when TimeChanged is actively firing to avoid conflicting updates
    // that cause flicker during animation playback.
    DWORD now = GetTickCount();
    if (now - g_lastTimeChangedTick > 1000) {
        Interface* ip = GetCOREInterface();
        if (ip) {
            TimeValue t = ip->GetTime();
            int tpf = GetTicksPerFrame(), fps = GetFrameRate();
            int timeMs = (tpf > 0 && fps > 0)
                ? (int)((float)t / (float)tpf * 1000.0f / (float)fps) : 0;
            WhiteoutDex::FrameState state = g_adapter->Evaluate(timeMs);
            g_renderer->ApplyFrameState(state, timeMs);
        }
    }
}

// ============================================================================
// Helpers
// ============================================================================
static void NdxCleanup() {
    if (g_materialTimerId) {
        KillTimer(nullptr, g_materialTimerId);
        g_materialTimerId = 0;
    }
    if (g_timeCallback) {
        Interface* ip = GetCOREInterface();
        if (ip) ip->UnRegisterTimeChangeCallback(g_timeCallback);
        delete g_timeCallback; g_timeCallback = nullptr;
    }
    g_running = false;
    if (g_renderer) {
        g_renderer->ClearModel();
        g_renderer->Close();
        delete g_renderer; g_renderer = nullptr;
    }
    if (g_adapter) { delete g_adapter; g_adapter = nullptr; }
    mprintf(_M("WhiteoutDex: === STOPPED ===\n"));
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInstance = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    if (reason == DLL_PROCESS_DETACH) {
        NdxCleanup();
    }
    return TRUE;
}

// ============================================================================
// Max Plugin Descriptor
// ============================================================================
class WhiteoutDexExtractorClassDesc : public ClassDesc2 {
public:
    int            IsPublic() override     { return FALSE; }
    void*          Create(BOOL) override   { return nullptr; }
    const MCHAR*   ClassName() override    { return _M("WhiteoutDexExtractor"); }
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    const MCHAR*   NonLocalizedClassName() override { return _M("WhiteoutDexExtractor"); }
#endif
    SClass_ID      SuperClassID() override { return GUP_CLASS_ID; }
    Class_ID       ClassID() override      { return Class_ID(0x4e444558, 0x45585452); }
    const MCHAR*   Category() override     { return _M("WhiteoutDex"); }
    const MCHAR*   InternalName() override { return _M("WhiteoutDexExtractor"); }
    HINSTANCE      HInstance() override    { return g_hInstance; }
};

static WhiteoutDexExtractorClassDesc g_classDesc;

extern "C" {
    __declspec(dllexport) const MCHAR* LibDescription()    { return _M("WhiteoutDex All-in-One Preview"); }
    __declspec(dllexport) int          LibNumberClasses()  { return 1; }
    __declspec(dllexport) ClassDesc*   LibClassDesc(int i) { return (i == 0) ? &g_classDesc : nullptr; }
    __declspec(dllexport) ULONG        LibVersion()        { return VERSION_3DSMAX; }
}

// ============================================================================
// ndxStart() — collect scene via adapter, load into renderer
// Returns extraction time in ms, or -1 on error
// ============================================================================

def_visible_primitive(ndxStart, "ndxStart");
Value* ndxStart_cf(Value** arg_list, int count)
{
    check_arg_count(ndxStart, 0, count);
    auto start = std::chrono::high_resolution_clock::now();

    if (g_running) NdxCleanup();

    // Create renderer
    g_renderer = new WhiteoutDex::Renderer();
    if (!g_renderer->Open(800, 600)) {
        mprintf(_M("WhiteoutDex: ERROR - Could not open renderer window\n"));
        delete g_renderer; g_renderer = nullptr;
        return Integer::intern(-1);
    }

    // Make renderer window float above Max
    Interface* ip = GetCOREInterface();
    HWND ndxWnd = FindWindowW(L"WhiteoutDexRendererClass", nullptr);
    if (ndxWnd && ip)
        SetWindowLongPtrW(ndxWnd, GWLP_HWNDPARENT, (LONG_PTR)ip->GetMAXHWnd());

    // Collect scene data
    TimeValue savedTime = ip->GetTime();
    ip->SetTime(0, FALSE);

    g_adapter = new WhiteoutDex::MaxSceneAdapter();
    mprintf(_M("WhiteoutDex: Collecting scene...\n"));
    g_adapter->CollectScene();

    // Get typed data and load into renderer
    mprintf(_M("WhiteoutDex: Loading model...\n"));
    auto meshes     = g_adapter->GetMeshes();
    auto textures   = g_adapter->GetTextures();
    auto materials  = g_adapter->GetMaterials();
    auto skeleton   = g_adapter->GetSkeleton();
    auto skinW      = g_adapter->GetSkinWeights();
    auto particles  = g_adapter->GetParticleConfigs();
    auto ribbons    = g_adapter->GetRibbonConfigs();
    auto collisions = g_adapter->GetCollisionShapes();

    g_renderer->LoadModel(meshes, textures, materials, skeleton,
                          skinW, particles, ribbons, collisions);

    // Populate camera combo with scene cameras
    auto cameras = g_adapter->GetCameraPresets();
    if (!cameras.empty())
        g_renderer->SetCameraPresets(cameras);

    // Restore time and do initial evaluation
    ip->SetTime(savedTime, FALSE);
    TimeValue t = ip->GetTime();
    int tpf = GetTicksPerFrame(), fps = GetFrameRate();
    int timeMs = (tpf > 0 && fps > 0)
        ? (int)((float)t / (float)tpf * 1000.0f / (float)fps) : 0;

    WhiteoutDex::FrameState state = g_adapter->Evaluate(timeMs);
    g_renderer->ApplyFrameState(state, timeMs);

    // Register time callback
    g_timeCallback = new NdxTimeCallback();
    ip->RegisterTimeChangeCallback(g_timeCallback);
    g_running = true;

    // Start material polling timer (500ms interval)
    g_materialTimerId = SetTimer(nullptr, 0, 500, MaterialPollTimer);

    auto end = std::chrono::high_resolution_clock::now();
    int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    mprintf(_M("\nWhiteoutDex: === STARTED in %d ms ===\n"), ms);
    mprintf(_M("  %d meshes, %d textures, %d materials, %d bones\n"),
            (int)meshes.size(), (int)textures.size(), (int)materials.size(), skeleton.boneCount);
    mprintf(_M("  %d particles, %d ribbons, %d collisions\n"),
            (int)particles.size(), (int)ribbons.size(), (int)collisions.size());

    return Integer::intern(ms);
}

// ============================================================================
// ndxStop() — stop sync, close renderer
// ============================================================================

def_visible_primitive(ndxStop, "ndxStop");
Value* ndxStop_cf(Value** arg_list, int count)
{
    check_arg_count(ndxStop, 0, count);
    NdxCleanup();
    return &ok;
}

// ============================================================================
// ndxRefreshMaterials() — force re-read of material properties
// Returns true if anything changed
// ============================================================================

def_visible_primitive(ndxRefreshMaterials, "ndxRefreshMaterials");
Value* ndxRefreshMaterials_cf(Value** arg_list, int count)
{
    check_arg_count(ndxRefreshMaterials, 0, count);
    if (!g_running || !g_renderer || !g_adapter)
        return &false_value;

    auto result = g_adapter->RefreshMaterials();
    if (result.changed) {
        g_renderer->UpdateMaterials(result.materials, result.textures);
        mprintf(_M("WhiteoutDex: Materials refreshed (%d materials, %d textures)\n"),
                (int)result.materials.size(), (int)result.textures.size());
        return &true_value;
    }
    return &false_value;
}
