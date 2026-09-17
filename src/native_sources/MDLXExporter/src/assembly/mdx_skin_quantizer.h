// MDLXExporter — v800 skin weight quantizer
// Converts per-vertex float bone weights into v800 matrix groups by snapping
// each weight vector to the nearest achievable *uniform* vector.
//
// v800 matrix groups carry no weights: the runtime averages the group's bones
// equally, so a group of k bones can only ever represent (1/k, …, 1/k).
// Choosing a group is therefore a plain nearest-neighbour quantization, not a
// smoothing problem — which also makes it exactly idempotent for models that
// were imported from an already-quantized classic MDX.
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
    // kMaxSlots is the largest matrix group we will emit. Blizzard's own
    // classic assets go up to 8 bones per group (see Undead3D_Exp, kroxigor
    // family in the corpus), so capping at 4 would silently break round-trip
    // on those. The nearest-uniform rule only ever picks a large k when the
    // weights really are near-uniform across that many bones, so raising the
    // cap does not inflate ordinary hand-painted skins.
    static constexpr int   kMaxSlots  = 8;     // max bones per matrix group
    static constexpr int   kMaxGroups = 255;   // u8 vertex-group limit
    static constexpr float kMinWeight = 0.02f; // discard bleed weights below 2%

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

    static void normalize(VertexWeights& w);
    static void prune(VertexWeights& w, float threshold);
    static void mergeWeight(VertexWeights& w, uint32_t boneId, float weight);

    SlotAssignment assignSlots(const VertexWeights& weights) const;
    Result buildResult(const std::vector<SlotAssignment>& assignments) const;
};
