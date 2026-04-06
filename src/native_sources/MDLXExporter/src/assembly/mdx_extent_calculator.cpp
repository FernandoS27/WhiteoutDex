// MDLXExporter — MDX extent calculator implementation
#include "mdx_extent_calculator.h"
#include "mdx_coord_transform.h"
#include <cmath>
#include <algorithm>
#include <limits>

using whiteout::Vector3f;
using whiteout::mdx::Extent;
using whiteout::mdx::Geoset;
using whiteout::mdx::Sequence;
using whiteout::mdx::Model;

namespace {

Extent computeExtentFromVertices(const std::vector<Vector3f>& positions) {
    Extent ext{};
    if (positions.empty()) return ext;

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();

    for (auto& v : positions) {
        minX = std::min(minX, v.x);
        minY = std::min(minY, v.y);
        minZ = std::min(minZ, v.z);
        maxX = std::max(maxX, v.x);
        maxY = std::max(maxY, v.y);
        maxZ = std::max(maxZ, v.z);
    }

    ext.minimum = {minX, minY, minZ};
    ext.maximum = {maxX, maxY, maxZ};

    // Bounding sphere: center at midpoint, radius = half diagonal
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float cz = (minZ + maxZ) * 0.5f;
    float dx = maxX - minX;
    float dy = maxY - minY;
    float dz = maxZ - minZ;
    ext.boundsRadius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;

    return ext;
}

Extent unionExtents(const Extent& a, const Extent& b) {
    Extent out;
    out.minimum.x = std::min(a.minimum.x, b.minimum.x);
    out.minimum.y = std::min(a.minimum.y, b.minimum.y);
    out.minimum.z = std::min(a.minimum.z, b.minimum.z);
    out.maximum.x = std::max(a.maximum.x, b.maximum.x);
    out.maximum.y = std::max(a.maximum.y, b.maximum.y);
    out.maximum.z = std::max(a.maximum.z, b.maximum.z);

    float dx = out.maximum.x - out.minimum.x;
    float dy = out.maximum.y - out.minimum.y;
    float dz = out.maximum.z - out.minimum.z;
    out.boundsRadius = std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f;
    return out;
}

} // anonymous namespace

void MdxExtentCalculator::compute(const ir::IRModel& ir, Model& model) {
    // Compute per-geoset extents from vertex positions
    for (auto& geo : model.geosets) {
        geo.extent = computeExtentFromVertices(geo.vertexPositions);

        // Per-sequence extents: for static meshes, use the same extent for all sequences
        geo.sequenceExtents.resize(model.sequences.size(), geo.extent);
    }

    // Model extent = union of all geoset extents
    if (!model.geosets.empty()) {
        model.modelExtent = model.geosets[0].extent;
        for (size_t i = 1; i < model.geosets.size(); i++)
            model.modelExtent = unionExtents(model.modelExtent, model.geosets[i].extent);
    }

    // Sequence extents
    for (size_t s = 0; s < model.sequences.size(); s++) {
        Extent seqExt{};
        bool first = true;
        for (auto& geo : model.geosets) {
            if (s < geo.sequenceExtents.size()) {
                if (first) {
                    seqExt = geo.sequenceExtents[s];
                    first = false;
                } else {
                    seqExt = unionExtents(seqExt, geo.sequenceExtents[s]);
                }
            }
        }

        // Use the IR sequence extents if available (from custom attribute data)
        if (s < ir.sequences.size()) {
            auto& irSeq = ir.sequences[s];
            if (irSeq.extentRadius > 0.0f) {
                // Use the stored extent from the scene
                model.sequences[s].extent.boundsRadius = irSeq.extentRadius;
                // Coordinate transform can swap min/max on negated axes — re-sort
                auto tMin = mdx_transform::position(irSeq.extentMin);
                auto tMax = mdx_transform::position(irSeq.extentMax);
                model.sequences[s].extent.minimum = {std::min(tMin.x, tMax.x),
                                                      std::min(tMin.y, tMax.y),
                                                      std::min(tMin.z, tMax.z)};
                model.sequences[s].extent.maximum = {std::max(tMin.x, tMax.x),
                                                      std::max(tMin.y, tMax.y),
                                                      std::max(tMin.z, tMax.z)};
                continue;
            }
        }

        model.sequences[s].extent = seqExt;
    }
}

Extent MdxExtentCalculator::fromPositions(const Vector3f* positions, size_t count) {
    std::vector<Vector3f> vec(positions, positions + count);
    return computeExtentFromVertices(vec);
}
