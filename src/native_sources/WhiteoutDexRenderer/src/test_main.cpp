// ============================================================================
// WhiteoutDex Standalone Test Harness
// Loads an .mdx file via MdxModelAdapter → Renderer, no 3ds Max required.
// Usage: WhiteoutDexTest.exe <path-to-mdx-file>
// ============================================================================

#include "renderer/renderer.h"
#include "renderer/mdx_model_adapter.h"
#include <whiteout/models/mdx/parser.h>
#include <filesystem>
#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: WhiteoutDexTest <path-to.mdx>\n";
        return 1;
    }

    std::filesystem::path mdxPath(argv[1]);
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

    // Create adapter
    auto basePath = mdxPath.parent_path();
    WhiteoutDex::MdxModelAdapter adapter(std::move(model), basePath);

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

    std::cout << "Loaded: " << meshes.size() << " meshes, "
              << textures.size() << " textures, "
              << materials.size() << " materials, "
              << skeleton.boneCount << " bones\n";

    // Open renderer
    WhiteoutDex::Renderer renderer;
    if (!renderer.Open(1024, 768)) {
        std::cerr << "Failed to open renderer window\n";
        return 1;
    }

    renderer.LoadModel(meshes, textures, materials, skeleton,
                       skinWeights, particles, ribbons, collisions);

    // Select first sequence if available
    if (!sequences.empty()) {
        adapter.SetActiveSequence(0);
        std::cout << "Playing: " << sequences[0].name
                  << " [" << sequences[0].startMs << "-" << sequences[0].endMs << "ms]\n";
    }

    // Camera defaults
    renderer.SetCamera(30.0f, 45.0f, 300.0f, 0, 0, 50.0f);

    // Main loop
    auto startTime = std::chrono::steady_clock::now();
    std::cout << "Renderer open. Close the window to exit.\n";

    while (renderer.IsOpen()) {
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();

        // Loop animation within sequence bounds
        int timeMs = elapsed;
        if (!sequences.empty()) {
            int duration = sequences[0].endMs - sequences[0].startMs;
            if (duration > 0)
                timeMs = sequences[0].startMs + (elapsed % duration);
        }

        auto frameState = adapter.Evaluate(timeMs);
        renderer.ApplyFrameState(frameState, timeMs);

        Sleep(16); // ~60 FPS
    }

    renderer.Close();
    std::cout << "Done.\n";
    return 0;
}
