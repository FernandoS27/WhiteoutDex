// MDLXExporter — v800 skin weight quantizer
// Converts per-vertex float bone weights into v800 matrix groups using
// iterative relaxation with distance-aware correction and softmax sharpening.
#pragma once

#include <core/intermediate_types.h>
#include "mdx_hierarchy_resolver.h"
#include <whiteout/models/mdx/structures.h>
#include <cstdint>
#include <vector>

class MdxSkinQuantizer {
public:
    struct Result {
        std::vector<uint8_t>  vertexGroups;   // per-vertex group index
        std::vector<uint32_t> matrixGroups;   // bone count per group
        std::vector<uint32_t> matrixIndices;  // flattened bone object IDs
    };

    Result quantize(const ir::Mesh& mesh,
                    const MdxHierarchyResolver& hierarchy);

private:
    // ── Tuning constants ────────────────────────────────────
    static constexpr int   kMaxSlots        = 4;     // max bones per matrix group
    static constexpr int   kMaxGroups       = 255;   // u8 vertex-group limit
    static constexpr int   kRelaxIterations = 6;
    static constexpr float kTempStart       = 0.50f; // initial softmax temperature (soft)
    static constexpr float kTempEnd         = 0.08f; // final softmax temperature (sharp)
    static constexpr float kRelaxAlpha      = 0.25f; // neighbor blend strength
    static constexpr float kMinWeight       = 0.02f; // discard weights below 2%

    // ── Internal types ──────────────────────────────────────
    struct BoneWeight {
        uint32_t boneId;
        float    weight;
    };
    using VertexWeights = std::vector<BoneWeight>;

    struct SlotAssignment {
        uint32_t boneIds[kMaxSlots] = {};
        int      count = 0;

        bool operator==(const SlotAssignment& o) const;
        bool operator<(const SlotAssignment& o) const;
    };

    // ── Pipeline stages ─────────────────────────────────────
    std::vector<VertexWeights> extractWeights(
        const ir::Mesh& mesh, const MdxHierarchyResolver& hierarchy) const;

    std::vector<std::vector<uint32_t>> buildAdjacency(
        const std::vector<uint32_t>& indices, size_t vertexCount) const;

    void relax(std::vector<VertexWeights>& weights,
               const std::vector<ir::Vertex>& vertices,
               const std::vector<std::vector<uint32_t>>& adjacency) const;

    static void sharpen(VertexWeights& w, float temperature);
    static void normalize(VertexWeights& w);
    static void prune(VertexWeights& w, float threshold);
    static void mergeWeight(VertexWeights& w, uint32_t boneId, float weight);

    SlotAssignment assignSlots(const VertexWeights& weights) const;
    Result buildResult(const std::vector<SlotAssignment>& assignments) const;
};
