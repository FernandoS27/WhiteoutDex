// MaxCore — Vertex optimizer (merge near-identical vertices)
#pragma once

#include "../core/intermediate_types.h"

namespace core {

class VertexOptimizer {
public:
    /// Merge near-identical vertices in an ir::Mesh.
    /// Updates mesh.vertices and remaps mesh.indices.
    void optimize(ir::Mesh& mesh, float threshold = 0.001f);
};

} // namespace core
