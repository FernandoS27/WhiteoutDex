// MDLXExporter — Wc3Material extractor
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <vector>
#include <unordered_map>

class Mtl;

namespace mdx_extract {

using MaterialMap = std::unordered_map<Mtl*, int32_t>;

MaterialMap extractMaterials(const std::vector<core::SceneNode>& nodes,
                             ir::IRModel& model,
                             core::ExportErrorReporter& reporter);

} // namespace mdx_extract
