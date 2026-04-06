// MDLXExporter — Wc3Particles2 extractor
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <vector>

namespace mdx_extract {

void extractParticles2(const std::vector<core::SceneNode>& nodes,
                       ir::IRModel& model,
                       core::ExportErrorReporter& reporter);

} // namespace mdx_extract
