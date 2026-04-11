#pragma once
// ============================================================================
// MDX Animation Evaluator — Track interpolation + bone hierarchy evaluation
// No Max SDK dependency. Uses WhiteoutLib types + DirectXMath.
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
                       int timeMs, int seqStart, int seqEnd, float defaultVal);

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
    enum class Source { Bone, Helper, ParticleEmitter, ParticleEmitter2, RibbonEmitter, CollisionShape, Attachment, Light, Other };
    Source source = Source::Other;
    int sourceIndex = 0;   // index into the corresponding model vector
};

class MdxHierarchy {
public:
    // Build hierarchy from a parsed Model. Call once at load time.
    void Build(const whiteout::mdx::Model& model);

    // Evaluate the full hierarchy at the given time for the active sequence.
    // Returns world matrices for ALL nodes (indexed by node position in nodes_).
    // Also fills boneWorldMatrices (indexed by bone index 0..boneCount-1).
    // cameraPos is used for billboard node facing (pass nullptr to skip billboarding).
    void Evaluate(int timeMs, int seqStart, int seqEnd,
                  const std::vector<whiteout::u32>& globalSequences,
                  std::vector<XMMATRIX>& boneWorldMatrices,
                  std::vector<XMMATRIX>& allNodeMatrices,
                  const XMFLOAT3* cameraPos = nullptr) const;

    int BoneCount() const { return boneCount_; }
    int NodeCount() const { return (int)nodes_.size(); }
    const std::vector<HierarchyNode>& Nodes() const { return nodes_; }

    // Map from objectId → index in nodes_
    int ObjectIdToNodeIndex(int objectId) const;

private:
    std::vector<HierarchyNode> nodes_;
    int boneCount_ = 0;
    std::unordered_map<int, int> objectIdToIdx_;
};

// ============================================================================
// Utility: Convert WhiteoutLib Matrix44f / 3x4 bind pose to XMMATRIX
// ============================================================================

XMMATRIX BindPose3x4ToXMMatrix(const std::array<whiteout::f32, 12>& bp);
XMMATRIX Vec3QuatScaleToXMMatrix(const whiteout::Vector3f& t,
                                  const whiteout::Quaternion& r,
                                  const whiteout::Vector3f& s,
                                  const whiteout::Vector3f& pivot);

} // namespace WhiteoutDex
