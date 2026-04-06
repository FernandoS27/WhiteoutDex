// MaxCore — Mesh geometry extractor
#pragma once

#include "../core/intermediate_types.h"
#include "../util/error_reporter.h"

#include <max.h>
#include <inode.h>

namespace core {

class MeshExtractor {
public:
    /// Extract a single mesh from a geometry node at bind pose (Skin disabled).
    /// Produces triangulated vertices with positions, normals, up to 4 UV sets,
    /// and skin influences if a Skin modifier is present.
    ir::Mesh extract(INode* node, int nodeIndex, TimeValue t,
                     ExportErrorReporter& reporter);

private:
    void extractGeometry(::Mesh& mesh, INode* node, ir::Mesh& out, bool hasEditNormals,
                         ExportErrorReporter& reporter);
    void extractUVSet(::Mesh& mesh, int channel, int setIndex, ir::Mesh& out);
    void extractSkinWeights(INode* node, ir::Mesh& out,
                            const std::vector<int>& faceVertMap,
                            ExportErrorReporter& reporter);

    // Compute smoothing-group-based normal for a vertex on a face
    static Point3 getVNormal(::Mesh& mesh, int faceIdx, int vertIdx);
};

} // namespace core
