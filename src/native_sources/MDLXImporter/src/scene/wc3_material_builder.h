// MDLXImporter — Wc3MaterialBuilder: IR → Wc3Material scripted plugin
#pragma once

#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <max.h>
#include <functional>

namespace mdx_scene {
class TextureResolver;
}

namespace mdx_scene {

/// Does this layer import as an HD Wc3Material (shaderType 2)? A shader path,
/// or any texture slot besides diffuse and team colour, makes it one.
bool isHdLayer(const ir::Material& irMat, const ir::MaterialLayer& layer);

class Wc3MaterialBuilder {
public:
    /// Create Wc3Material instances for all IR materials.
    /// Returns mapping from IR material index → Max Mtl*.
    /// `resolver` is a borrowed TextureResolver that handles local disk
    /// + CASC + MPQ lookup with extension aliases. May be nullptr; in
    /// that case only local disk is searched (via the free-function
    /// resolveTexturePath).
    std::vector<Mtl*> buildMaterials(
        const ir::IRModel& irModel,
        bool importTextures,
        const std::wstring& modelDir,
        TextureResolver* resolver,
        Interface* gi,
        core::ExportErrorReporter& reporter);

private:
    /// Function type that returns a texmap vector (indexed by texture index)
    /// for a given layer. Different layers may get different bitmap instances
    /// for the same texture index if they use different texture animations.
    using LayerTexmapsFn =
        std::function<std::vector<Texmap*>(const ir::MaterialLayer&)>;

    Mtl* buildWc3Material(
        const ir::Material& irMat,
        const ir::IRModel& irModel,
        const std::vector<Texmap*>& texmapsFlat,
        const LayerTexmapsFn& buildLayerTexmaps,
        const std::wstring& modelDir,
        TextureResolver* resolver,
        Interface* gi,
        core::ExportErrorReporter& reporter);

    Mtl* buildStdFallback(
        const ir::Material& irMat,
        const ir::IRModel& irModel,
        const std::vector<Texmap*>& texmaps,
        const std::wstring& modelDir,
        Interface* gi);
};

} // namespace mdx_scene
