// MDLXExporter — MDX extent calculator implementation
// CHANGES: Added debug logging for extent computation
#include "mdx_extent_calculator.h"
#include "mdx_coord_transform.h"
#include <cmath>
#include <algorithm>
#include <limits>

#include <max.h>
#ifndef MDX_DEBUG_PRINT
#define MDX_DEBUG_PRINT 1
#endif
#if MDX_DEBUG_PRINT
  #define MDX_LOG(...) DebugPrint(__VA_ARGS__)
#else
  #define MDX_LOG(...) ((void)0)
#endif

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
        minX = std::min(minX, v.x); minY = std::min(minY, v.y); minZ = std::min(minZ, v.z);
        maxX = std::max(maxX, v.x); maxY = std::max(maxY, v.y); maxZ = std::max(maxZ, v.z);
    }

    ext.minimum = {minX, minY, minZ};
    ext.maximum = {maxX, maxY, maxZ};

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
    MDX_LOG(_T("── ExtentCalculator::compute ──\n"));

    for (size_t g = 0; g < model.geosets.size(); g++) {
        auto& geo = model.geosets[g];
        geo.extent = computeExtentFromVertices(geo.vertexPositions);
        geo.sequenceExtents.resize(model.sequences.size(), geo.extent);

        MDX_LOG(_T("  geo[%d] extent: radius=%.2f min=(%.1f,%.1f,%.1f) max=(%.1f,%.1f,%.1f)\n"),
                (int)g, geo.extent.boundsRadius,
                geo.extent.minimum.x, geo.extent.minimum.y, geo.extent.minimum.z,
                geo.extent.maximum.x, geo.extent.maximum.y, geo.extent.maximum.z);
    }

    if (!model.geosets.empty()) {
        model.modelExtent = model.geosets[0].extent;
        for (size_t i = 1; i < model.geosets.size(); i++)
            model.modelExtent = unionExtents(model.modelExtent, model.geosets[i].extent);
    }

    MDX_LOG(_T("  Model extent: radius=%.2f\n"), model.modelExtent.boundsRadius);

    for (size_t s = 0; s < model.sequences.size(); s++) {
        Extent seqExt{};
        bool first = true;
        for (auto& geo : model.geosets) {
            if (s < geo.sequenceExtents.size()) {
                if (first) { seqExt = geo.sequenceExtents[s]; first = false; }
                else seqExt = unionExtents(seqExt, geo.sequenceExtents[s]);
            }
        }

        if (s < ir.sequences.size()) {
            auto& irSeq = ir.sequences[s];
            if (irSeq.extentRadius > 0.0f) {
                auto tMin = mdx_transform::position(irSeq.extentMin);
                auto tMax = mdx_transform::position(irSeq.extentMax);
                model.sequences[s].extent.boundsRadius = irSeq.extentRadius;
                model.sequences[s].extent.minimum = {std::min(tMin.x, tMax.x),
                                                      std::min(tMin.y, tMax.y),
                                                      std::min(tMin.z, tMax.z)};
                model.sequences[s].extent.maximum = {std::max(tMin.x, tMax.x),
                                                      std::max(tMin.y, tMax.y),
                                                      std::max(tMin.z, tMax.z)};
                MDX_LOG(_T("  seq[%d] \"%S\" extent from CA: radius=%.2f\n"),
                        (int)s, model.sequences[s].name.c_str(), irSeq.extentRadius);
                continue;
            }
        }

        model.sequences[s].extent = seqExt;
        MDX_LOG(_T("  seq[%d] \"%S\" extent computed: radius=%.2f\n"),
                (int)s, model.sequences[s].name.c_str(), seqExt.boundsRadius);
    }

    MDX_LOG(_T("── ExtentCalculator done ──\n\n"));
}

Extent MdxExtentCalculator::fromPositions(const Vector3f* positions, size_t count) {
    std::vector<Vector3f> vec(positions, positions + count);
    return computeExtentFromVertices(vec);
}
