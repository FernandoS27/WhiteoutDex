// MDLXExporter — Geoset animation extractor (KGAO = alpha, KGAC = color)
//
// Reads per-mesh animation data that ends up in the MDX GEOA chunk:
//   * Visibility track on the mesh INode → alpha animation (KGAO)
//   * Base color of the material (or a dedicated color controller) → color (KGAC)
//
// For each mesh in the scene, we emit one ir::IRModel::GeosetAnim entry.
// Meshes with neither animation nor non-default values still get an entry
// so the GEOA chunk always has one slot per geoset (matches NeoDex behaviour).
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <vector>

namespace mdx_extract {

void extractGeosetAnims(const std::vector<core::SceneNode>& nodes,
                        ir::IRModel& model,
                        core::ExportErrorReporter& reporter);

} // namespace mdx_extract
