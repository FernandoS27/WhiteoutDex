// MDLXExporter — no geoset with more vertices than a u16 index reaches
//
// PVTX keeps every triangle corner as a u16 (WhiteoutLib mdx/structures.h:
// std::vector<u16> faces) and mdx_model_builder.cpp casts each index to
// uint16_t, so a mesh with more vertices came out with wrapped indices: the
// body feathers of a Phoenix FBX (one mesh, 308,196 MDX vertices) were drawn
// as triangles across the whole model. Run before the build, this cuts such a
// mesh into consecutive pieces of at most kMaxGeosetVertices vertices, in
// triangle order. Each piece is a mesh of its own with the same node,
// material, selection settings, LOD and geoset animation; the builder then
// gives it its own extents. Meshes within the limit are not touched, so
// every other export stays exactly as it was.
#pragma once

#include <core/intermediate_types.h>
#include <util/error_reporter.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mdx_assembly {

// The most vertices one geoset gets (indices 0..65534); the geoset merger
// keeps to the same bound (mdx_geoset_merger.cpp).
inline constexpr size_t kMaxGeosetVertices = 65535;

namespace geoset_split_detail {

inline ir::Mesh emptyLike(const ir::Mesh& m) {
    ir::Mesh p;
    p.name = m.name;
    p.nodeIndex = m.nodeIndex;
    p.materialIndex = m.materialIndex;
    p.hasDropShadow = m.hasDropShadow;
    p.selectionGroup = m.selectionGroup;
    p.unselectable = m.unselectable;
    p.lod = m.lod;
    p.lodName = m.lodName;
    p.sequenceExtents = m.sequenceExtents;
    p.globalExtent = m.globalExtent;
    return p;
}

inline std::wstring nodeName(const ir::IRModel& model, int32_t nodeIndex) {
    if (nodeIndex >= 0 && nodeIndex < static_cast<int32_t>(model.nodes.size()) && model.nodes[nodeIndex].maxNode)
        return model.nodes[nodeIndex].maxNode->GetName();
    return {};
}

} // namespace geoset_split_detail

// Returns how many meshes were split.
inline size_t splitOversizedMeshes(ir::IRModel& model, core::ExportErrorReporter& reporter) {
    using namespace geoset_split_detail;
    bool any = false;
    for (const auto& m : model.meshes)
        if (m.vertices.size() > kMaxGeosetVertices) { any = true; break; }
    if (!any) return 0;

    std::vector<ir::Mesh> out;
    out.reserve(model.meshes.size() + 4);
    std::vector<std::vector<int32_t>> newIndex(model.meshes.size());  // old mesh -> its meshes now
    size_t splitCount = 0;

    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        ir::Mesh& mesh = model.meshes[mi];
        if (mesh.vertices.size() <= kMaxGeosetVertices) {
            newIndex[mi].push_back(static_cast<int32_t>(out.size()));
            out.push_back(std::move(mesh));
            continue;
        }

        const size_t vertexCount = mesh.vertices.size();
        std::vector<int32_t> remap(vertexCount, -1);  // old vertex -> index in the current piece
        std::vector<uint32_t> used;                   // old vertices of the current piece
        ir::Mesh piece = emptyLike(mesh);
        const size_t first = out.size();
        size_t skipped = 0;
        auto finishPiece = [&]() {
            for (uint32_t v : used) remap[v] = -1;
            used.clear();
            newIndex[mi].push_back(static_cast<int32_t>(out.size()));
            out.push_back(std::move(piece));
            piece = emptyLike(mesh);
        };
        for (size_t f = 0; f + 2 < mesh.indices.size(); f += 3) {
            const uint32_t c[3] = {mesh.indices[f], mesh.indices[f + 1], mesh.indices[f + 2]};
            if (c[0] >= vertexCount || c[1] >= vertexCount || c[2] >= vertexCount) {
                ++skipped;  // names no vertex: no geoset could draw it
                continue;
            }
            size_t added = 0;
            for (int k = 0; k < 3; ++k)
                if (remap[c[k]] < 0 && (k == 0 || c[k] != c[0]) && (k < 2 || c[k] != c[1]))
                    ++added;
            if (piece.vertices.size() + added > kMaxGeosetVertices)
                finishPiece();
            for (int k = 0; k < 3; ++k) {
                if (remap[c[k]] < 0) {
                    remap[c[k]] = static_cast<int32_t>(piece.vertices.size());
                    piece.vertices.push_back(mesh.vertices[c[k]]);
                    used.push_back(c[k]);
                }
                piece.indices.push_back(static_cast<uint32_t>(remap[c[k]]));
            }
        }
        if (!piece.indices.empty())
            finishPiece();
        if (newIndex[mi].empty()) {
            // no drawable triangle at all: keep the mesh as it was
            newIndex[mi].push_back(static_cast<int32_t>(out.size()));
            out.push_back(std::move(mesh));
            continue;
        }

        const size_t pieces = out.size() - first;
        for (size_t k = 0; k < pieces; ++k)
            out[first + k].name += " (" + std::to_string(k + 1) + "/" + std::to_string(pieces) + ")";
        reporter.info(L"The mesh has " + std::to_wstring(vertexCount) + L" vertices; a geoset can address " +
                          std::to_wstring(kMaxGeosetVertices) + L" (16-bit triangle indices), so it is written as " +
                          std::to_wstring(pieces) + L" geosets with the same material.",
                      nodeName(model, mesh.nodeIndex));
        if (skipped > 0)
            reporter.warning(std::to_wstring(skipped) + L" triangle(s) name a vertex the mesh does not have and are "
                                                        L"left out.",
                             nodeName(model, mesh.nodeIndex));
        ++splitCount;
    }

    // A geoset animation belongs to every piece of its mesh (same alpha and
    // colour tracks, so the pieces show and hide together).
    std::vector<ir::IRModel::GeosetAnim> anims;
    anims.reserve(model.geosetAnims.size() + out.size() - model.meshes.size());
    for (const auto& ga : model.geosetAnims) {
        if (ga.meshIndex < 0 || ga.meshIndex >= static_cast<int32_t>(newIndex.size())) {
            anims.push_back(ga);
            continue;
        }
        for (int32_t ni : newIndex[ga.meshIndex]) {
            auto copy = ga;
            copy.meshIndex = ni;
            anims.push_back(copy);
        }
    }
    model.geosetAnims.swap(anims);
    model.meshes.swap(out);
    return splitCount;
}

} // namespace mdx_assembly
