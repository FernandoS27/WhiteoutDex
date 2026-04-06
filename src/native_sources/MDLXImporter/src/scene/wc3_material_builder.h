// MDLXImporter — Wc3MaterialBuilder: IR → Wc3Material scripted plugin
#pragma once

#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <max.h>

namespace mdx_scene {

class Wc3MaterialBuilder {
public:
    /// Create Wc3Material instances for all IR materials.
    /// Returns mapping from IR material index → Max Mtl*.
    std::vector<Mtl*> buildMaterials(
        const ir::IRModel& irModel,
        bool importTextures,
        const std::wstring& modelDir,
        Interface* gi,
        core::ExportErrorReporter& reporter);

private:
    Mtl* buildWc3Material(
        const ir::Material& irMat,
        const ir::IRModel& irModel,
        bool importTextures,
        const std::wstring& modelDir,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

} // namespace mdx_scene
