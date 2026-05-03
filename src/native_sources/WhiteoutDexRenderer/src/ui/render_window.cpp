// ============================================================================
// WhiteoutDex Real-Time Renderer — Win32 Render Window Implementation
// Extracted from renderer.cpp — all Win32 window, toolbar, and mouse handling.
// ============================================================================

#include "render_window.h"
#include "render_service.h"
#include "settings_ini.h"                             // SaveSettingsIni
#include "../renderer/debug/debug_renderer.h"        // service_.Debug() return type
#include "../renderer/replaceable_texture_manager.h" // service_.Replaceables() return type
#include "../io/replaceable_paths.h"                 // Tileset enum + setter
#include "resource.h"
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <cmath>      // std::floor for the TOD HH:MM split
#include <algorithm>
#include <cctype>     // std::tolower in the IDC_SEQUENCE substring check
#include <cstring>    // std::strlen in the IDC_SEQUENCE substring check
#include <string>

#pragma comment(lib, "comctl32.lib")

#pragma comment(lib, "comdlg32.lib")

namespace WhiteoutDex {

static const wchar_t* WINDOW_CLASS   = L"WhiteoutDexRendererClass";
static const wchar_t* RENDER_CLASS   = L"WhiteoutDexRenderSurface";
static const wchar_t* SETTINGS_CLASS = L"WhiteoutDexSettingsWindow";
static const wchar_t* WINDOW_TITLE   = L"WhiteoutFlakes";

// ============================================================================
// Construction / Destruction
// ============================================================================

RenderWindow::RenderWindow(RenderService& service) : service_(service) {}

RenderWindow::~RenderWindow() { Close(); Destroy(); }

// ============================================================================
// Lifecycle — render thread ownership
// ============================================================================

bool RenderWindow::Open(int w, int h, gfx::GfxApi api) {
    if (running_) return true;
    // Join any previous thread that exited (e.g. user closed the window)
    if (renderThread_.joinable()) renderThread_.join();
    running_ = true;
    initialized_ = false;
    renderThread_ = std::thread(&RenderWindow::ThreadFunc, this, w, h, api);
    for (int i = 0; i < 500 && !initialized_ && running_; ++i) Sleep(10);
    return initialized_;
}

void RenderWindow::Close() {
    running_ = false;
    if (renderThread_.joinable()) {
        if (hwnd_) PostMessage(hwnd_, WM_CLOSE, 0, 0);
        renderThread_.join();
    }
}

bool RenderWindow::IsOpen() const { return running_ && initialized_; }

// ============================================================================
// Render Thread Function
// ============================================================================

void RenderWindow::ThreadFunc(int w, int h, gfx::GfxApi api) {
    if (!Create(w, h))              { running_ = false; return; }
    if (!service_.InitDevice(api))  { running_ = false; Destroy(); return; }

    targetId_ = service_.CreateSwapChainTarget(static_cast<void*>(hwndRender_), w, h);
    if (targetId_ == 0)           { running_ = false; service_.ShutdownDevice(); Destroy(); return; }
    service_.SetPrimaryTarget(targetId_);

    Show();
    initialized_ = true;

    LARGE_INTEGER freq, lastTime, now, fpsTimer;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&lastTime);
    fpsTimer = lastTime;
    int frameCount = 0;
    int lastParentTimeMs = service_.Scene().GetAnimationTime();

    // Frame pacing is handled by `Present(1, 0)` (V-Sync) in the
    // D3D12 backend. The previous manual 60 Hz `Sleep` loop stacked
    // Windows' ~15.6 ms scheduler quantum on top of V-Sync's 16.6 ms
    // wait, capping the visible framerate at ~40 FPS even on
    // high-refresh monitors. Let Present do the pacing alone.
    while (running_) {
        if (!PumpMessages()) { running_ = false; break; }

        QueryPerformanceCounter(&now);
        lastTime = now;

        // Particle/ribbon/PE1 simulation dt is derived from the parent's
        // animation clock so everything stays locked to Max's timeline:
        // when Max is paused, sims freeze; when playing, sims advance at
        // exactly the parent's playback rate.
        int curParentMs = service_.Scene().GetAnimationTime();
        int parentDtMs  = curParentMs - lastParentTimeMs;
        if (parentDtMs < 0)   parentDtMs = 0;     // backward scrub → freeze
        if (parentDtMs > 100) parentDtMs = 100;    // clamp big jumps
        lastParentTimeMs = curParentMs;
        float parentDt = (float)parentDtMs / 1000.0f;

        // Process pending camera preset / sequence updates
        ProcessCameraPresets();
        ProcessSequences();
        if (service_.Replaceables().ConsumeDirty()) InvalidateTeamColorSwatch();
        // Drain auto-HD notifications — no user UI to sync, but we
        // still clear the flag so it doesn't leak state.
        (void)service_.ConsumeRenderModeDirty();

        // Advance simulation and render
        service_.Tick(parentDt);
        service_.RenderFrame(targetId_);
        service_.Present(targetId_);
        frameCount++;

        double fpsDt = (double)(now.QuadPart - fpsTimer.QuadPart) / freq.QuadPart;
        if (fpsDt >= 1.0) {
            int nGeo = 0, nTex = 0, nNodes = 0, nParts = 0, nSegs = 0;
            service_.GetFrameStats(nGeo, nTex, nNodes, nParts, nSegs);
            wchar_t title[300];
            swprintf_s(title,
                L"WhiteoutFlakes \u2014 %d FPS | %d geo, %d tex, %d nodes, %d parts, %d segs",
                frameCount, nGeo, nTex, nNodes, nParts, nSegs
            );
            SetTitle(title);
            frameCount = 0;
            fpsTimer = now;
        }
    }

    service_.ShutdownDevice();
    Destroy();
    initialized_ = false;
}

// ============================================================================
// Create Win32 Window Hierarchy
// ============================================================================

bool RenderWindow::Create(int w, int h) {
    // Get HINSTANCE of the module containing this code (DLL or EXE)
    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCWSTR)&RenderWindow::WndProc, &hMod);
    HINSTANCE hInst = hMod ? (HINSTANCE)hMod : GetModuleHandle(nullptr);

    // Register the trackbar (msctls_trackbar32) class — required before
    // CreateWindow can instantiate the exposure slider below.
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    // Load icon from embedded resource
    icon_ = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_WHITEOUT_ICON));

    // Register parent window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = RenderWindow::WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon          = icon_;
    wc.hIconSm        = icon_;
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName  = WINDOW_CLASS;
    if (!RegisterClassExW(&wc))
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Register child (render surface) window class
    WNDCLASSEXW rc = {};
    rc.cbSize        = sizeof(rc);
    rc.style         = CS_HREDRAW | CS_VREDRAW;
    rc.lpfnWndProc   = RenderWindow::RenderWndProc;
    rc.hInstance      = hInst;
    rc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    rc.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
    rc.lpszClassName  = RENDER_CLASS;
    if (!RegisterClassExW(&rc))
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // Build the menu bar (View + Debug) BEFORE parent-window creation
    // so AdjustWindowRect can factor the menu height into the client
    // size. Menu handle attaches via the CreateWindowEx param.
    DisplayFlags df = service_.GetDisplayFlags();
    hMenuBar_      = CreateMenu();
    hMenuView_     = CreatePopupMenu();
    hMenuDebug_    = CreatePopupMenu();
    hMenuDebugVis_ = CreatePopupMenu();
    hMenuLod_      = CreatePopupMenu();

    auto addToggle = [](HMENU m, UINT id, const wchar_t* label, bool checked) {
        AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED), id, label);
    };
    addToggle(hMenuView_, IDM_VIEW_GRID,      L"Grid",      df.showGrid);
    addToggle(hMenuView_, IDM_VIEW_PARTICLES, L"Particles", df.showParticles);
    addToggle(hMenuView_, IDM_VIEW_RIBBONS,   L"Ribbons",   df.showRibbons);
    addToggle(hMenuView_, IDM_VIEW_EVENTS,    L"Event Objects", df.showEvents);
    AppendMenuW(hMenuView_, MF_SEPARATOR, 0, nullptr);

    // Tileset submenu (radio). Drives io::ReplaceableCanonicalPath /
    // CliffTypes.slk lookup — affects replaceable id 11. Item index =
    // id - IDM_TILESET_BASE casts straight to io::Tileset.
    hMenuTileset_ = CreatePopupMenu();
    {
        const int n = static_cast<int>(WhiteoutDex::io::Tileset::Count);
        for (int i = 0; i < n; ++i) {
            const char* nm = WhiteoutDex::io::TilesetName(
                static_cast<WhiteoutDex::io::Tileset>(i));
            wchar_t wbuf[64];
            MultiByteToWideChar(CP_UTF8, 0, nm, -1, wbuf, 64);
            AppendMenuW(hMenuTileset_, MF_STRING, IDM_TILESET_BASE + i, wbuf);
        }
        const int curIdx = static_cast<int>(service_.GetTileset());
        CheckMenuRadioItem(hMenuTileset_,
                           IDM_TILESET_BASE, IDM_TILESET_LAST,
                           IDM_TILESET_BASE + curIdx, MF_BYCOMMAND);
    }
    AppendMenuW(hMenuView_, MF_POPUP | MF_STRING,
                (UINT_PTR)hMenuTileset_, L"Tileset");

    addToggle(hMenuDebug_, IDM_DBG_COLLISIONS, L"Collision Markers", df.showCollisions);
    addToggle(hMenuDebug_, IDM_DBG_LIGHTS,     L"Light Markers",     df.showLights);
    AppendMenuW(hMenuDebug_, MF_SEPARATOR, 0, nullptr);

    static const wchar_t* const kDebugVisLabels[8] = {
        L"Off",
        L"Albedo",
        L"World Normal",
        L"LOD Heatmap",
        L"Light Count",
        L"Shading Only (white albedo)",
        L"Shading Only (grey albedo)",
        L"Specular Only (black albedo)",
    };
    for (int i = 0; i < 8; ++i)
        AppendMenuW(hMenuDebugVis_, MF_STRING, IDM_DBGVIS_BASE + i, kDebugVisLabels[i]);
    const int initDbg = service_.GetHdDebugMode();
    CheckMenuRadioItem(hMenuDebugVis_,
                       IDM_DBGVIS_BASE, IDM_DBGVIS_BASE + 7,
                       IDM_DBGVIS_BASE + (initDbg >= 0 && initDbg < 8 ? initDbg : 0),
                       MF_BYCOMMAND);
    AppendMenuW(hMenuDebug_, MF_POPUP | MF_STRING, (UINT_PTR)hMenuDebugVis_, L"Debug View");

    // LOD submenu: Auto + Force 0..3. Radio-checked. Default is
    // "Force LOD 0" — preview tooling always wants the highest-detail
    // mesh regardless of viewport size. "Auto (screen size)" mirrors
    // Previewd's screen-size-driven LOD when the user picks it.
    static const wchar_t* const kLodLabels[5] = {
        L"Auto (screen size)",
        L"Force LOD 0 (base)",
        L"Force LOD 1",
        L"Force LOD 2",
        L"Force LOD 3 (lowest)",
    };
    for (int i = 0; i < 5; ++i)
        AppendMenuW(hMenuLod_, MF_STRING, IDM_LOD_BASE + i, kLodLabels[i]);
    const int initLod = service_.GetLodOverride(); // -1 = auto, 0..3 = force
    const int lodCheckIdx = (initLod < 0) ? 0 : (1 + std::clamp(initLod, 0, 3));
    CheckMenuRadioItem(hMenuLod_,
                       IDM_LOD_BASE, IDM_LOD_LAST,
                       IDM_LOD_BASE + lodCheckIdx, MF_BYCOMMAND);
    AppendMenuW(hMenuDebug_, MF_POPUP | MF_STRING, (UINT_PTR)hMenuLod_, L"LOD");

    AppendMenuW(hMenuBar_, MF_POPUP | MF_STRING, (UINT_PTR)hMenuView_,  L"&View");
    AppendMenuW(hMenuBar_, MF_POPUP | MF_STRING, (UINT_PTR)hMenuDebug_, L"&Debug");
    // Top-level clickable entry — no submenu, fires WM_COMMAND with
    // IDM_SETTINGS straight from the menu bar.
    AppendMenuW(hMenuBar_, MF_STRING, IDM_SETTINGS, L"&Settings");

    // Create parent window (menu bar adds its own height — pass TRUE).
    RECT adj = {0, 0, w, h + kToolbarH};
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, TRUE);
    hwnd_ = CreateWindowExW(0, WINDOW_CLASS, WINDOW_TITLE, WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            adj.right - adj.left, adj.bottom - adj.top,
                            nullptr, hMenuBar_, hInst, this);
    if (!hwnd_) return false;

    // Child render surface (below toolbar).
    hwndRender_ = CreateWindowExW(0, RENDER_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                   0, kToolbarH, w, h,
                                   hwnd_, nullptr, hInst, this);
    if (!hwndRender_) return false;

    // Toolbar: only the live controls users adjust every frame.
    // --- Animation ---
    int x = 8;
    lblSequence_ = CreateWindowW(L"STATIC", L"Animation:",
        WS_CHILD | SS_CENTERIMAGE,
        x, 4, 64, 20, hwnd_, nullptr, hInst, nullptr);
    x += 66;
    cmbSequence_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, 2, 180, 300, hwnd_, (HMENU)(INT_PTR)IDC_SEQUENCE, hInst, nullptr);
    x += 188;

    // --- Camera ---
    CreateWindowW(L"STATIC", L"Camera:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        x, 4, 50, 20, hwnd_, nullptr, hInst, nullptr);
    x += 52;
    // Narrower than the Animation combo — most camera-preset names
    // fit in ~110 px and the dropdown still expands to the full text
    // width when opened.
    cmbCamera_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, 2, 110, 200, hwnd_, (HMENU)(INT_PTR)IDC_CAMERA, hInst, nullptr);
    SendMessageW(cmbCamera_, CB_ADDSTRING, 0, (LPARAM)L"Free Camera");
    SendMessageW(cmbCamera_, CB_SETCURSEL, 0, 0);
    x += 118;

    // --- Team color ---
    CreateWindowW(L"STATIC", L"Team:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        x, 4, 36, 20, hwnd_, nullptr, hInst, nullptr);
    x += 38;
    btnTeamColor_ = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, 4, 22, 20, hwnd_, (HMENU)(INT_PTR)IDC_TEAMCOLOR, hInst, nullptr);
    x += 30;

    // --- Lighting mode ---
    // Picks how the renderer's baseline headlight mixes with the model's
    // authored MDX lights. Index order matches the LightingMode enum
    // (InGame=0, Glue=1, Dynamic=2) so we can cast straight from CB_GETCURSEL.
    CreateWindowW(L"STATIC", L"Lighting:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        x, 4, 56, 20, hwnd_, nullptr, hInst, nullptr);
    x += 58;
    cmbLighting_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, 2, 100, 200, hwnd_, (HMENU)(INT_PTR)IDC_LIGHTING, hInst, nullptr);
    SendMessageW(cmbLighting_, CB_ADDSTRING, 0, (LPARAM)L"InGame");
    SendMessageW(cmbLighting_, CB_ADDSTRING, 0, (LPARAM)L"Glue");
    SendMessageW(cmbLighting_, CB_ADDSTRING, 0, (LPARAM)L"Dynamic");
    SendMessageW(cmbLighting_, CB_SETCURSEL,
                 static_cast<WPARAM>(service_.GetLightingMode()), 0);
    x += 108;

    // Background colour swatch + Exposure slider live in the Settings
    // popup now (see EnsureSettingsWindow). The toolbar carries only
    // controls the user manipulates frame-to-frame — Team / Camera /
    // Animation / Lighting.

    return true;
}

void RenderWindow::Destroy() {
    // Owned windows go away with the parent — null the cached HWND so a
    // re-open call to EnsureSettingsWindow rebuilds rather than handing
    // out a stale handle. The child controls (btnBgColor_/sldExposure_/
    // lblExposure_) destroy with their parent too; null them here.
    hwndSettings_ = nullptr;
    btnBgColor_   = nullptr;
    sldExposure_  = nullptr;
    lblExposure_  = nullptr;
    sldSndVolume_ = nullptr;
    lblSndVolume_ = nullptr;
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    hwndRender_ = nullptr;
    if (icon_) { DestroyIcon(icon_); icon_ = nullptr; }
    UnregisterClassW(WINDOW_CLASS,   GetModuleHandle(nullptr));
    UnregisterClassW(RENDER_CLASS,   GetModuleHandle(nullptr));
    UnregisterClassW(SETTINGS_CLASS, GetModuleHandle(nullptr));
}

void RenderWindow::Show() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
}

// ============================================================================
// Message Loop
// ============================================================================

bool RenderWindow::PumpMessages() {
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

// ============================================================================
// Win32 Callbacks
// ============================================================================

// Child (DX11 render surface) WndProc — forwards mouse input
LRESULT CALLBACK RenderWindow::RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RenderWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<RenderWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<RenderWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    // Forward mouse messages to parent WndProc logic
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_MOUSEMOVE:   case WM_MOUSEWHEEL:
        return RenderWindow::WndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK RenderWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RenderWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<RenderWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<RenderWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Settings popup WndProc. Same pointer-bootstrap pattern as the parent
// WndProc — `lpCreateParams` carries the owning RenderWindow and we
// stash it in GWLP_USERDATA so subsequent messages can route to the
// member handler.
LRESULT CALLBACK RenderWindow::SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RenderWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<RenderWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<RenderWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleSettingsMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT RenderWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        int renderH = h - kToolbarH;
        if (w > 0 && renderH > 0) {
            if (hwndRender_) MoveWindow(hwndRender_, 0, kToolbarH, w, renderH, TRUE);
            if (service_.IsDeviceReady()) service_.ResizePrimaryTarget(w, renderH);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        int vcHit = service_.Debug().HitTestViewCube(mx, my);
        if (vcHit >= 0) {
            if (vcHit == 6) service_.ResetCamera();
            else            service_.SnapCameraToFace(vcHit);
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
        auto vcr = service_.Debug().GetViewCubeRect();
        service_.Debug().SetViewCubeHovered(
            cur.x >= vcr.left && cur.x <= vcr.right &&
            cur.y >= vcr.top  && cur.y <= vcr.bottom);
        if (!service_.Scene().CameraLocked()) {
            if (lmbDown_) service_.RotateCamera(dx, dy);
            if (rmbDown_) service_.PanCamera(-dx, dy);
            if (mmbDown_) service_.ZoomCameraSmooth(dy);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!service_.Scene().CameraLocked()) {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            service_.ZoomCamera(delta * 30);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        return 0;
    }
    case WM_DRAWITEM: {
        // Only the toolbar's Team-colour button still owner-draws here
        // — the Background swatch lives on the Settings popup and its
        // WM_DRAWITEM is handled in SettingsWndProc.
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_TEAMCOLOR) {
            COLORREF tc = service_.Replaceables().GetTeamColorRaw();
            HBRUSH brush = CreateSolidBrush(tc);
            FillRect(dis->hDC, &dis->rcItem, brush);
            DeleteObject(brush);
            DrawEdge(dis->hDC, &dis->rcItem, EDGE_SUNKEN, BF_RECT);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // Menu toggles — flip the stored state, push to service. Menu
        // checkmark is toggled via CheckMenuItem on the current state.
        // View-menu toggles persist to the INI; Debug-menu toggles
        // ride along (SaveSettingsIni only writes the keys it knows
        // about, so showCollisions / showLights aren't persisted —
        // matches the prior behaviour).
        auto toggleView = [&](UINT menuId, bool DisplayFlags::*field) {
            DisplayFlags df = service_.GetDisplayFlags();
            bool& v = df.*field;
            v = !v;
            CheckMenuItem(hMenuBar_, menuId, MF_BYCOMMAND | (v ? MF_CHECKED : MF_UNCHECKED));
            service_.SetDisplayFlags(df);
            SaveSettingsIni(service_);
        };
        // Top-level &Settings menu entry — open the lazily-created popup
        // and bring it forward.
        if (id == IDM_SETTINGS) {
            EnsureSettingsWindow();
            if (hwndSettings_) {
                ShowWindow(hwndSettings_, SW_SHOW);
                SetForegroundWindow(hwndSettings_);
            }
            return 0;
        }
        if (id == IDM_VIEW_GRID)        { toggleView(id, &DisplayFlags::showGrid);       return 0; }
        if (id == IDM_VIEW_PARTICLES)   { toggleView(id, &DisplayFlags::showParticles);  return 0; }
        if (id == IDM_VIEW_RIBBONS)     { toggleView(id, &DisplayFlags::showRibbons);    return 0; }
        if (id == IDM_VIEW_EVENTS)      { toggleView(id, &DisplayFlags::showEvents);     return 0; }
        if (id == IDM_DBG_COLLISIONS)   { toggleView(id, &DisplayFlags::showCollisions); return 0; }
        if (id == IDM_DBG_LIGHTS)       { toggleView(id, &DisplayFlags::showLights);     return 0; }

        // Tileset submenu (radio). idx casts straight to io::Tileset.
        if (id >= (int)IDM_TILESET_BASE && id <= (int)IDM_TILESET_LAST) {
            const int idx = id - IDM_TILESET_BASE;
            const int n   = static_cast<int>(WhiteoutDex::io::Tileset::Count);
            if (idx < 0 || idx >= n) return 0;
            CheckMenuRadioItem(hMenuTileset_, IDM_TILESET_BASE, IDM_TILESET_LAST,
                               id, MF_BYCOMMAND);
            service_.SetTileset(static_cast<WhiteoutDex::io::Tileset>(idx));
            SaveSettingsIni(service_);
            return 0;
        }

        // Debug-vis submenu (radio). id → mode passed to the service.
        if (id >= (int)IDM_DBGVIS_BASE && id <= (int)IDM_DBGVIS_LAST) {
            const int mode = id - IDM_DBGVIS_BASE;
            CheckMenuRadioItem(hMenuDebugVis_, IDM_DBGVIS_BASE, IDM_DBGVIS_LAST, id, MF_BYCOMMAND);
            service_.SetHdDebugMode(mode);
            return 0;
        }

        // LOD submenu (radio). idx 0 = Auto (-1), idx 1..4 = force LOD 0..3.
        if (id >= (int)IDM_LOD_BASE && id <= (int)IDM_LOD_LAST) {
            const int idx = id - IDM_LOD_BASE;
            CheckMenuRadioItem(hMenuLod_, IDM_LOD_BASE, IDM_LOD_LAST, id, MF_BYCOMMAND);
            service_.SetLodOverride(idx == 0 ? -1 : (idx - 1));
            return 0;
        }

        switch (id) {
            case IDC_TEAMCOLOR: {
                CHOOSECOLORW cc = {};
                static COLORREF customColors[16] = {};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd_;
                cc.rgbResult = service_.Replaceables().GetTeamColorRaw();
                cc.lpCustColors = customColors;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    service_.SetTeamColor(
                        GetRValue(cc.rgbResult),
                        GetGValue(cc.rgbResult),
                        GetBValue(cc.rgbResult));
                    InvalidateRect(btnTeamColor_, nullptr, TRUE);
                }
                break;
            }
            case IDC_CAMERA: {
                if (code == CBN_SELCHANGE) {
                    int sel = (int)SendMessageW(cmbCamera_, CB_GETCURSEL, 0, 0);
                    if (sel == 0) {
                        service_.ActivateCameraPreset(-1);  // free camera
                        service_.Scene().SetCameraLocked(false);
                    } else {
                        int idx = sel - 1;
                        if (idx >= 0 && idx < (int)cameraPresets_.size()) {
                            service_.ActivateCameraPreset(idx);
                            service_.Scene().SetCameraLocked(cameraPresets_[idx].isLive);
                        }
                    }
                }
                break;
            }
            case IDC_SEQUENCE: {
                if (code == CBN_SELCHANGE) {
                    const int sel = (int)SendMessageW(cmbSequence_, CB_GETCURSEL, 0, 0);
                    if (sel < 0) break;
                    const int prev = service_.GetActiveSequenceIndex();
                    service_.SetActiveSequence(sel);
                    if (sel == prev) break;  // re-pick of same row → don't disturb live splats

                    // App policy: wipe ground decals on every sequence
                    // change EXCEPT when the new pose is a residue/cleanup
                    // pose (Decay-Bone / Decay-Flesh / Dissipate are
                    // authored as the visual continuation of the prior
                    // Death pose, so its blood splats and burn marks must
                    // persist into them). Case-insensitive substring match.
                    auto containsCi = [](const std::string& hay, const char* needle) {
                        const size_t hn = hay.size(), nn = std::strlen(needle);
                        if (nn == 0 || hn < nn) return false;
                        for (size_t i = 0; i + nn <= hn; ++i) {
                            bool ok = true;
                            for (size_t j = 0; j < nn; ++j) {
                                const char a = (char)std::tolower((unsigned char)hay[i + j]);
                                const char b = (char)std::tolower((unsigned char)needle[j]);
                                if (a != b) { ok = false; break; }
                            }
                            if (ok) return true;
                        }
                        return false;
                    };
                    bool keep = false;
                    if (sel < (int)sequenceNames_.size()) {
                        const std::string& name = sequenceNames_[sel];
                        keep = containsCi(name, "decay") || containsCi(name, "dissipate");
                    }
                    if (!keep) service_.ClearSplats();
                }
                break;
            }
            case IDC_LIGHTING: {
                if (code == CBN_SELCHANGE) {
                    int sel = (int)SendMessageW(cmbLighting_, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel <= 2)
                        service_.SetLightingMode(static_cast<LightingMode>(sel));
                }
                break;
            }
            // IDC_BGCOLOR / IDC_EXPOSURE are wired by SettingsWndProc —
            // their controls are children of hwndSettings_, so messages
            // never reach the parent's HandleMessage.
        }
        return 0;
    }
    case WM_DESTROY:
        hwnd_ = nullptr;
        running_ = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Camera Presets / Sequences — consume pending data from Renderer
// ============================================================================

void RenderWindow::ProcessCameraPresets() {
    auto pending = service_.TakePendingCameraPresets();
    if (!pending || !cmbCamera_) return;
    SendMessageW(cmbCamera_, CB_RESETCONTENT, 0, 0);
    SendMessageW(cmbCamera_, CB_ADDSTRING, 0, (LPARAM)L"Free Camera");
    for (auto& p : *pending)
        SendMessageW(cmbCamera_, CB_ADDSTRING, 0, (LPARAM)p.name.c_str());
    cameraPresets_ = std::move(*pending);
    SendMessageW(cmbCamera_, CB_SETCURSEL, 0, 0);
    service_.Scene().SetCameraLocked(false);
}

void RenderWindow::ProcessSequences() {
    auto pending = service_.TakePendingSequences();
    if (!pending || !cmbSequence_) return;
    SendMessageW(cmbSequence_, CB_RESETCONTENT, 0, 0);
    for (auto& n : *pending) {
        std::wstring wn(n.begin(), n.end());
        SendMessageW(cmbSequence_, CB_ADDSTRING, 0, (LPARAM)wn.c_str());
    }
    sequenceNames_ = std::move(*pending);
    if (!sequenceNames_.empty()) {
        ShowWindow(lblSequence_, SW_SHOW);
        ShowWindow(cmbSequence_, SW_SHOW);
        SendMessageW(cmbSequence_, CB_SETCURSEL, 0, 0);
        service_.SetActiveSequence(0);
    }
}

// ============================================================================
// Utility
// ============================================================================

void RenderWindow::SetTitle(const wchar_t* title) {
    if (hwnd_) SetWindowTextW(hwnd_, title);
}

void RenderWindow::InvalidateTeamColorSwatch() {
    if (btnTeamColor_) InvalidateRect(btnTeamColor_, nullptr, TRUE);
}

int RenderWindow::GetActiveCameraIndex() const {
    return cmbCamera_ ? (int)SendMessageW(cmbCamera_, CB_GETCURSEL, 0, 0) : 0;
}

void RenderWindow::SyncViewMenuFromService() {
    if (!hMenuView_) return;
    const DisplayFlags df = service_.GetDisplayFlags();
    auto syncToggle = [&](UINT id, bool checked) {
        CheckMenuItem(hMenuBar_, id,
                      MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    };
    syncToggle(IDM_VIEW_GRID,      df.showGrid);
    syncToggle(IDM_VIEW_PARTICLES, df.showParticles);
    syncToggle(IDM_VIEW_POPCORN,   df.showPopcorn);
    syncToggle(IDM_VIEW_RIBBONS,   df.showRibbons);
    syncToggle(IDM_VIEW_EVENTS,    df.showEvents);
    if (hMenuTileset_) {
        const int n      = static_cast<int>(WhiteoutDex::io::Tileset::Count);
        const int curIdx = std::clamp(static_cast<int>(service_.GetTileset()), 0, n - 1);
        CheckMenuRadioItem(hMenuTileset_,
                           IDM_TILESET_BASE, IDM_TILESET_LAST,
                           IDM_TILESET_BASE + curIdx, MF_BYCOMMAND);
    }
    // Repaint the menu bar so any visible top-level checkmarks update.
    // Safe cross-thread: DrawMenuBar just posts WM_NCPAINT to the
    // window's owning thread.
    if (hwnd_) DrawMenuBar(hwnd_);
}

// ============================================================================
// Settings popup
// ============================================================================
//
// Lazily build a small modeless tool window the first time the &Settings
// menu entry fires. Subsequent opens just ShowWindow the cached HWND.
// Layout is fixed-pixel (no resize) — Background swatch on top, Exposure
// slider below — to mirror what was on the toolbar.

void RenderWindow::EnsureSettingsWindow() {
    if (hwndSettings_) return;

    HMODULE hMod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCWSTR)&RenderWindow::SettingsWndProc, &hMod);
    HINSTANCE hInst = hMod ? (HINSTANCE)hMod : GetModuleHandle(nullptr);

    // Register the popup's window class. Reusing the parent's class would
    // route messages to the parent's WndProc — we want a separate proc so
    // its WM_DRAWITEM / WM_HSCROLL / WM_COMMAND can stay scoped to the
    // popup's controls.
    WNDCLASSEXW sc = {};
    sc.cbSize        = sizeof(sc);
    sc.style         = CS_HREDRAW | CS_VREDRAW;
    sc.lpfnWndProc   = RenderWindow::SettingsWndProc;
    sc.hInstance     = hInst;
    sc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    sc.hIcon         = icon_;
    sc.hIconSm       = icon_;
    sc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    sc.lpszClassName = SETTINGS_CLASS;
    if (!RegisterClassExW(&sc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return;

    // Fixed client size — tall enough for nine rows of label+control
    // (Background colour, Exposure, SND Volume, Loop NonLooping,
    //  Time of Day, Animate TOD, IBL Mode, Shadows, DNC Model). The
    // DNC row is wider because the edit field needs room for full
    // Environment/DNC/... paths (~80 chars worst case).
    constexpr int kClientW = 380;
    constexpr int kClientH = 388;
    RECT rc = {0, 0, kClientW, kClientH};
    // WS_POPUPWINDOW gives us a thin frame + close box without resize
    // grippers; WS_CAPTION puts a title bar on top.
    const DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_TOOLWINDOW;  // small caption, omit from taskbar
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    // Spawn near the parent's top-left corner, just below the menu.
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (hwnd_) {
        RECT pr{};
        GetWindowRect(hwnd_, &pr);
        x = pr.left + 60;
        y = pr.top  + 60;
    }

    hwndSettings_ = CreateWindowExW(exStyle, SETTINGS_CLASS, L"Settings",
                                    style, x, y,
                                    rc.right - rc.left, rc.bottom - rc.top,
                                    hwnd_, nullptr, hInst, this);
    if (!hwndSettings_) return;

    // --- Row 1: Background colour ---
    int rowY = 12;
    CreateWindowW(L"STATIC", L"Background:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        12, rowY, 90, 22, hwndSettings_, nullptr, hInst, nullptr);
    btnBgColor_ = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        108, rowY + 1, 28, 20, hwndSettings_,
        (HMENU)(INT_PTR)IDC_BGCOLOR, hInst, nullptr);

    // --- Row 2: Exposure ---
    rowY += 38;
    CreateWindowW(L"STATIC", L"Exposure:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        12, rowY, 90, 22, hwndSettings_, nullptr, hInst, nullptr);
    sldExposure_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        108, rowY, 150, 24, hwndSettings_,
        (HMENU)(INT_PTR)IDC_EXPOSURE, hInst, nullptr);
    SendMessageW(sldExposure_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 300));
    SendMessageW(sldExposure_, TBM_SETPOS,   TRUE,
                 (LPARAM)(int)(service_.GetTonemapExposure() * 100.0f));
    {
        wchar_t buf[16];
        swprintf_s(buf, L"%.2f", service_.GetTonemapExposure());
        lblExposure_ = CreateWindowW(L"STATIC", buf,
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            264, rowY, 44, 22, hwndSettings_, nullptr, hInst, nullptr);
    }

    // --- Row 3: SND EventObject volume ---
    // Drives RenderService::SetSoundVolume → ISoundEmitter::SetVolume
    // (WindowsSoundEmitter pre-scales PCM samples). Range 0..100 →
    // 0.00..1.00 so the trackbar steps in 0.01.
    rowY += 38;
    CreateWindowW(L"STATIC", L"SND Volume:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        12, rowY, 90, 22, hwndSettings_, nullptr, hInst, nullptr);
    sldSndVolume_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        108, rowY, 150, 24, hwndSettings_,
        (HMENU)(INT_PTR)IDC_SND_VOLUME, hInst, nullptr);
    SendMessageW(sldSndVolume_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(sldSndVolume_, TBM_SETPOS,   TRUE,
                 (LPARAM)(int)(service_.GetSoundVolume() * 100.0f));
    {
        wchar_t buf[16];
        swprintf_s(buf, L"%.2f", service_.GetSoundVolume());
        lblSndVolume_ = CreateWindowW(L"STATIC", buf,
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            264, rowY, 44, 22, hwndSettings_, nullptr, hInst, nullptr);
    }

    // --- Row 4: Loop NonLooping animations ---
    // Drives RenderService::SetIgnoreNonLooping. When checked, every
    // top-level actor's `ignoreNonLooping` flag flips on, so MDX
    // sequences with the SEQS NonLooping bit (Death / Decay / climax
    // poses) wrap back to startMs instead of holding their last frame
    // — handy for animation editors that want to see the clip cycle.
    rowY += 38;
    chkLoopNonLoop_ = CreateWindowW(L"BUTTON",
        L"Loop NonLooping animations",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        12, rowY, 280, 22, hwndSettings_,
        (HMENU)(INT_PTR)IDC_LOOP_NONLOOP, hInst, nullptr);
    SendMessageW(chkLoopNonLoop_, BM_SETCHECK,
                 service_.GetIgnoreNonLooping() ? BST_CHECKED : BST_UNCHECKED, 0);

    // --- Row 5: Time of Day ---
    // Drives DncService::SetTimeOfDay. Slider range 0..2400 → 0..24h
    // in 0.01h steps. The label reads HH:MM. Lighting only updates
    // when LightingMode == InGame and a DNC MDL is bound — the
    // service falls back to the legacy hardcoded baseline otherwise,
    // so the slider stays visible but its effect is gated.
    rowY += 38;
    CreateWindowW(L"STATIC", L"Time of Day:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        12, rowY, 90, 22, hwndSettings_, nullptr, hInst, nullptr);
    sldTimeOfDay_ = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        108, rowY, 150, 24, hwndSettings_,
        (HMENU)(INT_PTR)IDC_TIME_OF_DAY, hInst, nullptr);
    {
        const float hpd = service_.GetDncService()
                              ? service_.GetDncService()->GetHoursPerDay()
                              : 24.0f;
        const int rangeHi = static_cast<int>(hpd * 100.0f);
        SendMessageW(sldTimeOfDay_, TBM_SETRANGE, TRUE, MAKELPARAM(0, rangeHi));
        const float tod = service_.GetDncService()
                              ? service_.GetDncService()->GetTimeOfDay()
                              : 12.0f;
        SendMessageW(sldTimeOfDay_, TBM_SETPOS, TRUE,
                     (LPARAM)(int)(tod * 100.0f));
        wchar_t buf[16];
        const int hh = static_cast<int>(tod) % 24;
        const int mm = static_cast<int>((tod - std::floor(tod)) * 60.0f) % 60;
        swprintf_s(buf, L"%02d:%02d", hh, mm);
        lblTimeOfDay_ = CreateWindowW(L"STATIC", buf,
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            264, rowY, 44, 22, hwndSettings_, nullptr, hInst, nullptr);
    }

    // --- Row 6: Animate TOD ---
    // Toggles DncService::SetTodScale between 0 (paused — slider in
    // direct control) and 1 (real-time — TOD drifts at 1× across the
    // configured day length). Default off for the model viewer
    // (we're an interactive tool, not a wall-clock simulation).
    rowY += 38;
    chkAnimateTod_ = CreateWindowW(L"BUTTON",
        L"Animate TOD",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        12, rowY, 280, 22, hwndSettings_,
        (HMENU)(INT_PTR)IDC_ANIMATE_TOD, hInst, nullptr);
    {
        const bool animating = service_.GetDncService()
                                  && service_.GetDncService()->GetTodScale() > 0.0f;
        SendMessageW(chkAnimateTod_, BM_SETCHECK,
                     animating ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    // --- Row 7: HD IBL probe selection ---
    // Picks between the engine's authored Day/Night pair (TOD-blended,
    // engine-faithful colour shift) and the Portrait probe (single,
    // horizontally isotropic, no concavity blotches). See
    // render_target.h::IblMode for the rationale. Index order matches
    // the enum so we can cast straight from CB_GETCURSEL.
    rowY += 38;
    CreateWindowW(L"STATIC", L"IBL:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        12, rowY, 90, 22, hwndSettings_, nullptr, hInst, nullptr);
    cmbIblMode_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        108, rowY, 200, 200, hwndSettings_,
        (HMENU)(INT_PTR)IDC_IBL_MODE, hInst, nullptr);
    SendMessageW(cmbIblMode_, CB_ADDSTRING, 0, (LPARAM)L"Portrait");
    SendMessageW(cmbIblMode_, CB_ADDSTRING, 0, (LPARAM)L"Day/Night");
    SendMessageW(cmbIblMode_, CB_ADDSTRING, 0, (LPARAM)L"Dungeon");
    SendMessageW(cmbIblMode_, CB_ADDSTRING, 0, (LPARAM)L"Sunset");
    SendMessageW(cmbIblMode_, CB_SETCURSEL,
                 static_cast<WPARAM>(service_.GetIblMode()), 0);

    // --- Row 8: Shadow cascades ---
    // 0 cascades = service disabled (HAS_SHADOWS perm off, depth maps
    // not allocated). 1-3 = number of active cascades. v1 ships with
    // depth maps cleared but no draw walk — see shadow_pass.cpp's
    // TODO. Toggle here exercises the shader-side perm + CB upload.
    rowY += 38;
    CreateWindowW(L"STATIC", L"Shadows:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        12, rowY, 90, 22, hwndSettings_, nullptr, hInst, nullptr);
    cmbShadows_ = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        108, rowY, 200, 200, hwndSettings_,
        (HMENU)(INT_PTR)IDC_SHADOWS, hInst, nullptr);
    SendMessageW(cmbShadows_, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendMessageW(cmbShadows_, CB_ADDSTRING, 0, (LPARAM)L"1 cascade");
    SendMessageW(cmbShadows_, CB_ADDSTRING, 0, (LPARAM)L"2 cascades");
    SendMessageW(cmbShadows_, CB_ADDSTRING, 0, (LPARAM)L"3 cascades");
    {
        int sel = 0;
        if (auto* shadow = service_.GetShadowService()) {
            sel = shadow->IsEnabled() ? shadow->Params().cascadeCount : 0;
            if (sel < 0) sel = 0; else if (sel > 3) sel = 3;
        }
        SendMessageW(cmbShadows_, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
    }

    // --- Row 9: DNC Model override ---
    // Engine parity with JASS native SetDayNightModels(_, units).
    // Edit field accepts any CASC path (forward-slash form); Reset
    // reverts to dnc::DncService::kDefaultUnitMdl. Path applies on
    // Reset click or when the user presses Enter in the edit field
    // (WM_COMMAND: EN_CHANGE would re-load on every keystroke — too
    // aggressive; we wait for an explicit commit).
    rowY += 38;
    CreateWindowW(L"STATIC", L"DNC Model:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        12, rowY, 90, 22, hwndSettings_, nullptr, hInst, nullptr);
    editDncPath_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        108, rowY + 1, 200, 20, hwndSettings_,
        (HMENU)(INT_PTR)IDC_DNC_PATH, hInst, nullptr);
    btnDncReset_ = CreateWindowW(L"BUTTON", L"Reset",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        314, rowY, 54, 22, hwndSettings_,
        (HMENU)(INT_PTR)IDC_DNC_RESET, hInst, nullptr);
    if (auto* dnc = service_.GetDncService()) {
        const std::string& path = dnc->UnitMdlPath();
        // Widen for SetWindowTextW. ANSI→UTF-16 via MultiByteToWideChar
        // since paths may carry non-ASCII characters in custom assets.
        const int wlen = ::MultiByteToWideChar(CP_UTF8, 0,
                                               path.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wpath(wlen, L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1,
                                  wpath.data(), wlen);
            // wlen includes the null terminator; SetWindowTextW expects
            // a null-terminated string but the std::wstring's null is
            // outside its size, so a `c_str()` works.
            SetWindowTextW(editDncPath_, wpath.c_str());
        }
    }
}

LRESULT RenderWindow::HandleSettingsMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
        // Hide instead of destroy so the next open is instant and the
        // controls' transient state (focus, slider drag, etc.) survives.
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_BGCOLOR) {
            COLORREF bc = service_.GetBackgroundColorRaw();
            HBRUSH brush = CreateSolidBrush(bc);
            FillRect(dis->hDC, &dis->rcItem, brush);
            DeleteObject(brush);
            DrawEdge(dis->hDC, &dis->rcItem, EDGE_SUNKEN, BF_RECT);
        }
        return TRUE;
    }

    case WM_HSCROLL: {
        // Trackbar drag / keypress messages — dispatched per slider.
        const HWND src = (HWND)lParam;
        if (src == sldExposure_ && sldExposure_) {
            int pos = (int)SendMessageW(sldExposure_, TBM_GETPOS, 0, 0);
            float exposure = (float)pos / 100.0f;
            service_.SetTonemapExposure(exposure);
            if (lblExposure_) {
                wchar_t buf[16];
                swprintf_s(buf, L"%.2f", exposure);
                SetWindowTextW(lblExposure_, buf);
            }
            // Persist on every drag tick. INI writes are cheap enough
            // that debouncing isn't worth the complexity for a small
            // file; if profiling ever shows it, gate on TB_ENDTRACK.
            SaveSettingsIni(service_);
            return 0;
        }
        if (src == sldSndVolume_ && sldSndVolume_) {
            int pos = (int)SendMessageW(sldSndVolume_, TBM_GETPOS, 0, 0);
            float volume = (float)pos / 100.0f;
            service_.SetSoundVolume(volume);
            if (lblSndVolume_) {
                wchar_t buf[16];
                swprintf_s(buf, L"%.2f", volume);
                SetWindowTextW(lblSndVolume_, buf);
            }
            SaveSettingsIni(service_);
            return 0;
        }
        if (src == sldTimeOfDay_ && sldTimeOfDay_) {
            const int pos = (int)SendMessageW(sldTimeOfDay_, TBM_GETPOS, 0, 0);
            const float tod = (float)pos / 100.0f;
            if (auto* dnc = service_.GetDncService()) dnc->SetTimeOfDay(tod);
            if (lblTimeOfDay_) {
                wchar_t buf[16];
                const int hh = static_cast<int>(tod) % 24;
                const int mm = static_cast<int>((tod - std::floor(tod)) * 60.0f) % 60;
                swprintf_s(buf, L"%02d:%02d", hh, mm);
                SetWindowTextW(lblTimeOfDay_, buf);
            }
            SaveSettingsIni(service_);
            return 0;
        }
        break;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == IDC_BGCOLOR) {
            CHOOSECOLORW cc = {};
            static COLORREF customColors[16] = {};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.rgbResult    = service_.GetBackgroundColorRaw();
            cc.lpCustColors = customColors;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                service_.SetBackgroundColor(
                    GetRValue(cc.rgbResult),
                    GetGValue(cc.rgbResult),
                    GetBValue(cc.rgbResult));
                if (btnBgColor_) InvalidateRect(btnBgColor_, nullptr, TRUE);
                SaveSettingsIni(service_);
            }
            return 0;
        }
        if (id == IDC_LOOP_NONLOOP) {
            const bool on = chkLoopNonLoop_
                && SendMessageW(chkLoopNonLoop_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            service_.SetIgnoreNonLooping(on);
            SaveSettingsIni(service_);
            return 0;
        }
        if (id == IDC_ANIMATE_TOD) {
            const bool on = chkAnimateTod_
                && SendMessageW(chkAnimateTod_, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (auto* dnc = service_.GetDncService()) dnc->SetTodScale(on ? 1.0f : 0.0f);
            SaveSettingsIni(service_);
            return 0;
        }
        if (id == IDC_IBL_MODE && HIWORD(wParam) == CBN_SELCHANGE && cmbIblMode_) {
            const int sel = (int)SendMessageW(cmbIblMode_, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel <= static_cast<int>(IblMode::Sunset)) {
                service_.SetIblMode(static_cast<IblMode>(sel));
                SaveSettingsIni(service_);
            }
            return 0;
        }
        if (id == IDC_SHADOWS && HIWORD(wParam) == CBN_SELCHANGE && cmbShadows_) {
            const int sel = (int)SendMessageW(cmbShadows_, CB_GETCURSEL, 0, 0);
            if (auto* shadow = service_.GetShadowService(); shadow && sel >= 0 && sel <= 3) {
                shadow::ShadowParams p = shadow->Params();
                p.enabled       = (sel > 0);
                p.cascadeCount  = (sel > 0) ? sel : 1;
                shadow->SetParams(p);
                SaveSettingsIni(service_);
            }
            return 0;
        }
        if (id == IDC_DNC_RESET && btnDncReset_) {
            // Revert to the engine's default Lordaeron Unit MDL.
            // Updates the edit field so the user sees the new path.
            if (auto* dnc = service_.GetDncService()) {
                dnc->SetUnitMdl(dnc::DncService::kDefaultUnitMdl);
                if (editDncPath_) {
                    SetWindowTextA(editDncPath_, dnc::DncService::kDefaultUnitMdl);
                }
                SaveSettingsIni(service_);
            }
            return 0;
        }
        if (id == IDC_DNC_PATH && HIWORD(wParam) == EN_KILLFOCUS && editDncPath_) {
            // Commit on focus-loss (Enter on a single-line edit also
            // sends EN_KILLFOCUS via the dialog manager, modulo our
            // popup not being a dialog — but Enter focus-shift is fine
            // for power users). Pull the text and apply.
            wchar_t buf[512] = {};
            const int n = GetWindowTextW(editDncPath_, buf, 512);
            if (n >= 0) {
                const int u8len = ::WideCharToMultiByte(CP_UTF8, 0, buf, n,
                                                        nullptr, 0, nullptr, nullptr);
                std::string utf8(u8len, '\0');
                if (u8len > 0) {
                    ::WideCharToMultiByte(CP_UTF8, 0, buf, n,
                                          utf8.data(), u8len, nullptr, nullptr);
                }
                if (auto* dnc = service_.GetDncService()) {
                    dnc->SetUnitMdl(utf8);
                    SaveSettingsIni(service_);
                }
            }
            return 0;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace WhiteoutDex
