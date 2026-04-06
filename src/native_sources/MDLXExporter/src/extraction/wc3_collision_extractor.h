// MDLXExporter — Wc3Collision extractor (scripted plugin boxes/spheres)
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <vector>

namespace mdx_extract {

void extractCollisions(const std::vector<core::SceneNode>& nodes,
                       ir::IRModel& model,
                       core::ExportErrorReporter& reporter);

} // namespace mdx_extract
