// MDLXExporter — Wc3Material extractor
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <vector>
#include <unordered_map>
#include <string>

class Mtl;

namespace mdx_extract {

using MaterialMap = std::unordered_map<Mtl*, int32_t>;

// formatVersion: the MDX version being written (800 Classic, 900+ Reforged).
MaterialMap extractMaterials(const std::vector<core::SceneNode>& nodes,
                             ir::IRModel& model,
                             core::ExportErrorReporter& reporter,
                             int32_t formatVersion = 800);

// Shared helpers — exported for use by particle/ribbon extractors that
// need to add their own texture entries (e.g. PE2 particle texture).
int32_t findOrAddTexture(ir::IRModel& model, const std::string& path,
                         int32_t replaceableId, bool wrapU, bool wrapV,
                         const std::string& sourceDiskPath = "");

} // namespace mdx_extract
