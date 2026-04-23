// ============================================================================
// WhiteoutDex Standalone Test Harness
// Loads an .mdx file via MdxModelAdapter → RenderService, no 3ds Max required.
// Usage: WhiteoutDexRenderer.exe [--backend d3d11|d3d12] [<path-to-mdx-file>]
// ============================================================================

#include "renderer/render_service.h"
#include "ui/render_window.h"
#include "io/mdx_model_adapter.h"
#include "gfx/gfx_types.h"
#include <whiteout/models/mdx/parser.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
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
        std::cerr << "File not found: " << mdxPath.string() << "\n";
        return 1;
    }

    std::cout << "Backend: "
              << (backend == WhiteoutDex::gfx::GfxApi::D3D11 ? "D3D11" : "D3D12") << "\n";

    // Parse MDX
    // Read the file via std::ifstream(fs::path) so MSVC uses _wfopen internally
    // — this handles Unicode (CJK, etc.) paths without changing the WhiteoutLib API.
    std::cout << "Parsing " << mdxPath.filename().generic_string() << "...\n";
    whiteout::mdx::Parser parser(whiteout::mdx::Parser::ParseMode::Lenient);
    whiteout::mdx::Model model;
    try {
        std::ifstream mdxFile(mdxPath, std::ios::binary);
        if (!mdxFile.is_open())
            throw std::runtime_error("Failed to open file");
        std::vector<uint8_t> mdxBytes(
            (std::istreambuf_iterator<char>(mdxFile)),
            std::istreambuf_iterator<char>());
        auto ext = mdxPath.extension();
        auto fmt = (ext == L".mdl" || ext == L".MDL")
                       ? whiteout::mdx::MDLXFormat::MDL
                       : whiteout::mdx::MDLXFormat::MDX;
        model = parser.parse(
            std::span<const whiteout::u8>(mdxBytes.data(), mdxBytes.size()), fmt);
    } catch (const std::exception& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "Model: v" << model.version
              << ", " << model.geosets.size() << " geosets"
              << ", " << model.bones.size() << " bones"
              << ", " << model.sequences.size() << " sequences"
              << ", " << model.particleEmitters2.size() << " particles"
              << ", " << model.ribbonEmitters.size() << " ribbons\n";

    // Open renderer (initializes the FileContentProvider which discovers WC3)
    WhiteoutDex::RenderService renderer;
    WhiteoutDex::RenderWindow renderWindow(renderer);
    if (!renderWindow.Open(1024, 768, backend)) {
        std::cerr << "Failed to open renderer window\n";
        return 1;
    }

    // Create adapter — pass content provider for CASC/MPQ texture fallback
    auto basePath = mdxPath.parent_path();
    WhiteoutDex::MdxModelAdapter adapter(
        std::move(model), basePath, &renderer.GetContentProvider());
    renderer.GetContentProvider().SetBasePath(basePath);

    // Fetch static data
    auto meshes     = adapter.GetMeshes();
    auto textures   = adapter.GetTextures();
    auto materials  = adapter.GetMaterials();
    auto skeleton   = adapter.GetSkeleton();
    auto skinWeights = adapter.GetSkinWeights();
    auto particles  = adapter.GetParticleConfigs();
    auto ribbons    = adapter.GetRibbonConfigs();
    auto collisions = adapter.GetCollisionShapes();
    auto sequences  = adapter.GetSequences();
    auto pe1Configs = adapter.GetPE1Configs();
    auto attachConfigs = adapter.GetAttachmentConfigs();

    std::cout << "Loaded: " << meshes.size() << " meshes, "
              << textures.size() << " textures, "
              << materials.size() << " materials, "
              << skeleton.nodeCount << " nodes\n";
    if (!pe1Configs.empty())
        std::cout << "  PE1: " << pe1Configs.size() << " emitter(s)\n";
    {
        int attModels = 0;
        for (auto& a : attachConfigs) if (!a.modelPath.empty()) attModels++;
        if (attModels > 0)
            std::cout << "  Attachments: " << attModels << " with child model(s)\n";
    }

    renderer.LoadModel(meshes, textures, materials, skeleton,
                       skinWeights, particles, ribbons, collisions);
    // LoadModel internally registers PE2 emitters with the service via
    // InitFromLegacyConfig. GetPlaneEmitterInits() / AddPlaneEmitters() is
    // available for callers that want to bypass the legacy config path.

    // PE1 (model particle emitters) — set configs + base path for child model loading
    if (!pe1Configs.empty()) {
        renderer.SetPE1BasePath(basePath);
        renderer.SetPE1Configs(renderer.GetFocusModelHandle(), pe1Configs);
    }

    // Attachment child models — set configs (uses same PE1 base path for resolution)
    if (!attachConfigs.empty()) {
        if (pe1Configs.empty()) {
            renderer.SetPE1BasePath(basePath);
        }
        renderer.SetAttachmentConfigs(renderer.GetFocusModelHandle(), attachConfigs);
    }

    // Populate sequence picker in the renderer toolbar
    if (!sequences.empty()) {
        std::vector<std::string> seqNames;
        seqNames.reserve(sequences.size());
        for (auto& s : sequences) seqNames.push_back(s.name);
        renderer.SetSequences(seqNames);
        renderer.SetSequenceRanges(sequences);  // needed for MDX camera animators
        adapter.SetActiveSequence(0);
        std::cout << "Playing: " << sequences[0].name
                  << " [" << sequences[0].startMs << "-" << sequences[0].endMs << "ms]\n";
    }

    // Camera defaults
    renderer.SetCamera(30.0f, 45.0f, 300.0f, 0, 0, 50.0f);

    // Populate camera combo with model cameras
    auto cameraPresets = adapter.GetCameraPresets();
    if (!cameraPresets.empty())
        renderer.SetCameraPresets(cameraPresets);

    // Main loop
    auto startTime = std::chrono::steady_clock::now();
    int currentSeq = 0;
    std::cout << "Renderer open. Close the window to exit.\n";

    while (renderWindow.IsOpen()) {
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();

        // React to sequence picker changes
        if (!sequences.empty()) {
            int picked = renderer.GetActiveSequenceIndex();
            if (picked >= 0 && picked < (int)sequences.size() && picked != currentSeq) {
                currentSeq = picked;
                adapter.SetActiveSequence(currentSeq);
                startTime = now;
                elapsed = 0;
            }
        }

        // Loop animation within sequence bounds
        int timeMs = elapsed;
        if (!sequences.empty()) {
            const auto& seq = sequences[currentSeq];
            int duration = seq.endMs - seq.startMs;
            if (duration > 0)
                timeMs = seq.startMs + (elapsed % duration);
        }

        auto camPos = renderer.GetCameraPosition();
        adapter.SetCameraPosition(camPos.x, camPos.y, camPos.z);
        auto frameState = adapter.Evaluate(timeMs, elapsed);
        renderer.ApplyFrameState(frameState, timeMs);

        Sleep(16); // ~60 FPS
    }

    renderWindow.Close();
    std::cout << "Done.\n";
    return 0;
}
