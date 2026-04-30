#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Win32 Render Window
// Wraps all Win32 UI: parent window, toolbar, child render surface, mouse input.
// Calls back into Renderer for camera/display/model operations.
// ============================================================================

#include "model_types.h"     // CameraPreset
#include "render_target.h"   // DisplayFlags, RenderTargetId
#include "gfx/gfx_types.h"   // GfxApi
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace WhiteoutDex {

class RenderService;

class RenderWindow {
public:
    explicit RenderWindow(RenderService& service);
    ~RenderWindow();

    // Lifecycle — spawns / joins the render thread
    bool Open(int width, int height, gfx::GfxApi api = gfx::GfxApi::D3D12);
    void Close();
    bool IsOpen() const;

    // Create the Win32 window hierarchy (parent + child render surface + toolbar)
    bool Create(int width, int height);
    void Destroy();
    void Show();

    // Pump Win32 messages. Returns false if WM_QUIT received.
    bool PumpMessages();

    // Called from render loop to apply pending camera/sequence updates to combos
    void ProcessCameraPresets();
    void ProcessSequences();

    // Window handles
    HWND GetParentHWND() const { return hwnd_; }
    HWND GetRenderHWND() const { return hwndRender_; }

    // Set window title (FPS display)
    void SetTitle(const wchar_t* title);

    // Invalidate the team color swatch after SetTeamColor()
    void InvalidateTeamColorSwatch();

    // Query active combo box selections
    int GetActiveCameraIndex() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleSettingsMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Render thread function (owned by this window)
    void ThreadFunc(int width, int height, gfx::GfxApi api);

    // Lazily build the Settings popup the first time the menu item is
    // clicked. The window is hidden on close instead of destroyed so
    // re-opening is instant and the controls keep their state.
    void EnsureSettingsWindow();

    RenderService& service_;

    // Windows
    HWND hwnd_ = nullptr;
    HWND hwndRender_ = nullptr;

    // Toolbar: only the live controls users touch every frame
    // (Team / Camera / Animation). Show/hide toggles + debug pickers
    // moved to the menu bar (View + Debug).
    HWND btnTeamColor_ = nullptr;
    HWND cmbCamera_    = nullptr;
    HWND lblSequence_  = nullptr;
    HWND cmbSequence_  = nullptr;
    HWND cmbLighting_  = nullptr;
    HWND btnBgColor_   = nullptr;
    HWND sldExposure_  = nullptr;
    HWND lblExposure_  = nullptr;
    HWND sldSndVolume_ = nullptr;
    HWND lblSndVolume_ = nullptr;
    HMENU hMenuBar_      = nullptr;
    HMENU hMenuView_     = nullptr;
    HMENU hMenuProbe_    = nullptr;
    HMENU hMenuTileset_  = nullptr;
    HMENU hMenuDebug_    = nullptr;
    HMENU hMenuDebugVis_ = nullptr;
    HMENU hMenuLod_      = nullptr;

    // Settings popup — owns the Background colour swatch + Exposure slider
    // controls (moved off the toolbar so the toolbar only carries the
    // live, frame-touched controls). Created lazily on first click of
    // the &Settings menu entry; hidden on close, never destroyed until
    // the parent window itself goes away.
    HWND hwndSettings_ = nullptr;

    // Menu item IDs. Ranged enums for the two submenu groups so the
    // WM_COMMAND handler can dispatch by range instead of a case per
    // entry.
    enum : UINT {
        IDC_TEAMCOLOR = 1001,
        IDC_CAMERA,
        IDC_SEQUENCE,
        IDC_LIGHTING,
        IDC_BGCOLOR,
        IDC_EXPOSURE,
        IDC_SND_VOLUME,
        // View menu toggles
        IDM_VIEW_GRID      = 1100,
        IDM_VIEW_PARTICLES,
        IDM_VIEW_RIBBONS,
        IDM_VIEW_EVENTS,
        // Debug menu toggles
        IDM_DBG_COLLISIONS = 1200,
        IDM_DBG_LIGHTS,
        // Probe submenu (5 entries; index = id - IDM_PROBE_BASE)
        IDM_PROBE_BASE     = 1300,
        IDM_PROBE_LAST     = IDM_PROBE_BASE + 4,
        // Tileset submenu (16 entries — one per io::Tileset enumerator;
        // index = id - IDM_TILESET_BASE casts straight to io::Tileset)
        IDM_TILESET_BASE   = 1310,
        IDM_TILESET_LAST   = IDM_TILESET_BASE + 15,
        // Debug-vis submenu (8 entries; index = id - IDM_DBGVIS_BASE)
        IDM_DBGVIS_BASE    = 1400,
        IDM_DBGVIS_LAST    = IDM_DBGVIS_BASE + 7,
        // LOD submenu: [0]=Auto, [1]=Force 0, [2]=Force 1, [3]=Force 2, [4]=Force 3
        IDM_LOD_BASE       = 1500,
        IDM_LOD_LAST       = IDM_LOD_BASE + 4,
        // Top-level &Settings menu entry (clickable, no popup) — opens
        // the Settings window for Background colour + Exposure.
        IDM_SETTINGS       = 1600,
    };

    // Mouse state
    bool lmbDown_ = false, rmbDown_ = false, mmbDown_ = false;
    POINT lastMouse_ = {0, 0};

    // Icon
    HICON icon_ = nullptr;

    // Camera presets (local copy for combo selection logic)
    std::vector<CameraPreset> cameraPresets_;

    // Render thread and sync
    std::thread           renderThread_;
    std::atomic<bool>     running_{false};
    std::atomic<bool>     initialized_{false};
    RenderTargetId        targetId_ = 0;

    // Constants
    static constexpr int kToolbarH = 28;
};

} // namespace WhiteoutDex
