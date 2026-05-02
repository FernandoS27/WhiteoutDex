// ============================================================================
// WhiteoutDex Standalone Test Harness
// Loads an .mdx file via the new component-style API. No 3ds Max required.
// Usage: WhiteoutDexRenderer.exe [--backend d3d11|d3d12] [<path-to-mdx-file>]
// ============================================================================

#include "renderer/render_service.h"
#include "renderer/scene_manager.h"
#include "renderer/camera.h"           // Camera::Mode for the walk-drift gate
#include "renderer/model_instance.h"   // Actor
#include "renderer/model_template.h"   // for cameraPresets accessor
#include "renderer/windows_sound_emitter.h"
#include "ui/render_window.h"
#include "ui/settings_ini.h"           // LoadSettingsIni — restores bg/exposure
#include "gfx/gfx_types.h"
#include "io/path_utf8.h"              // PathToUtf8 — UTF-8 round-trip from fs::path

#include <cctype>
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

    WhiteoutDex::RenderWindow  renderWindow(renderer);
    if (!renderWindow.Open(1024, 768, backend)) {
        std::cerr << "Failed to open renderer window\n";
        return 1;
    }

    // Restore persisted Background colour + Exposure + DNC TOD AFTER
    // the window opens. The DNC service is constructed lazily during
    // gfx-device init (inside RenderWindow::Open), so loading earlier
    // would silently drop the TOD-related settings. The Settings popup
    // is created lazily on first menu click and reads the restored
    // values then; the toolbar doesn't surface any of these knobs.
    WhiteoutDex::LoadSettingsIni(renderer);
    // The View menu was built from compile-time defaults during Open;
    // resync its checkmarks / Tileset radio so the loaded DisplayFlags
    // and Tileset show through.
    renderWindow.SyncViewMenuFromService();

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

    // ---- Walk-cycle locomotion drift ----
    // When the active sequence's name contains "walk" (case-insensitive),
    // translate the actor + free-camera target along world +X so
    // world-space splats / particles fall behind, giving the illusion
    // of forward motion. Speed comes from the MDX SEQS chunk's
    // `moveSpeed` when authored; many walk cycles ship with
    // `moveSpeed=0` (the in-game movement code reads it elsewhere or
    // supplies a default), so we fall back to a fixed 100 game-units/s
    // in that case. Non-walk sequences never drift, even if they
    // carry a non-zero moveSpeed (e.g. attack lunges) — Run cycles
    // are out of scope for the standalone preview's drift behaviour.
    //
    // Rules:
    //   1. Only Orbital (free) camera mode drifts. Direct (MDX preset
    //      / scripted shot) framing is authored-faithful — leaving
    //      the actor at origin keeps the preset's eye→target vector
    //      centred on the model.
    //   2. On every active-sequence change, undo the accumulated
    //      drift in one step so the next animation always starts
    //      from the model's authored origin.
    //   3. We add deltas — never overwrite — so user pan / zoom on
    //      the camera target survives mid-walk.
    struct WalkDrift {
        int   prevSeqIdx  = -1;
        float accumulated = 0.0f;
    };
    WalkDrift drift;
    constexpr float kDefaultWalkSpeed = 100.0f;
    auto containsWalk = [](const std::string& s) {
        for (size_t i = 0; i + 4 <= s.size(); ++i) {
            const auto lc = [](char c) {
                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            };
            if (lc(s[i]) == 'w' && lc(s[i+1]) == 'a' && lc(s[i+2]) == 'l' && lc(s[i+3]) == 'k')
                return true;
        }
        return false;
    };
    auto effectiveMoveSpeed = [&](const WhiteoutDex::SequenceInfo& s) {
        if (!containsWalk(s.name)) return 0.0f;
        return s.moveSpeed != 0.0f ? s.moveSpeed : kDefaultWalkSpeed;
    };
    auto applyWalkDrift = [&](float dt) {
        if (!hero) return;
        if (scene.Camera().GetMode() != WhiteoutDex::Camera::Mode::Orbital) return;
        const int   idx  = hero->animation.ActiveSequenceIndex();
        const auto& seqs = scene.SequenceRanges();
        float delta = 0.0f;
        if (idx != drift.prevSeqIdx) {
            // Sequence change → snap actor + camera target back to
            // the unwalked pose. delta=-accumulated cancels every
            // prior frame's drift in one tick (zero on the very first
            // tick, when accumulated is still 0).
            delta = -drift.accumulated;
            drift.prevSeqIdx = idx;
        } else if (idx >= 0 && idx < (int)seqs.size()) {
            const float ms = effectiveMoveSpeed(seqs[idx]);
            if (ms != 0.0f) delta = ms * dt;
        }
        if (delta == 0.0f) return;
        drift.accumulated += delta;
        hero->worldTransform.data[3][0] += delta;
        const auto t = scene.Camera().GetTarget();
        scene.Camera().SetTarget(t.x + delta, t.y, t.z);
    };

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
        applyWalkDrift(dt);
        // Advance the day/night-cycle clock. No-op when the user has
        // the "Animate TOD" checkbox off (todScale_ defaults to 0) —
        // the slider then controls TOD directly without auto-drift.
        if (auto* dnc = renderer.GetDncService()) dnc->Advance(dt);
        Sleep(16); // ~60 FPS
    }

    renderWindow.Close();
    std::cout << "Done.\n";
    return 0;
}
