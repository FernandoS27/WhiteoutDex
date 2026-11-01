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
#include <map>
#include <vector>

// How the rig really moves, for the gradient option: the rigid move of a node
// from frame 0, where the vertices are exported, to moments spread over the
// sequences. One per export; a node is sampled the first time it is asked for.
class MdxBoneMotion {
public:
    explicit MdxBoneMotion(const ir::IRModel& ir);

    size_t sampleCount() const { return times_.size(); }
    // Empty when the node has no scene node to sample.
    const std::vector<Matrix3>& movesOf(int32_t irNode);

private:
    const ir::IRModel& ir_;
    std::vector<TimeValue> times_;
    std::map<int32_t, std::vector<Matrix3>> moves_;
};

class MdxSkinQuantizer {
public:
    struct Result {
        std::vector<uint8_t>  vertexGroups;   // per-vertex group index
        std::vector<uint32_t> matrixGroups;   // bone count per group
        std::vector<uint32_t> matrixIndices;  // flattened bone object IDs
    };

    // With `motion`, the optional gradient mode: a group may list a bone more
    // than once, so [A,A,B] means 2/3 A and 1/3 B, and the runtime's equal
    // average keeps a gradient the plain groups would round away. At most
    // kGradientEntries entries per group, because Warcraft III falls back to
    // skinning on the CPU above that.
    Result quantize(const ir::Mesh& mesh,
                    const MdxHierarchyResolver& hierarchy,
                    MdxBoneMotion* motion = nullptr);

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
    static constexpr int   kGradientEntries = 4; // max entries per group in gradient mode

    // ── Internal types ──────────────────────────────────────
    struct BoneWeight {
        uint32_t boneId;
        float    weight;
        int32_t  irNode;   // the node behind boneId, for MdxBoneMotion
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
    static void mergeWeight(VertexWeights& w, uint32_t boneId, float weight, int32_t irNode);

    SlotAssignment assignSlots(const VertexWeights& weights) const;
    SlotAssignment assignGradient(const VertexWeights& weights, const Point3& position,
                                  MdxBoneMotion& motion) const;
    static double ratioDistance(const SlotAssignment& a, const SlotAssignment& b);
    Result buildResult(const std::vector<SlotAssignment>& assignments, bool byRatio) const;
};
