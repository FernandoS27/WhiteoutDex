#include "asset_picker_window.h"

// max.h first: it is the header with opinions about windows.h, and the picker
// needs GetCOREInterface() to find Max's top-level HWND to be modal against.
#include <max.h>

#include "imgui_theme.h"
#include "renderer/render_pipeline.h"
#include "renderer/render_service.h"
#include "renderer/scene_manager.h"
#include "resource.h" // IDI_WHITEOUTDEX_ICON
#include "storage_explorer.h" // WhiteoutFlakesExplorerLib

#include <chrono>
#include <filesystem>
#include <memory>

#include <imgui.h>
#include <imgui_impl_win32.h>

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on
#pragma comment(lib, "dwmapi.lib")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

// imgui_impl_win32.h asks callers to forward-declare this.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace whiteout::flakes {

namespace {

constexpr const wchar_t* kPickerClass = L"WhiteoutDexAssetPickerClass";
constexpr int kDefaultWidth = 1100;
constexpr int kDefaultHeight = 760;
// Everything the window procedure needs. Lives on RunAssetPicker's stack and
// is hung off the HWND's GWLP_USERDATA.
struct PickerState {
    bool done = false;
    bool imguiReady = false;
    // This picker's own ImGui context. Held rather than assumed-current:
    // ImGui::CreateContext restores whatever was current before it, so on
    // return from CreateContext the preview's context may well still be the
    // selected one. Every entry point below selects this one first.
    ImGuiContext* ctx = nullptr;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    bool resized = false;
};

LRESULT CALLBACK PickerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<PickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (st && st->imguiReady && st->ctx) {
        ImGui::SetCurrentContext(st->ctx);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
            return 1;
    }

    switch (msg) {
    case WM_CLOSE:
        // Never DestroyWindow from here: the frame loop owns teardown order
        // (explorer → device → ImGui → window) and destroying under it would
        // leave the pipeline presenting to a dead HWND.
        if (st)
            st->done = true;
        return 0;
    case WM_SIZE:
        if (st && wp != SIZE_MINIMIZED) {
            st->width = LOWORD(lp);
            st->height = HIWORD(lp);
            st->resized = true;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The path a resource stores: the archive path with the Warcraft III mod chain
// dropped, so "war3.w3mod:_hd.w3mod:units\...\druid.mdx" becomes
// "units\...\druid.mdx". Same rule as io::ToListingPath, but keeping MDX's
// backslashes rather than switching to the listing form's forward slashes.
std::string StripModChain(std::string p) {
    const auto colon = p.rfind(':');
    if (colon != std::string::npos)
        p.erase(0, colon + 1);
    for (char& c : p)
        if (c == '/')
            c = '\\';
    while (!p.empty() && p.front() == '\\')
        p.erase(0, 1);
    return p;
}

std::string ParentFolder(const std::string& displayPath) {
    const auto sep = displayPath.find_last_of("\\/");
    return sep == std::string::npos ? std::string() : displayPath.substr(0, sep);
}

// Directory holding this .dlx — where the installer drops `shaders/`, which
// RenderPipeline::InitDevice loads the BLS bundles from.
std::filesystem::path PluginDirectory() {
    wchar_t buf[MAX_PATH] = {};
    if (GetModuleFileNameW(WdxPluginInstance(), buf, MAX_PATH) > 0)
        return std::filesystem::path(buf).parent_path();
    return {};
}

} // namespace

AssetPickResult RunAssetPicker(const std::wstring& title, io::BrowseType types,
                               const std::string& cascRoot, const std::string& initialRel,
                               const std::string& initialFilter,
                               const std::vector<AssetPickerRoot>& roots,
                               const std::vector<std::string>& archives) {
    AssetPickResult out;

    // Park the preview for the duration, if it is up: two ImGui contexts may
    // coexist, but only one may be current, and the preview drives its frames
    // from another thread. Resumes when this goes out of scope, on every exit
    // path below.
    WdxPreviewPause previewPause;

    if (cascRoot.empty()) {
        out.error = "No Warcraft III install configured. Set the CASC path in WhiteoutDex "
                    "Settings first.";
        return out;
    }

    // What the empty-selection hint calls the thing being picked. Derived from
    // the type mask rather than the caption so it stays right when a caller
    // passes a title of its own.
    const char* noun = "model";
    if (types == io::BrowseType::Effects)
        noun = "particle effect";
    else if (types == io::BrowseType::Textures)
        noun = "texture";

    Interface* ip = GetCOREInterface();
    HWND owner = ip ? GetAncestor(ip->GetMAXHWnd(), GA_ROOT) : nullptr;

    PickerState st;
    HINSTANCE hInst = WdxPluginInstance();

    // ---- Window ------------------------------------------------------------
    HICON icon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_WHITEOUTDEX_ICON));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PickerWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kPickerClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        out.error = "Could not register the picker window class.";
        return out;
    }

    RECT adj = {0, 0, kDefaultWidth, kDefaultHeight};
    AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, kPickerClass, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, adj.right - adj.left, adj.bottom - adj.top, owner,
                                nullptr, hInst, &st);
    if (!hwnd) {
        out.error = "Could not create the picker window.";
        return out;
    }
    // The class carries the icon, but only the registration that created it
    // does - a second open finds the class already there. Set it per window so
    // the title bar is right either way.
    if (icon) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    }
    {
        const BOOL useDark = TRUE;
        const COLORREF chrome = RGB(38, 45, 56);
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &chrome, sizeof(chrome));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &chrome, sizeof(chrome));
    }

    // Modal: the picker owns the interaction until it returns, and Max's main
    // window would otherwise happily let the user delete the node we are about
    // to write a path onto.
    if (owner)
        EnableWindow(owner, FALSE);

    auto teardownWindow = [&]() {
        if (owner) {
            EnableWindow(owner, TRUE);
            SetActiveWindow(owner);
        }
        DestroyWindow(hwnd);
    };

    // ---- ImGui -------------------------------------------------------------
    IMGUI_CHECKVERSION();
    st.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(st.ctx);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // No imgui.ini: the panel is transient and a stale saved layout would pin
    // its window off-screen the next time the picker opens at another size.
    ImGui::GetIO().IniFilename = nullptr;
    ApplyImGuiTheme();
    {
        UINT dpi = 96;
        using GetDpiFn = UINT(WINAPI*)(HWND);
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        auto getDpi =
            u32 ? reinterpret_cast<GetDpiFn>(GetProcAddress(u32, "GetDpiForWindow")) : nullptr;
        if (getDpi)
            dpi = getDpi(hwnd);
        ApplyImGuiDpiScale(static_cast<float>(dpi) / 96.0f);
    }
    ImGui_ImplWin32_Init(hwnd);
    st.imguiReady = true;

    auto shutdownImGui = [&]() {
        st.imguiReady = false;
        ImGui::SetCurrentContext(st.ctx);
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(st.ctx);
        st.ctx = nullptr;
    };

    // ---- Renderer ----------------------------------------------------------
    // A scene + service of the picker's own. The thumbnail pool spawns one
    // scene per visible cell inside this service, so it must not be the
    // preview's — which is also why the two windows are mutually exclusive.
    auto scene = std::make_unique<renderer::SceneManager>();
    auto svc = std::make_unique<renderer::RenderService>(*scene);
    svc->Settings().SetDefaultBackend(gfx::GfxApi::D3D11);
    if (const auto dir = PluginDirectory(); !dir.empty())
        scene->GetContentProvider().SetBasePath(dir);
    scene->GetContentProvider().SetInstallPath(cascRoot);

    if (!svc->Pipeline().InitDevice(gfx::GfxApi::D3D11)) {
        shutdownImGui();
        teardownWindow();
        out.error = "Could not create the graphics device for the asset picker. See "
                    "%TEMP%\\WhiteoutDex_render.log.";
        return out;
    }

    RECT cr{};
    GetClientRect(hwnd, &cr);
    st.width = (std::max)(1L, cr.right - cr.left);
    st.height = (std::max)(1L, cr.bottom - cr.top);
    const renderer::RenderTargetId targetId =
        svc->Pipeline().CreateSwapChainTarget(static_cast<void*>(hwnd), st.width, st.height);
    if (targetId == 0) {
        svc->Pipeline().Shutdown();
        shutdownImGui();
        teardownWindow();
        out.error = "Could not create the picker's swap chain.";
        return out;
    }
    svc->Pipeline().SetPrimaryTarget(targetId);

    // ---- The explorer panel ------------------------------------------------
    auto explorer = std::make_unique<tools::StorageExplorer>(*svc);
    // Lock the game/type row: the caller asked for models, effects or textures,
    // and a picker that lets you switch to another game mid-pick would hand
    // back a path the resource cannot store. The search box, zoom and Grid/Tree
    // switch are part of the browser itself and stay.
    explorer->SetFilterUIVisible(false);
    // Before the open, because this narrows the WALK and not just the listing.
    // It matters most for textures: a Warcraft III install holds an order of
    // magnitude more of them than models, and a picker that walked all of both
    // would pay for the half it is about to hide.
    explorer->SetOpenTypes(types);
    // The installs the host resolved out of its own settings. The panel's game
    // combo is hidden here (SetFilterUIVisible above), and would not have found
    // the second Warcraft III anyway - it asks the game finder for one path per
    // product, and a machine with Reforged AND a classic downgrade has two.
    if (!roots.empty()) {
        std::vector<tools::NamedRoot> named;
        named.reserve(roots.size());
        for (const AssetPickerRoot& r : roots)
            named.push_back(tools::NamedRoot{r.label, r.root});
        explorer->SetNamedRoots(std::move(named));
    }

    // The user's MPQ load order, before the open - it decides what the walk
    // covers. Without it a mod's archive is invisible here even though the
    // renderer and the importer both read it, and an archive the user took out
    // of the order would still be offered by a picker whose extractor can no
    // longer produce it.
    if (!archives.empty())
        explorer->SetArchiveOverride(archives);

    std::string activated; // set by a double-click
    explorer->SetOnActivate([&](const tools::ActivatedFile& f) {
        activated = f.path;
        st.done = true;
    });

    // OpenStorage, not OpenCasc: a Reforged install is CASC and a 1.2x one is a
    // directory of MPQs, and the settings INI holds whichever the user has.
    if (!explorer->OpenStorage(cascRoot)) {
        const std::string err = explorer->LastError();
        explorer.reset();
        svc->Pipeline().Shutdown();
        shutdownImGui();
        teardownWindow();
        out.error =
            err.empty() ? ("Could not open the Warcraft III storage at '" + cascRoot + "'.") : err;
        return out;
    }
    // Only now: SetEnabledTypes clamps against what the open storage actually
    // holds, so before the open it would clamp to nothing. StorageBrowser::Open
    // enables the game's full set, which is what we narrow here.
    explorer->SetBrowseTypes(types);
    if (!initialFilter.empty())
        explorer->SetSearchText(initialFilter);
    if (!initialRel.empty())
        explorer->NavigateTo(ParentFolder(initialRel));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    // ---- Frame loop --------------------------------------------------------
    std::string selected; // archive path of the current single-click
    auto last = std::chrono::steady_clock::now();

    while (!st.done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                st.done = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (st.done)
            break;

        if (st.resized) {
            st.resized = false;
            if (st.width > 0 && st.height > 0)
                svc->Pipeline().ResizePrimaryTarget(st.width, st.height);
        }
        if (st.width <= 0 || st.height <= 0) {
            Sleep(16);
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f)
            dt = 0.1f;

        // Pump the panel's provider and apply any staged navigation before the
        // frame that reads it.
        explorer->NewFrame(dt);

        ImGui::SetCurrentContext(st.ctx);
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        // Two rows of widgets plus padding, measured off the live style so the
        // bar tracks the DPI scale ApplyImGuiDpiScale set.
        const float barH = ImGui::GetFrameHeightWithSpacing() * 2.0f +
                           ImGui::GetStyle().WindowPadding.y * 2.0f;
        const ImVec2 panelSize(vp->WorkSize.x, (std::max)(64.0f, vp->WorkSize.y - barH));

        // BuildWindow sets its own size with ImGuiCond_FirstUseEver, which
        // would win over a SetNextWindowSize here. Size constraints are a
        // separate slot it never touches, so pinning min == max is what
        // actually makes the panel fill the picker.
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(panelSize, panelSize);
        explorer->BuildWindow(nullptr);

        if (!explorer->Opening())
            selected = explorer->Selected();

        // Action bar.
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + panelSize.y),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - panelSize.y),
                                 ImGuiCond_Always);
        ImGui::Begin("##pickerbar", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        {
            const std::string rel = StripModChain(selected);
            ImGui::TextUnformatted("Selected:");
            ImGui::SameLine();
            if (rel.empty())
                ImGui::TextDisabled("(nothing — click a %s, or double-click to pick it)", noun);
            else
                ImGui::TextUnformatted(rel.c_str());

            const float btnW = 110.0f;
            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2.0f * btnW - style.ItemSpacing.x -
                                 style.WindowPadding.x);
            ImGui::BeginDisabled(rel.empty());
            if (ImGui::Button("Select", ImVec2(btnW, 0))) {
                activated = selected;
                st.done = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnW, 0)))
                st.done = true;
        }
        ImGui::End();

        ImGui::Render();

        // Each visible cell renders into its own target here, before the main
        // ImGui pass samples those textures.
        explorer->RenderThumbnails(dt);

        svc->Pipeline().RenderFrame(targetId);
        svc->Pipeline().Present(targetId);
    }

    if (!activated.empty()) {
        out.accepted = true;
        out.archivePath = activated;
        out.relPath = StripModChain(activated);
    }

    // Teardown order matters: the explorer owns thumbnail scenes and GPU
    // targets that must go while the device is still alive.
    explorer.reset();
    svc->Pipeline().Shutdown();
    shutdownImGui();
    svc.reset();
    scene.reset();
    teardownWindow();
    return out;
}

} // namespace whiteout::flakes
