// ============================================================================
// WhiteoutDex Standalone Test Harness
// Loads an .mdx file via the new component-style API. No 3ds Max required.
// Usage: WhiteoutDexRenderer.exe [--backend d3d11|d3d12] [<path-to-mdx-file>]
// ============================================================================

#include "renderer/render_service.h"
#include "renderer/scene_manager.h"
#include "renderer/model_instance.h"   // Actor
#include "renderer/model_template.h"   // for cameraPresets accessor
#include "renderer/windows_sound_emitter.h"
#include "ui/render_window.h"
#include "ui/settings_ini.h"           // LoadSettingsIni — restores bg/exposure
#include "gfx/gfx_types.h"
#include "io/path_utf8.h"              // PathToUtf8 — UTF-8 round-trip from fs::path

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

static std::filesystem::path OpenFileDialog() {
    wchar_t filename[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = L"MDX Files (*.mdx)\0*.mdx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = L"Open MDX Model";
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn))
        return std::filesystem::path(filename);
    return {};
}

int wmain(int argc, wchar_t* argv[]) {
    // ---- Parse args: [--backend <d3d11|d3d12>] [<mdx-path>] ----
    WhiteoutDex::gfx::GfxApi backend = WhiteoutDex::gfx::GfxApi::D3D12;
    std::filesystem::path mdxPath;

    for (int i = 1; i < argc; ++i) {
        const wchar_t* a = argv[i];
        if ((std::wcscmp(a, L"--backend") == 0 || std::wcscmp(a, L"-b") == 0) && i + 1 < argc) {
            const wchar_t* v = argv[++i];
            if      (_wcsicmp(v, L"d3d11") == 0 || _wcsicmp(v, L"dx11") == 0)
                backend = WhiteoutDex::gfx::GfxApi::D3D11;
            else if (_wcsicmp(v, L"d3d12") == 0 || _wcsicmp(v, L"dx12") == 0)
                backend = WhiteoutDex::gfx::GfxApi::D3D12;
            else {
                std::wcerr << L"Unknown backend: " << v << L" (valid: d3d11, d3d12)\n";
                return 1;
            }
        } else if (std::wcscmp(a, L"--help") == 0 || std::wcscmp(a, L"-h") == 0) {
            std::cout << "Usage: WhiteoutFlakes.exe [--backend d3d11|d3d12] [<mdx-path>]\n";
            return 0;
        } else if (mdxPath.empty()) {
            mdxPath = a;
        }
    }

    if (mdxPath.empty()) {
        mdxPath = OpenFileDialog();
        if (mdxPath.empty()) {
            std::cerr << "No file selected.\n";
            return 1;
        }
    }
    if (!std::filesystem::exists(mdxPath)) {
        // PathToUtf8 (not .string()) preserves CJK characters: path::string()
        // narrows to the ANSI code page on Windows.
        std::cerr << "File not found: " << WhiteoutDex::PathToUtf8(mdxPath) << "\n";
        return 1;
    }

    std::cout << "Backend: "
              << (backend == WhiteoutDex::gfx::GfxApi::D3D11 ? "D3D11" : "D3D12") << "\n";

    // Host owns SceneManager; renderer borrows it. Order matters: scene must
    // outlive the renderer (the renderer holds a non-owning pointer).
    WhiteoutDex::SceneManager  scene;
    WhiteoutDex::RenderService renderer(scene);

    // Restore persisted Background colour + Exposure before the window
    // opens so the toolbar / Settings popup pre-populate from the saved
    // values rather than the compile-time defaults. Best-effort — a
    // missing INI leaves the defaults in place.
    WhiteoutDex::LoadSettingsIni(renderer);

    WhiteoutDex::RenderWindow  renderWindow(renderer);
    if (!renderWindow.Open(1024, 768, backend)) {
        std::cerr << "Failed to open renderer window\n";
        return 1;
    }

    // Base path for texture resolution + child-model lookup.
    scene.SetPE1BasePath(mdxPath.parent_path());

    // Audio: install the Windows-native ISoundEmitter so SND
    // EventObjects actually play. Renderer ships a null backend by
    // default — without this, SND fires are silently dropped. The
    // emitter borrows the scene's content provider for CASC/MPQ
    // lookup; lifetime is fine since `scene` outlives `renderer`.
    renderer.SetSoundEmitter(std::make_unique<WhiteoutDex::WindowsSoundEmitter>(
        scene.ActiveContentProvider()));

    // ---- One-line load ----
    // LoadActorFromMdx parses the MDX (cached if seen before), builds the
    // ModelTemplate (geometry + skinning + materials + sequences + camera
    // presets), spawns an Actor, binds its AnimationDriver to the parsed
    // adapter, and sets focus. ~80 lines of legacy boilerplate collapse here.
    // PathToUtf8 keeps CJK characters intact across the std::string boundary;
    // every downstream `std::string` path in the renderer is treated as UTF-8.
    std::cout << "Loading " << WhiteoutDex::PathToUtf8(mdxPath.filename()) << "...\n";
    WhiteoutDex::Actor* hero = renderer.LoadActorFromMdx(WhiteoutDex::PathToUtf8(mdxPath));
    if (!hero) {
        std::cerr << "Failed to load MDX.\n";
        return 1;
    }

    // ---- Surface model info ----
    auto sequences = hero->animation.Sequences();
    std::cout << "Loaded: " << hero->render.gpuMaterials.size() << " materials, "
              << hero->animation.Source()->GetSequences().size() << " sequences"
              << ", template @ " << hero->sourceTemplate.get() << "\n";

    // Populate the toolbar combo + give the camera animator the timeline.
    if (!sequences.empty()) {
        std::vector<std::string> names;
        names.reserve(sequences.size());
        for (auto& s : sequences) names.push_back(s.name);
        scene.SetSequences(std::move(names));
        scene.SetSequenceRanges(sequences);
        std::cout << "Playing: " << sequences[0].name
                  << " [" << sequences[0].startMs << "-" << sequences[0].endMs << "ms]\n";
    }

    // Camera defaults — call directly through the scene's Camera component.
    scene.Camera().SetPitch(30.0f);
    scene.Camera().SetYaw(45.0f);
    scene.Camera().SetDistance(300.0f);
    scene.Camera().SetTarget(0, 0, 50.0f);

    // Camera presets ride along on the template; surface them to the
    // scene's preset list so the toolbar combo can offer them.
    if (hero->sourceTemplate && !hero->sourceTemplate->cameraPresets.empty())
        scene.SetCameraPresets(hero->sourceTemplate->cameraPresets);

    // ---- Main loop ----
    // SceneManager::Update advances the global animation clock, runs the
    // sequence-loop math per actor, and updates each animation cursor.
    // The render thread (inside RenderWindow) calls RenderService::Tick,
    // which evaluates each actor's FrameState and applies it. The host
    // loop's only job is to feed dt and keep the window alive.
    auto last = std::chrono::steady_clock::now();
    std::cout << "Renderer open. Close the window to exit.\n";

    while (renderWindow.IsOpen()) {
        auto  now = std::chrono::steady_clock::now();
        float dt  = std::chrono::duration<float>(now - last).count();
        last = now;
        scene.Update(dt);
        Sleep(16); // ~60 FPS
    }

    renderWindow.Close();
    std::cout << "Done.\n";
    return 0;
}
