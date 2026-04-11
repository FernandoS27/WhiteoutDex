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
// Helper: find bracketing keyframes for a given time
// Returns pair(lo, hi) indices. If time <= first key, returns (0,0).
// If time >= last key, returns (last,last).
// ============================================================================

template<typename KeyType>
static std::pair<int, int> FindBracket(const KeyType* keys, int count, int timeMs) {
    if (count == 0) return {-1, -1};
    if (count == 1 || timeMs <= (int)keys[0].frame) return {0, 0};
    if (timeMs >= (int)keys[count - 1].frame) return {count - 1, count - 1};
    for (int i = 0; i < count - 1; i++) {
        if (timeMs >= (int)keys[i].frame && timeMs < (int)keys[i + 1].frame)
            return {i, i + 1};
    }
    return {count - 1, count - 1};
}

template<typename KeyType>
static float InterpolationFactor(const KeyType* keys, int lo, int hi) {
    if (lo == hi) return 0.0f;
    float range = (float)(keys[hi].frame - keys[lo].frame);
    return range > 0 ? 1.0f : 0.0f; // placeholder — caller provides time
}

static float CalcT(uint32_t frameLo, uint32_t frameHi, int timeMs) {
    if (frameLo == frameHi) return 0.0f;
    float t = (float)(timeMs - (int)frameLo) / (float)((int)frameHi - (int)frameLo);
    return std::clamp(t, 0.0f, 1.0f);
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
        auto [lo, hi] = FindBracket(keys.data(), count, timeMs);
        if (lo < 0) return defaultVal;
        if (lo == hi) return keys[lo].value;
        if (interp == InterpolationType::None) return keys[lo].value;
        float t = CalcT(keys[lo].frame, keys[hi].frame, timeMs);
        return keys[lo].value + (keys[hi].value - keys[lo].value) * t;
    } else {
        auto keys = const_cast<Track<f32>&>(track).tangentKeys();
        int count = (int)keys.size();
        auto [lo, hi] = FindBracket(keys.data(), count, timeMs);
        if (lo < 0) return defaultVal;
        if (lo == hi) return keys[lo].value;
        float t = CalcT(keys[lo].frame, keys[hi].frame, timeMs);
        if (interp == InterpolationType::Hermite)
            return HermiteInterp(keys[lo].value, keys[lo].outTan, keys[hi].inTan, keys[hi].value, t);
        else // Bezier
            return BezierInterp(keys[lo].value, keys[lo].outTan, keys[hi].inTan, keys[hi].value, t);
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
    // Find last key <= timeMs
    u32 val = keys[0].value;
    for (int i = 0; i < count; i++) {
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
        auto [lo, hi] = FindBracket(keys.data(), count, timeMs);
        if (lo < 0) return defaultVal;
        if (lo == hi) return keys[lo].value;
        if (interp == InterpolationType::None) return keys[lo].value;
        float t = CalcT(keys[lo].frame, keys[hi].frame, timeMs);
        return Vector3f::lerp(keys[lo].value, keys[hi].value, t);
    } else {
        auto keys = const_cast<Track<Vector3f>&>(track).tangentKeys();
        int count = (int)keys.size();
        auto [lo, hi] = FindBracket(keys.data(), count, timeMs);
        if (lo < 0) return defaultVal;
        if (lo == hi) return keys[lo].value;
        float t = CalcT(keys[lo].frame, keys[hi].frame, timeMs);
        if (interp == InterpolationType::Hermite)
            return HermiteInterpV3(keys[lo].value, keys[lo].outTan, keys[hi].inTan, keys[hi].value, t);
        else
            return BezierInterpV3(keys[lo].value, keys[lo].outTan, keys[hi].inTan, keys[hi].value, t);
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
        auto [lo, hi] = FindBracket(keys.data(), count, timeMs);
        if (lo < 0) return defaultVal;
        if (lo == hi) return keys[lo].value;
        if (interp == InterpolationType::None) return keys[lo].value;
        float t = CalcT(keys[lo].frame, keys[hi].frame, timeMs);
        return Quaternion::slerp(keys[lo].value, keys[hi].value, t);
    } else {
        auto keys = const_cast<Track<Quaternion>&>(track).tangentKeys();
        int count = (int)keys.size();
        auto [lo, hi] = FindBracket(keys.data(), count, timeMs);
        if (lo < 0) return defaultVal;
        if (lo == hi) return keys[lo].value;
        float t = CalcT(keys[lo].frame, keys[hi].frame, timeMs);
        // For hermite/bezier quaternion tracks, use squad
        return Quaternion::squad(keys[lo].value, keys[lo].outTan, keys[hi].inTan, keys[hi].value, t);
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
                             const XMFLOAT3* cameraPos) const {
    int nc = (int)nodes_.size();
    allNodeMatrices.resize(nc);

    static const Vector3f defaultT = {0, 0, 0};
    static const Quaternion defaultR = {0, 0, 0, 1};  // identity
    static const Vector3f defaultS = {1, 1, 1};

    for (int i = 0; i < nc; i++) {
        const auto& n = nodes_[i];

        // Compute effective time per track, handling global sequences independently
        auto getEffTime = [&](const auto* track, int& et, int& es, int& ee) {
            et = timeMs; es = seqStart; ee = seqEnd;
            if (track && track->isUsed && track->globalSequenceId != 0xFFFFFFFF) {
                u32 gsId = track->globalSequenceId;
                if (gsId < (u32)globalSequences.size()) {
                    u32 duration = globalSequences[gsId];
                    if (duration > 0) {
                        es = 0;
                        ee = (int)duration;
                        et = (int)std::fmod((float)timeMs, (float)duration);
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

        XMMATRIX localM = Vec3QuatScaleToXMMatrix(localT, localR, localS, n.pivot);

        // Parent composition
        XMMATRIX parentWorld = XMMatrixIdentity();
        if (n.parentIdx >= 0 && n.parentIdx < nc)
            parentWorld = allNodeMatrices[n.parentIdx];

        // DontInherit flags (rare but needed for correctness)
        uint32_t flags = n.flags;
        using NF = Node::NodeFlag;
        if (flags & (uint32_t)NF::DontInheritTranslation ||
            flags & (uint32_t)NF::DontInheritRotation ||
            flags & (uint32_t)NF::DontInheritScaling) {
            // Decompose parent
            XMVECTOR pS, pR, pT;
            if (XMMatrixDecompose(&pS, &pR, &pT, parentWorld)) {
                XMVECTOR one  = XMVectorSet(1,1,1,1);
                XMVECTOR zero = XMVectorSet(0,0,0,0);
                XMVECTOR idR  = XMQuaternionIdentity();

                if (flags & (uint32_t)NF::DontInheritTranslation) pT = zero;
                if (flags & (uint32_t)NF::DontInheritRotation)    pR = idR;
                if (flags & (uint32_t)NF::DontInheritScaling)     pS = one;

                parentWorld = XMMatrixScalingFromVector(pS) *
                              XMMatrixRotationQuaternion(pR) *
                              XMMatrixTranslationFromVector(pT);
            }
        }

        XMMATRIX worldM = localM * parentWorld;

        // Billboard: override rotation to face the camera.
        // Warcraft 3 billboarding preserves the node's world position and scale
        // but replaces its rotation so it faces the camera.
        // Coordinate system: Z-up, right-handed.
        if (cameraPos && (flags & ((uint32_t)NF::Billboarded |
                                   (uint32_t)NF::BillboardedLockX |
                                   (uint32_t)NF::BillboardedLockY |
                                   (uint32_t)NF::BillboardedLockZ))) {
            XMVECTOR wS, wR, wT;
            if (XMMatrixDecompose(&wS, &wR, &wT, worldM)) {
                XMVECTOR camP = XMLoadFloat3(cameraPos);
                XMVECTOR toCamera = XMVectorSubtract(camP, wT);
                float dist = XMVectorGetX(XMVector3Length(toCamera));

                if (dist > 0.001f) {
                    XMVECTOR worldUp = XMVectorSet(0, 0, 1, 0);

                    if (flags & (uint32_t)NF::Billboarded) {
                        // Full billboard: build look-at basis facing camera
                        XMVECTOR fwd = XMVector3Normalize(toCamera);
                        XMVECTOR right = XMVector3Cross(fwd, worldUp);
                        float rightLen = XMVectorGetX(XMVector3Length(right));

                        if (rightLen < 0.001f) {
                            // Camera directly above/below — use arbitrary right
                            right = XMVectorSet(1, 0, 0, 0);
                        }
                        right = XMVector3Normalize(right);
                        XMVECTOR up = XMVector3Normalize(XMVector3Cross(right, fwd));

                        // Row-major XMMATRIX: row0=X(right), row1=Y(fwd), row2=Z(up)
                        XMMATRIX billboardRot = XMMATRIX(
                            right,
                            fwd,
                            up,
                            XMVectorSet(0, 0, 0, 1)
                        );
                        worldM = XMMatrixScalingFromVector(wS) * billboardRot *
                                 XMMatrixTranslationFromVector(wT);

                    } else if (flags & (uint32_t)NF::BillboardedLockZ) {
                        // Lock Z: rotate only around world Z to face camera (yaw only)
                        XMFLOAT3 tc;
                        XMStoreFloat3(&tc, toCamera);
                        float yaw = atan2f(tc.y, tc.x);
                        worldM = XMMatrixScalingFromVector(wS) *
                                 XMMatrixRotationZ(yaw) *
                                 XMMatrixTranslationFromVector(wT);

                    } else if (flags & (uint32_t)NF::BillboardedLockY) {
                        // Lock Y: rotate only around world Y to face camera
                        XMFLOAT3 tc;
                        XMStoreFloat3(&tc, toCamera);
                        float angle = atan2f(tc.z, tc.x);
                        worldM = XMMatrixScalingFromVector(wS) *
                                 XMMatrixRotationY(angle) *
                                 XMMatrixTranslationFromVector(wT);

                    } else if (flags & (uint32_t)NF::BillboardedLockX) {
                        // Lock X: rotate only around world X to face camera
                        XMFLOAT3 tc;
                        XMStoreFloat3(&tc, toCamera);
                        float angle = atan2f(tc.z, tc.y);
                        worldM = XMMatrixScalingFromVector(wS) *
                                 XMMatrixRotationX(angle) *
                                 XMMatrixTranslationFromVector(wT);
                    }
                }
            }
        }

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
