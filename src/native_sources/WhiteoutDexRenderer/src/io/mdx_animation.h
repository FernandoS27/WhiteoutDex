#pragma once
// ============================================================================
// MDX Animation Evaluator — Track interpolation + node hierarchy evaluation
// No Max SDK dependency. Uses WhiteoutLib types.
// ============================================================================

#include "model_types.h"
#include <whiteout/models/mdx/types.h>
#include <whiteout/models/mdx/structures.h>
#include <whiteout/vector_types.h>
#include <vector>
#include <cmath>

namespace WhiteoutDex {

// ============================================================================
// Track evaluation — interpolates Track<T> at a given time within [seqStart, seqEnd].
// For global-sequence tracks, pass calculated effectiveTime + [0, duration].
// ============================================================================

float EvaluateTrackF32(const whiteout::mdx::Track<whiteout::f32>& track,
                       int timeMs, int seqStart, int seqEnd, float defaultVal, bool forceNoInterp = false);

whiteout::u32 EvaluateTrackU32(const whiteout::mdx::Track<whiteout::u32>& track,
                               int timeMs, int seqStart, int seqEnd, whiteout::u32 defaultVal);

whiteout::Vector3f EvaluateTrackVec3(const whiteout::mdx::Track<whiteout::Vector3f>& track,
                                     int timeMs, int seqStart, int seqEnd,
                                     whiteout::Vector3f defaultVal);

whiteout::Quaternion EvaluateTrackQuat(const whiteout::mdx::Track<whiteout::Quaternion>& track,
                                       int timeMs, int seqStart, int seqEnd,
                                       whiteout::Quaternion defaultVal);

// ============================================================================
// MdxHierarchy — pre-built flattened node hierarchy for fast evaluation.
// Constructed once at model load time. Nodes are stored in topological order
// (parent always before child).
// ============================================================================

struct HierarchyNode {
    int objectId  = 0;
    int parentIdx = -1;    // index into MdxHierarchy::nodes_ (-1 = root)
    whiteout::Vector3f pivot = {0, 0, 0};
    uint32_t flags = 0;    // Node::NodeFlag bitfield

    // Tracks (pointers into model data, not owned)
    const whiteout::mdx::Track<whiteout::Vector3f>* translation = nullptr;
    const whiteout::mdx::Track<whiteout::Quaternion>* rotation  = nullptr;
    const whiteout::mdx::Track<whiteout::Vector3f>* scaling     = nullptr;

    // Classification: which model array this node came from, and its index there
    enum class Source { Bone, Helper, ParticleEmitter, ParticleEmitter2, RibbonEmitter, CollisionShape, Attachment, Light, EventObject, Other };
    Source source = Source::Other;
    int sourceIndex = 0;   // index into the corresponding model vector
};

class MdxHierarchy {
public:
    // Build hierarchy from a parsed Model. Call once at load time.
    void Build(const whiteout::mdx::Model& model);

    // Evaluate the full hierarchy at the given time for the active sequence.
    // Returns world matrices for ALL nodes (indexed by node position in nodes_).
    // Also fills boneWorldMatrices (indexed by bone source index, for Max adapter).
    // cameraPos is used for billboard node facing (pass nullptr to skip billboarding).
    void Evaluate(int timeMs, int seqStart, int seqEnd,
                  const std::vector<whiteout::u32>& globalSequences,
                  std::vector<Matrix44f>& boneWorldMatrices,
                  std::vector<Matrix44f>& allNodeMatrices,
                  const Vector3f* cameraPos = nullptr,
                  int globalTimeMs = -1) const;

    int BoneCount() const { return boneCount_; }  // bone-only count (for Max adapter boneWorldMatrices)
    int NodeCount() const { return (int)nodes_.size(); }
    const std::vector<HierarchyNode>& Nodes() const { return nodes_; }

    // Map from objectId → index in nodes_ (hierarchy palette position).
    int ObjectIdToNodeIndex(int objectId) const;

    // Map from dense bone index (0..BoneCount()-1, in model.bones order) →
    // palette position. Mirrors Previewd's `boneMatrices[matsValue]` indexing
    // (see BuildPrimBone @0x1402cf5e0): MDX MATS/SKIN values index the dense
    // bone array directly, NOT objectId. For models where bones are densely
    // numbered 0..numBones-1 at the start of objectId space (the Blizzard
    // convention) both mappings coincide; they diverge when helpers or other
    // node types are interleaved in objectId space.
    int BoneIndexToNodeIndex(int boneIdx) const;

private:
    std::vector<HierarchyNode> nodes_;
    int boneCount_ = 0;
    std::unordered_map<int, int> objectIdToIdx_;
    std::vector<int> boneIdxToNodeIdx_;  // size = boneCount_; -1 if missing
};

// ============================================================================
// Utility: Convert WhiteoutLib 3x4 bind pose to Matrix44f
// ============================================================================

Matrix44f BindPose3x4ToMatrix44f(const std::array<whiteout::f32, 12>& bp);
Matrix44f Vec3QuatScaleToMatrix44f(const whiteout::Vector3f& t,
                                   const whiteout::Quaternion& r,
                                   const whiteout::Vector3f& s,
                                   const whiteout::Vector3f& pivot);

} // namespace WhiteoutDex
