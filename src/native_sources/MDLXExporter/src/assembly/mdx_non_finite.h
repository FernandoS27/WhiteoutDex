// MDLXExporter — no NaN or infinity in the MDX
//
// Some protected models ship geosets whose every vertex is NaN, and NaN
// pivots (SusanooKurama5.mdx: two geosets, three bones). Imported, they stay
// NaN in Max, and the exporter wrote them back — and an all-NaN geoset got an
// infinite extent, because the min/max pass steps over NaN. The game draws no
// triangle with a NaN corner; tools that read the MDX choke on the numbers.
// Run before the build, this keeps that look with finite values:
//   - a vertex position that is not finite goes to the origin and the
//     triangles using it are left out; when no triangle would remain, every
//     vertex of the geoset goes to the origin instead (zero area, so still
//     nothing drawn, and the geoset keeps its triangles);
//   - a non-finite normal becomes +Z, a UV 0, a tangent +X;
//   - a non-finite pivot becomes the origin.
// Animation keys that are not finite are dropped already (convertTrack in
// mdx_model_builder.cpp).
#pragma once

#include <core/intermediate_types.h>
#include <util/error_reporter.h>

#include <max.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace mdx_assembly {

namespace non_finite_detail {

inline bool finite(const Point3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline bool finite(const Point2& p) { return std::isfinite(p.x) && std::isfinite(p.y); }
inline bool finite(const Point4& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w);
}

inline std::wstring nodeName(const ir::IRModel& model, int32_t nodeIndex) {
    if (nodeIndex >= 0 && nodeIndex < static_cast<int32_t>(model.nodes.size()) && model.nodes[nodeIndex].maxNode)
        return model.nodes[nodeIndex].maxNode->GetName();
    return {};
}

} // namespace non_finite_detail

// Returns how many values were replaced.
inline size_t replaceNonFinite(ir::IRModel& model, core::ExportErrorReporter& reporter) {
    using namespace non_finite_detail;
    size_t replaced = 0;

    for (auto& mesh : model.meshes) {
        const size_t vertexCount = mesh.vertices.size();
        std::vector<bool> badPosition(vertexCount, false);
        size_t badPositions = 0, otherValues = 0;
        for (size_t i = 0; i < vertexCount; ++i) {
            auto& v = mesh.vertices[i];
            if (!finite(v.position)) {
                badPosition[i] = true;
                ++badPositions;
            }
            if (!finite(v.normal)) {
                v.normal = Point3(0.0f, 0.0f, 1.0f);
                ++otherValues;
            }
            for (int s = 0; s < v.uvSetCount && s < ir::kMaxUVSets; ++s)
                if (!finite(v.uvSets[s])) {
                    v.uvSets[s] = Point2(0.0f, 0.0f);
                    ++otherValues;
                }
            if (v.hasTangent && !finite(v.tangent)) {
                v.tangent = Point4(1.0f, 0.0f, 0.0f, 1.0f);
                ++otherValues;
            }
        }
        if (badPositions == 0 && otherValues == 0)
            continue;

        const std::wstring name = nodeName(model, mesh.nodeIndex);
        if (badPositions > 0) {
            std::vector<uint32_t> kept;
            kept.reserve(mesh.indices.size());
            for (size_t f = 0; f + 2 < mesh.indices.size(); f += 3) {
                const uint32_t a = mesh.indices[f], b = mesh.indices[f + 1], c = mesh.indices[f + 2];
                const bool bad = (a < vertexCount && badPosition[a]) || (b < vertexCount && badPosition[b]) ||
                                 (c < vertexCount && badPosition[c]);
                if (!bad) {
                    kept.push_back(a);
                    kept.push_back(b);
                    kept.push_back(c);
                }
            }
            const bool collapseAll = kept.empty();
            for (size_t i = 0; i < vertexCount; ++i)
                if (collapseAll || badPosition[i])
                    mesh.vertices[i].position = Point3(0.0f, 0.0f, 0.0f);
            const size_t droppedFaces = (mesh.indices.size() - kept.size()) / 3;
            if (collapseAll) {
                reporter.warning(L"All " + std::to_wstring(vertexCount) +
                                     L" vertices have no valid position (NaN or infinite; protected models "
                                     L"ship such geosets). The geoset is written collapsed at the origin, "
                                     L"drawn as little as the game drew it.",
                                 name);
            } else {
                mesh.indices.swap(kept);
                reporter.warning(std::to_wstring(badPositions) + L" of " + std::to_wstring(vertexCount) +
                                     L" vertices have no valid position (NaN or infinite). They are written at "
                                     L"the origin and their " +
                                     std::to_wstring(droppedFaces) +
                                     L" triangle(s) are left out, which the game did not draw either.",
                                 name);
            }
            replaced += badPositions;
        }
        if (otherValues > 0)
            reporter.warning(std::to_wstring(otherValues) +
                                 L" normal, UV or tangent value(s) are not valid numbers (NaN or infinite) and "
                                 L"are written as defaults.",
                             name);
        replaced += otherValues;
    }

    for (size_t i = 0; i < model.nodes.size(); ++i) {
        auto& node = model.nodes[i];
        if (finite(node.pivotPoint))
            continue;
        node.pivotPoint = Point3(0.0f, 0.0f, 0.0f);
        ++replaced;
        reporter.warning(L"The pivot is not a valid number (NaN or infinite) and is written at the origin.",
                         nodeName(model, static_cast<int32_t>(i)));
    }
    for (auto& bone : model.bones)
        if (!finite(bone.pivotPoint))
            bone.pivotPoint = Point3(0.0f, 0.0f, 0.0f);

    return replaced;
}

} // namespace mdx_assembly
