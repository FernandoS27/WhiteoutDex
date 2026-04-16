// ============================================================================
// WhiteoutDex Standalone Test Harness
// Loads an .mdx file via MdxModelAdapter → RenderService, no 3ds Max required.
// Usage: WhiteoutDexTest.exe <path-to-mdx-file>
// ============================================================================

#include "renderer/render_service.h"
#include "ui/render_window.h"
#include "io/mdx_model_adapter.h"
#include <whiteout/models/mdx/parser.h>
#include <filesystem>
#include <iostream>
#include <chrono>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

static std::filesystem::path OpenFileDialog() {
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFilter  = "MDX Files (*.mdx)\0*.mdx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile    = filename;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = "Open MDX Model";
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn))
        return std::filesystem::path(filename);
    return {};
}

int main(int argc, char* argv[]) {
    std::filesystem::path mdxPath;
    if (argc >= 2) {
        mdxPath = argv[1];
    } else {
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

    // Parse MDX
    std::cout << "Parsing " << mdxPath.filename().string() << "...\n";
    whiteout::mdx::Parser parser(whiteout::mdx::Parser::ParseMode::Lenient);
    whiteout::mdx::Model model;
    try {
        model = parser.parse(mdxPath.string());
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
    if (!renderWindow.Open(1024, 768)) {
        std::cerr << "Failed to open renderer window\n";
        return 1;
    }

    // Create adapter — pass content provider for CASC/MPQ texture fallback
    auto basePath = mdxPath.parent_path();
    WhiteoutDex::MdxModelAdapter adapter(
        std::move(model), basePath, WhiteoutDex::CoordSpace::MDX,
        &renderer.GetContentProvider());
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

    // PE2 service path — register PlaneEmitters alongside the legacy
    // ParticleSystem (docs/PARTICLEEMITTERS2.md §4 Phase 5). Runs in parallel
    // until the cut-over.
    {
        auto planeInits = adapter.GetPlaneEmitterInits();
        if (!planeInits.empty()) {
            renderer.AddPlaneEmitters(renderer.GetFocusModelHandle(), planeInits);
            std::cout << "  PE2 service: " << planeInits.size() << " emitter(s) registered\n";
        }
    }

    // PE1 (model particle emitters) — set configs + base path for child model loading
    if (!pe1Configs.empty()) {
        renderer.SetPE1BasePath(basePath.string());
        renderer.SetPE1ChildCoordSpace(WhiteoutDex::CoordSpace::MDX);
        renderer.SetPE1Configs(renderer.GetFocusModelHandle(), pe1Configs);
    }

    // Attachment child models — set configs (uses same PE1 base path for resolution)
    if (!attachConfigs.empty()) {
        if (pe1Configs.empty()) {
            renderer.SetPE1BasePath(basePath.string());
            renderer.SetPE1ChildCoordSpace(WhiteoutDex::CoordSpace::MDX);
        }
        renderer.SetAttachmentConfigs(renderer.GetFocusModelHandle(), attachConfigs);
    }

    // Populate sequence picker in the renderer toolbar
    if (!sequences.empty()) {
        std::vector<std::string> seqNames;
        seqNames.reserve(sequences.size());
        for (auto& s : sequences) seqNames.push_back(s.name);
        renderer.SetSequences(seqNames);
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
