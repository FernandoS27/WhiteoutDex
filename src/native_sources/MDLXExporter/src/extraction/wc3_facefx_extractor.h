// MDLXExporter — BlizzFaceFX extractor (v1200+)
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <vector>

namespace mdx_extract {

struct FaceFXExtensionData : public ir::ExtensionData {
    std::string facefxName;
    std::string facefxPath;
};

void extractFaceFX(const std::vector<core::SceneNode>& nodes,
                   ir::IRModel& model,
                   core::ExportErrorReporter& reporter);

} // namespace mdx_extract
