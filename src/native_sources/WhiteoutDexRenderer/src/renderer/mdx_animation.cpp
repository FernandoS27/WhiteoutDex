// ============================================================================
// MDX Animation Evaluator — Track interpolation + bone hierarchy evaluation
// ============================================================================

#include "mdx_animation.h"
#include <algorithm>
#include <numeric>

using namespace whiteout;
using namespace whiteout::mdx;

namespace WhiteoutDex {

// ============================================================================
// Helper: find bracketing keyframes for a given time, restricted to a
// sequence's [seqStart, seqEnd] range.
//
// MDX tracks concatenate keyframes from all sequences into a single list, so
// evaluation must filter by the active sequence range — otherwise we bracket
// across into a neighbouring sequence and slerp between unrelated poses.
//
// When timeMs falls in the "gap" between seqStart and the first in-range
// keyframe, or between the last in-range keyframe and seqEnd, we wrap around
// (last → first through the loop point) instead of clamping. This matches
// mdx-m3-viewer's behaviour for looping sequences and keeps animation smooth
// across the loop boundary.
//
//   segLen = (firstKey - lastKey) + (seqEnd - seqStart)     // positive
//   post-last  (timeMs ∈ [lastKey, seqEnd)): pos = timeMs - lastKey
//   pre-first  (timeMs ∈ [seqStart, firstKey)): pos = (timeMs - seqStart) + (seqEnd - lastKey)
//   t = pos / segLen
// ============================================================================

struct KeyBracket {
    int lo = -1;
    int hi = -1;
    float t = 0.0f;
};

template<typename KeyType>
static KeyBracket FindBracket(const KeyType* keys, int count, int timeMs,
                              int seqStart, int seqEnd) {
    KeyBracket b;
    if (count == 0) return b;

    // Locate in-sequence keyframe index range [rangeLo, rangeHi].
    int rangeLo = -1, rangeHi = -1;
    for (int i = 0; i < count; i++) {
        int f = (int)keys[i].frame;
        if (f > seqEnd) break;
        if (f >= seqStart) {
            if (rangeLo < 0) rangeLo = i;
            rangeHi = i;
        }
    }
    if (rangeLo < 0) return b;                      // no keys in this sequence
    if (rangeLo == rangeHi) { b.lo = b.hi = rangeLo; return b; }

    int firstFrame = (int)keys[rangeLo].frame;
    int lastFrame  = (int)keys[rangeHi].frame;

    // Wrap region: timeMs is in the gap between lastFrame and firstFrame
    // (going forward through the loop point). Bracket = (lastKey, firstKey).
    if (timeMs < firstFrame || timeMs >= lastFrame) {
        int loopLen = seqEnd - seqStart;
        int segLen  = (firstFrame - lastFrame) + loopLen;
        b.lo = rangeHi;
        b.hi = rangeLo;
        if (segLen <= 0) { b.t = 0.0f; return b; }
        int pos;
        if (timeMs >= lastFrame) pos = timeMs - lastFrame;
        else                     pos = (timeMs - seqStart) + (seqEnd - lastFrame);
        float t = (float)pos / (float)segLen;
        b.t = std::clamp(t, 0.0f, 1.0f);
        return b;
    }

    // Normal bracket within the in-range span
    for (int i = rangeLo; i < rangeHi; i++) {
        int fa = (int)keys[i].frame;
        int fb = (int)keys[i + 1].frame;
        if (timeMs >= fa && timeMs < fb) {
            b.lo = i;
            b.hi = i + 1;
            int denom = fb - fa;
            b.t = denom > 0 ? (float)(timeMs - fa) / (float)denom : 0.0f;
            return b;
        }
    }
    b.lo = b.hi = rangeHi;
    return b;
}

// ============================================================================
// Scalar hermite/bezier interpolation
// ============================================================================

static float HermiteInterp(float a, float outTanA, float inTanB, float b, float t) {
    float t2 = t * t, t3 = t2 * t;
    float h1 =  2*t3 - 3*t2 + 1;
    float h2 = -2*t3 + 3*t2;
    float h3 =    t3 - 2*t2 + t;
    float h4 =    t3 -   t2;
    return h1*a + h2*b + h3*outTanA + h4*inTanB;
}

static float BezierInterp(float a, float outTanA, float inTanB, float b, float t) {
    float it = 1.0f - t;
    return it*it*it*a + 3*it*it*t*outTanA + 3*it*t*t*inTanB + t*t*t*b;
}

// ============================================================================
// Vector3f hermite/bezier interpolation (component-wise)
// ============================================================================

static Vector3f HermiteInterpV3(const Vector3f& a, const Vector3f& ota,
                                const Vector3f& itb, const Vector3f& b, float t) {
    return {HermiteInterp(a.x, ota.x, itb.x, b.x, t),
            HermiteInterp(a.y, ota.y, itb.y, b.y, t),
            HermiteInterp(a.z, ota.z, itb.z, b.z, t)};
}

static Vector3f BezierInterpV3(const Vector3f& a, const Vector3f& ota,
                                const Vector3f& itb, const Vector3f& b, float t) {
    return {BezierInterp(a.x, ota.x, itb.x, b.x, t),
            BezierInterp(a.y, ota.y, itb.y, b.y, t),
            BezierInterp(a.z, ota.z, itb.z, b.z, t)};
}

// ============================================================================
// EvaluateTrackF32
// ============================================================================

float EvaluateTrackF32(const Track<f32>& track, int timeMs, int seqStart, int seqEnd, float defaultVal) {
    if (!track.isUsed || track.keyCount == 0) return defaultVal;

    auto interp = track.interpolationType;
    if (interp == InterpolationType::None || interp == InterpolationType::Linear) {
        auto keys = const_cast<Track<f32>&>(track).keys();
        int count = (int)keys.size();
        auto br = FindBracket(keys.data(), count, timeMs, seqStart, seqEnd);
        if (br.lo < 0) return defaultVal;
        if (br.lo == br.hi) return keys[br.lo].value;
        if (interp == InterpolationType::None) return keys[br.lo].value;
        return keys[br.lo].value + (keys[br.hi].value - keys[br.lo].value) * br.t;
    } else {
        auto keys = const_cast<Track<f32>&>(track).tangentKeys();
        int count = (int)keys.size();
        auto br = FindBracket(keys.data(), count, timeMs, seqStart, seqEnd);
        if (br.lo < 0) return defaultVal;
        if (br.lo == br.hi) return keys[br.lo].value;
        if (interp == InterpolationType::Hermite)
            return HermiteInterp(keys[br.lo].value, keys[br.lo].outTan, keys[br.hi].inTan, keys[br.hi].value, br.t);
        else // Bezier
            return BezierInterp(keys[br.lo].value, keys[br.lo].outTan, keys[br.hi].inTan, keys[br.hi].value, br.t);
    }
}

// ============================================================================
// EvaluateTrackU32
// ============================================================================

u32 EvaluateTrackU32(const Track<u32>& track, int timeMs, int seqStart, int seqEnd, u32 defaultVal) {
    if (!track.isUsed || track.keyCount == 0) return defaultVal;
    // u32 tracks are always step (nearest key)
    auto keys = const_cast<Track<u32>&>(track).keys();
    int count = (int)keys.size();
    if (count == 0) return defaultVal;

    // Step semantics: hold the most recent in-sequence key whose frame <= timeMs,
    // falling back to the first in-sequence key if timeMs precedes it.
    int rangeLo = -1, rangeHi = -1;
    for (int i = 0; i < count; i++) {
        int f = (int)keys[i].frame;
        if (f > seqEnd) break;
        if (f >= seqStart) { if (rangeLo < 0) rangeLo = i; rangeHi = i; }
    }
    if (rangeLo < 0) return defaultVal;

    u32 val = keys[rangeLo].value;
    for (int i = rangeLo; i <= rangeHi; i++) {
        if ((int)keys[i].frame <= timeMs) val = keys[i].value;
        else break;
    }
    return val;
}

// ============================================================================
// EvaluateTrackVec3
// ============================================================================

Vector3f EvaluateTrackVec3(const Track<Vector3f>& track, int timeMs, int seqStart, int seqEnd,
                           Vector3f defaultVal) {
    if (!track.isUsed || track.keyCount == 0) return defaultVal;

    auto interp = track.interpolationType;
    if (interp == InterpolationType::None || interp == InterpolationType::Linear) {
        auto keys = const_cast<Track<Vector3f>&>(track).keys();
        int count = (int)keys.size();
        auto br = FindBracket(keys.data(), count, timeMs, seqStart, seqEnd);
        if (br.lo < 0) return defaultVal;
        if (br.lo == br.hi) return keys[br.lo].value;
        if (interp == InterpolationType::None) return keys[br.lo].value;
        return Vector3f::lerp(keys[br.lo].value, keys[br.hi].value, br.t);
    } else {
        auto keys = const_cast<Track<Vector3f>&>(track).tangentKeys();
        int count = (int)keys.size();
        auto br = FindBracket(keys.data(), count, timeMs, seqStart, seqEnd);
        if (br.lo < 0) return defaultVal;
        if (br.lo == br.hi) return keys[br.lo].value;
        if (interp == InterpolationType::Hermite)
            return HermiteInterpV3(keys[br.lo].value, keys[br.lo].outTan, keys[br.hi].inTan, keys[br.hi].value, br.t);
        else
            return BezierInterpV3(keys[br.lo].value, keys[br.lo].outTan, keys[br.hi].inTan, keys[br.hi].value, br.t);
    }
}

// ============================================================================
// EvaluateTrackQuat
// ============================================================================

Quaternion EvaluateTrackQuat(const Track<Quaternion>& track, int timeMs, int seqStart, int seqEnd,
                             Quaternion defaultVal) {
    if (!track.isUsed || track.keyCount == 0) return defaultVal;

    auto interp = track.interpolationType;
    if (interp == InterpolationType::None || interp == InterpolationType::Linear) {
        auto keys = const_cast<Track<Quaternion>&>(track).keys();
        int count = (int)keys.size();
        auto br = FindBracket(keys.data(), count, timeMs, seqStart, seqEnd);
        if (br.lo < 0) return defaultVal;
        if (br.lo == br.hi) return keys[br.lo].value;
        if (interp == InterpolationType::None) return keys[br.lo].value;
        return Quaternion::slerp(keys[br.lo].value, keys[br.hi].value, br.t);
    } else {
        auto keys = const_cast<Track<Quaternion>&>(track).tangentKeys();
        int count = (int)keys.size();
        auto br = FindBracket(keys.data(), count, timeMs, seqStart, seqEnd);
        if (br.lo < 0) return defaultVal;
        if (br.lo == br.hi) return keys[br.lo].value;
        // For hermite/bezier quaternion tracks, use squad
        // return Quaternion::slerp(keys[br.lo].value, keys[br.hi].value, br.t);
        return Quaternion::squad(keys[br.lo].value, keys[br.lo].outTan, keys[br.hi].inTan, keys[br.hi].value, br.t);
    }
}

// ============================================================================
// BindPose3x4ToXMMatrix: 3x4 row-major (a00..a23) → 4x4 XMMATRIX
// ============================================================================

XMMATRIX BindPose3x4ToXMMatrix(const std::array<f32, 12>& bp) {
    return XMMATRIX(
        bp[0], bp[1], bp[2],  0.0f,
        bp[3], bp[4], bp[5],  0.0f,
        bp[6], bp[7], bp[8],  0.0f,
        bp[9], bp[10],bp[11], 1.0f
    );
}

// ============================================================================
// Vec3QuatScaleToXMMatrix: Compose T/R/S around a pivot point into XMMATRIX.
//   M = Translate(-pivot) * Scale * Rotate * Translate(pivot) * Translate(t)
// ============================================================================

XMMATRIX Vec3QuatScaleToXMMatrix(const Vector3f& t, const Quaternion& r,
                                  const Vector3f& s, const Vector3f& pivot) {
    XMVECTOR quat = XMVectorSet(r.x, r.y, r.z, r.w);
    XMVECTOR scale = XMVectorSet(s.x, s.y, s.z, 1.0f);
    XMVECTOR trans = XMVectorSet(t.x, t.y, t.z, 1.0f);
    XMVECTOR piv   = XMVectorSet(pivot.x, pivot.y, pivot.z, 0.0f);

    // SRT around pivot: Translate(-pivot) * S * R * Translate(pivot + t)
    XMMATRIX mS = XMMatrixScalingFromVector(scale);
    XMMATRIX mR = XMMatrixRotationQuaternion(quat);
    XMMATRIX mNegPiv = XMMatrixTranslationFromVector(XMVectorNegate(piv));
    XMMATRIX mPosPivT = XMMatrixTranslationFromVector(XMVectorAdd(piv, trans));

    return mNegPiv * mS * mR * mPosPivT;
}

// ============================================================================
// MdxHierarchy::Build
// ============================================================================

void MdxHierarchy::Build(const whiteout::mdx::Model& model) {
    nodes_.clear();
    objectIdToIdx_.clear();
    boneCount_ = (int)model.bones.size();

    // Gather all node-bearing objects into a flat list
    auto addNode = [&](const Node& node, HierarchyNode::Source src, int srcIdx) {
        HierarchyNode hn;
        hn.objectId    = (int)node.objectId;
        hn.flags       = (uint32_t)node.flags;
        hn.translation = &node.translationTracks;
        hn.rotation    = &node.rotationTracks;
        hn.scaling     = &node.scalingTracks;
        hn.source      = src;
        hn.sourceIndex = srcIdx;
        if (hn.objectId < (int)model.pivotPoints.size())
            hn.pivot = model.pivotPoints[hn.objectId];
        nodes_.push_back(hn);
    };

    for (int i = 0; i < (int)model.bones.size(); i++)
        addNode(model.bones[i].node, HierarchyNode::Source::Bone, i);
    for (int i = 0; i < (int)model.helpers.size(); i++)
        addNode(model.helpers[i].node, HierarchyNode::Source::Helper, i);
    for (int i = 0; i < (int)model.particleEmitters.size(); i++)
        addNode(model.particleEmitters[i].node, HierarchyNode::Source::ParticleEmitter, i);
    for (int i = 0; i < (int)model.particleEmitters2.size(); i++)
        addNode(model.particleEmitters2[i].node, HierarchyNode::Source::ParticleEmitter2, i);
    for (int i = 0; i < (int)model.ribbonEmitters.size(); i++)
        addNode(model.ribbonEmitters[i].node, HierarchyNode::Source::RibbonEmitter, i);
    for (int i = 0; i < (int)model.collisionShapes.size(); i++)
        addNode(model.collisionShapes[i].node, HierarchyNode::Source::CollisionShape, i);
    for (int i = 0; i < (int)model.attachments.size(); i++)
        addNode(model.attachments[i].node, HierarchyNode::Source::Attachment, i);
    for (int i = 0; i < (int)model.lights.size(); i++)
        addNode(model.lights[i].node, HierarchyNode::Source::Light, i);

    // Build objectId → index map
    for (int i = 0; i < (int)nodes_.size(); i++)
        objectIdToIdx_[nodes_[i].objectId] = i;

    // Resolve parentIdx from parentId (objectId → index)
    for (auto& n : nodes_) {
        // Find parent node in original model nodes
        uint32_t parentId = Node::NO_PARENT;
        // We need to look up the parentId from the original source node.
        // Since we stored pointers to tracks, we can infer the source.
        // But it's cleaner to just do a second pass using the model.
        n.parentIdx = -1; // default: root
    }

    // Second pass: resolve parent objectIds from model data
    auto resolveParent = [&](const Node& srcNode, int idx) {
        if (srcNode.parentId != Node::NO_PARENT) {
            auto it = objectIdToIdx_.find((int)srcNode.parentId);
            if (it != objectIdToIdx_.end())
                nodes_[idx].parentIdx = it->second;
        }
    };

    int offset = 0;
    for (int i = 0; i < (int)model.bones.size(); i++)
        resolveParent(model.bones[i].node, offset + i);
    offset += (int)model.bones.size();
    for (int i = 0; i < (int)model.helpers.size(); i++)
        resolveParent(model.helpers[i].node, offset + i);
    offset += (int)model.helpers.size();
    for (int i = 0; i < (int)model.particleEmitters.size(); i++)
        resolveParent(model.particleEmitters[i].node, offset + i);
    offset += (int)model.particleEmitters.size();
    for (int i = 0; i < (int)model.particleEmitters2.size(); i++)
        resolveParent(model.particleEmitters2[i].node, offset + i);
    offset += (int)model.particleEmitters2.size();
    for (int i = 0; i < (int)model.ribbonEmitters.size(); i++)
        resolveParent(model.ribbonEmitters[i].node, offset + i);
    offset += (int)model.ribbonEmitters.size();
    for (int i = 0; i < (int)model.collisionShapes.size(); i++)
        resolveParent(model.collisionShapes[i].node, offset + i);
    offset += (int)model.collisionShapes.size();
    for (int i = 0; i < (int)model.attachments.size(); i++)
        resolveParent(model.attachments[i].node, offset + i);
    offset += (int)model.attachments.size();
    for (int i = 0; i < (int)model.lights.size(); i++)
        resolveParent(model.lights[i].node, offset + i);

    // Topological sort: parent always before child
    // Simple approach: repeated passes until stable (works for small hierarchies)
    std::vector<int> order(nodes_.size());
    std::iota(order.begin(), order.end(), 0);

    // Kahn's algorithm on the parent graph
    std::vector<int> inDegree(nodes_.size(), 0);
    for (auto& n : nodes_)
        if (n.parentIdx >= 0) inDegree[&n - nodes_.data()]++;

    std::vector<int> sorted;
    sorted.reserve(nodes_.size());
    // Roots first
    for (int i = 0; i < (int)nodes_.size(); i++)
        if (inDegree[i] == 0) sorted.push_back(i);

    for (int head = 0; head < (int)sorted.size(); head++) {
        int cur = sorted[head];
        for (int i = 0; i < (int)nodes_.size(); i++) {
            if (nodes_[i].parentIdx == cur) {
                sorted.push_back(i);
            }
        }
    }

    // Remap nodes to sorted order
    if ((int)sorted.size() == (int)nodes_.size()) {
        std::vector<int> newIdx(nodes_.size());
        for (int i = 0; i < (int)sorted.size(); i++)
            newIdx[sorted[i]] = i;

        std::vector<HierarchyNode> sortedNodes(nodes_.size());
        for (int i = 0; i < (int)sorted.size(); i++) {
            sortedNodes[i] = nodes_[sorted[i]];
            if (sortedNodes[i].parentIdx >= 0)
                sortedNodes[i].parentIdx = newIdx[sortedNodes[i].parentIdx];
        }
        nodes_ = std::move(sortedNodes);

        // Rebuild objectId map
        objectIdToIdx_.clear();
        for (int i = 0; i < (int)nodes_.size(); i++)
            objectIdToIdx_[nodes_[i].objectId] = i;
    }
}

// ============================================================================
// MdxHierarchy::Evaluate
// ============================================================================

void MdxHierarchy::Evaluate(int timeMs, int seqStart, int seqEnd,
                             const std::vector<u32>& globalSequences,
                             std::vector<XMMATRIX>& boneWorldMatrices,
                             std::vector<XMMATRIX>& allNodeMatrices,
                             const XMFLOAT3* cameraPos,
                             int globalTimeMs) const {
    int nc = (int)nodes_.size();
    allNodeMatrices.resize(nc);

    static const Vector3f defaultT = {0, 0, 0};
    static const Quaternion defaultR = {0, 0, 0, 1};  // identity
    static const Vector3f defaultS = {1, 1, 1};

    // Cache each node's evaluated local TRS so children with DontInherit flags
    // can rebuild their parent's contribution selectively without having to
    // decompose a delta matrix (decompose can't cleanly separate rotation from
    // the pivot-offset encoded in the delta's translation column).
    std::vector<Vector3f>   localTs(nc);
    std::vector<Quaternion> localRs(nc);
    std::vector<Vector3f>   localSs(nc);

    for (int i = 0; i < nc; i++) {
        const auto& n = nodes_[i];

        // Compute effective time per track, handling global sequences independently
        auto getEffTime = [&](const auto* track, int& et, int& es, int& ee) {
            et = timeMs; es = seqStart; ee = seqEnd;
            if (track && track->isUsed && track->globalSequenceId != whiteout::mdx::Track<whiteout::f32>::kNoGlobalSequence) {
                u32 gsId = track->globalSequenceId;
                if (gsId < (u32)globalSequences.size()) {
                    u32 duration = globalSequences[gsId];
                    if (duration > 0) {
                        es = 0;
                        ee = (int)duration;
                        int gsTime = (globalTimeMs >= 0) ? globalTimeMs : timeMs;
                        et = (int)std::fmod((float)gsTime, (float)duration);
                    }
                }
            }
        };

        int tTime, tStart, tEnd;
        getEffTime(n.translation, tTime, tStart, tEnd);
        Vector3f localT = n.translation
            ? EvaluateTrackVec3(*n.translation, tTime, tStart, tEnd, defaultT)
            : defaultT;

        int rTime, rStart, rEnd;
        getEffTime(n.rotation, rTime, rStart, rEnd);
        Quaternion localR = n.rotation
            ? EvaluateTrackQuat(*n.rotation, rTime, rStart, rEnd, defaultR)
            : defaultR;

        int sTime, sStart, sEnd;
        getEffTime(n.scaling, sTime, sStart, sEnd);
        Vector3f localS = n.scaling
            ? EvaluateTrackVec3(*n.scaling, sTime, sStart, sEnd, defaultS)
            : defaultS;

        localTs[i] = localT;
        localRs[i] = localR;
        localSs[i] = localS;

        XMMATRIX localM = Vec3QuatScaleToXMMatrix(localT, localR, localS, n.pivot);

        // Parent composition
        XMMATRIX parentWorld = XMMatrixIdentity();
        if (n.parentIdx >= 0 && n.parentIdx < nc) {
            parentWorld = allNodeMatrices[n.parentIdx];

            // DontInherit flags: rebuild the direct parent's contribution with
            // the flagged TRS components zeroed out, using the parent's cached
            // local TRS (NOT its delta matrix, which encodes rotation effect
            // on pivot inseparably from translation). Ancestors above the
            // parent are unaffected — only the direct parent's contribution is
            // filtered, matching typical Wc3 usage where the flag decouples a
            // node (e.g. a billboard emitter) from its direct parent bone.
            uint32_t flags = n.flags;
            using NF = Node::NodeFlag;
            if (flags & ((uint32_t)NF::DontInheritTranslation |
                         (uint32_t)NF::DontInheritRotation |
                         (uint32_t)NF::DontInheritScaling)) {
                const auto& parent = nodes_[n.parentIdx];
                Vector3f   pT = localTs[n.parentIdx];
                Quaternion pR = localRs[n.parentIdx];
                Vector3f   pS = localSs[n.parentIdx];

                if (flags & (uint32_t)NF::DontInheritTranslation) pT = {0, 0, 0};
                if (flags & (uint32_t)NF::DontInheritRotation)    pR = {0, 0, 0, 1};
                if (flags & (uint32_t)NF::DontInheritScaling)     pS = {1, 1, 1};

                XMMATRIX filteredParentLocal =
                    Vec3QuatScaleToXMMatrix(pT, pR, pS, parent.pivot);

                XMMATRIX grandparentWorld = XMMatrixIdentity();
                if (parent.parentIdx >= 0 && parent.parentIdx < nc)
                    grandparentWorld = allNodeMatrices[parent.parentIdx];

                parentWorld = filteredParentLocal * grandparentWorld;
            }
        }

        XMMATRIX worldM = localM * parentWorld;

        // NOTE: Billboard rotation is applied in Renderer::ApplyFrameState()
        // using billboardFlags, NOT here. This ensures billboarding works
        // uniformly for both Max and MDX adapter paths.

        allNodeMatrices[i] = worldM;
    }

    // Extract bone world matrices (bones are the first boneCount_ entries by source)
    boneWorldMatrices.resize(boneCount_);
    for (int i = 0; i < nc; i++) {
        if (nodes_[i].source == HierarchyNode::Source::Bone &&
            nodes_[i].sourceIndex < boneCount_) {
            boneWorldMatrices[nodes_[i].sourceIndex] = allNodeMatrices[i];
        }
    }
}

// ============================================================================
// MdxHierarchy::ObjectIdToNodeIndex
// ============================================================================

int MdxHierarchy::ObjectIdToNodeIndex(int objectId) const {
    auto it = objectIdToIdx_.find(objectId);
    return it != objectIdToIdx_.end() ? it->second : -1;
}

} // namespace WhiteoutDex
