// MDLXExporter — Wc3Ribbon extractor
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include "wc3_material_extractor.h"
#include <vector>

namespace mdx_extract {

void extractRibbons(const std::vector<core::SceneNode>& nodes,
                    ir::IRModel& model,
                    const MaterialMap& mtlToIndex,
                    core::ExportErrorReporter& reporter);

} // namespace mdx_extract
