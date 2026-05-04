// ============================================================================
// MDX Animation Evaluator — Track interpolation + bone hierarchy evaluation
// ============================================================================

#include "mdx_animation.h"
#include <algorithm>

using namespace whiteout;
using namespace whiteout::mdx;

namespace WhiteoutDex {

namespace {

Quaternion Wc3Slerp(const Quaternion& a, const Quaternion& b, float t) {
    f32 d = a.dot(b);
    d = std::clamp(d, -1.0f, 1.0f);

    // If the dot product is negative, slerp won't take the shorter path.
    // Fix by reversing one quaternion.
    Quaternion end = b;
    if (d < 0.0f) {
        d = -d;
        end.x = -end.x;
        end.y = -end.y;
        end.z = -end.z;
        end.w = -end.w;
    }

    // This is the key difference of Wc3's slerp from a standard implementation: the dot product 
    // threshold for switching to linear interpolation is much higher (0.9 vs ~0.9995).
    // Compute the cosine of the angle between the two quaternions
    const f32 DOT_THRESHOLD = 0.9f;
    if (d > DOT_THRESHOLD) {
        // If the quaternions are close, use linear interpolation
        Quaternion result = Quaternion(
            a.x + t * (end.x - a.x),
            a.y + t * (end.y - a.y),
            a.z + t * (end.z - a.z),
            a.w + t * (end.w - a.w)
        );
        return result.normalized();
    }

    // Calculate the angle between the quaternions
    f32 theta_0 = std::acos(d); // angle between input quaternions
    f32 theta = theta_0 * t;    // angle between a and result

    f32 sin_theta = std::sin(theta);
    f32 sin_theta_0 = std::sin(theta_0);

    if (sin_theta_0 < 1e-6f)
        return a;

    f32 s0 = std::sin(theta_0 - theta) / sin_theta_0;
    f32 s1 = sin_theta / sin_theta_0;

    return Quaternion(
        a.x * s0 + end.x * s1,
        a.y * s0 + end.y * s1,
        a.z * s0 + end.z * s1,
        a.w * s0 + end.w * s1
    );
}

Quaternion Wc3Squad(const Quaternion& start, const Quaternion& outtan,
                             const Quaternion& inttan, const Quaternion& end, f32 t) {
    Quaternion slerp1 = Wc3Slerp(start, end, t);
    Quaternion slerp2 = Wc3Slerp(outtan, inttan, t);
    return Wc3Slerp(slerp1, slerp2, 2 * t * (1 - t));
}


} // namespace

// ============================================================================
// FindBracket — locate in-sequence keyframes bracketing timeMs.
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

    // Binary search: first key with frame >= seqStart (lower_bound of seqStart).
    {
        int lo = 0, hi = count;
        while (lo < hi) { int m = (lo + hi) >> 1; if ((int)keys[m].frame < seqStart) lo = m + 1; else hi = m; }
        b.lo = lo; // reuse b.lo as rangeLo temporarily
    }
    int rangeLo = b.lo;
    if (rangeLo >= count || (int)keys[rangeLo].frame > seqEnd) { b.lo = -1; return b; }

    // Binary search: last key with frame <= seqEnd (upper_bound of seqEnd, minus 1).
    {
        int lo = rangeLo, hi = count;
        while (lo < hi) { int m = (lo + hi) >> 1; if ((int)keys[m].frame <= seqEnd) lo = m + 1; else hi = m; }
        b.hi = lo - 1; // reuse b.hi as rangeHi temporarily
    }
    int rangeHi = b.hi;

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
        if (segLen <= 0) return b;
        int pos = (timeMs >= lastFrame)
            ? timeMs - lastFrame
            : (timeMs - seqStart) + (seqEnd - lastFrame);
        b.t = std::clamp((float)pos / (float)segLen, 0.0f, 1.0f);
        return b;
    }

    // Binary search: last key in [rangeLo, rangeHi) with frame <= timeMs.
    // timeMs >= firstFrame and timeMs < lastFrame guarantee a valid bracket exists.
    {
        int lo = rangeLo, hi = rangeHi;
        while (lo < hi) { int m = (lo + hi + 1) >> 1; if ((int)keys[m].frame <= timeMs) lo = m; else hi = m - 1; }
        b.lo = lo;
        b.hi = lo + 1;
        int denom = (int)keys[b.hi].frame - (int)keys[b.lo].frame;
        b.t = denom > 0 ? (float)(timeMs - (int)keys[b.lo].frame) / (float)denom : 0.0f;
    }
    return b;
}

// ============================================================================
// Scalar tangent-interp primitives + componentwise V3 dispatcher
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

template<typename ScalarFn>
static Vector3f ApplyV3(ScalarFn fn, const Vector3f& a, const Vector3f& ota,
                        const Vector3f& itb, const Vector3f& b, float t) {
    return {fn(a.x, ota.x, itb.x, b.x, t),
            fn(a.y, ota.y, itb.y, b.y, t),
            fn(a.z, ota.z, itb.z, b.z, t)};
}

// ============================================================================
// EvaluateTrackImpl — shared track interpolation for F32/Vec3/Quat.
//   lerp(a, b, t)                                 — linear step
//   curve(a, outTanA, inTanB, b, t, interpType)   — hermite/bezier/squad
// ============================================================================

template<typename T, typename LerpFn, typename CurveFn>
static T EvaluateTrackImpl(const Track<T>& track, int timeMs, int seqStart, int seqEnd,
                           const T& defaultVal, LerpFn lerp, CurveFn curve,
                           bool forceNoInterp = false) {
    if (!track.isUsed || track.keyCount == 0) return defaultVal;

    const InterpolationType interp = track.interpolationType;
    const bool useCurve = (interp == InterpolationType::Hermite ||
                           interp == InterpolationType::Bezier);

    auto& mut = const_cast<Track<T>&>(track);
    if (useCurve) {
        auto keys = mut.tangentKeys();
        auto br = FindBracket(keys.data(), (int)keys.size(), timeMs, seqStart, seqEnd);
        if (br.lo < 0) return defaultVal;
        if (br.lo == br.hi || forceNoInterp) return keys[br.lo].value;
        return curve(keys[br.lo].value, keys[br.lo].outTan,
                     keys[br.hi].inTan, keys[br.hi].value, br.t, interp);
    }
    auto keys = mut.keys();
    auto br = FindBracket(keys.data(), (int)keys.size(), timeMs, seqStart, seqEnd);
    if (br.lo < 0) return defaultVal;
    if (br.lo == br.hi || forceNoInterp || interp == InterpolationType::None)
        return keys[br.lo].value;
    return lerp(keys[br.lo].value, keys[br.hi].value, br.t);
}

float EvaluateTrackF32(const Track<f32>& track, int timeMs, int seqStart, int seqEnd,
                       float defaultVal, bool forceNoInterp) {
    return EvaluateTrackImpl<f32>(track, timeMs, seqStart, seqEnd, defaultVal,
        [](float a, float b, float t) { return a + (b - a) * t; },
        [](float a, float ota, float itb, float b, float t, InterpolationType it) {
            return it == InterpolationType::Hermite
                ? HermiteInterp(a, ota, itb, b, t)
                : BezierInterp(a, ota, itb, b, t);
        },
        forceNoInterp);
}

Vector3f EvaluateTrackVec3(const Track<Vector3f>& track, int timeMs, int seqStart, int seqEnd,
                           Vector3f defaultVal) {
    return EvaluateTrackImpl<Vector3f>(track, timeMs, seqStart, seqEnd, defaultVal,
        [](const Vector3f& a, const Vector3f& b, float t) { return Vector3f::lerp(a, b, t); },
        [](const Vector3f& a, const Vector3f& ota, const Vector3f& itb,
           const Vector3f& b, float t, InterpolationType it) {
            return it == InterpolationType::Hermite
                ? ApplyV3(HermiteInterp, a, ota, itb, b, t)
                : ApplyV3(BezierInterp, a, ota, itb, b, t);
        });
}

Quaternion EvaluateTrackQuat(const Track<Quaternion>& track, int timeMs, int seqStart, int seqEnd,
                             Quaternion defaultVal) {
    return EvaluateTrackImpl<Quaternion>(track, timeMs, seqStart, seqEnd, defaultVal,
        [](const Quaternion& a, const Quaternion& b, float t) { return Wc3Slerp(a, b, t); },
        [](const Quaternion& a, const Quaternion& ota, const Quaternion& itb,
           const Quaternion& b, float t, InterpolationType) {
            return Wc3Squad(a, ota, itb, b, t);
        });
}

// ============================================================================
// EvaluateTrackU32 — u32 tracks are always step (hold most recent key ≤ timeMs).
// ============================================================================

u32 EvaluateTrackU32(const Track<u32>& track, int timeMs, int seqStart, int seqEnd, u32 defaultVal) {
    if (!track.isUsed || track.keyCount == 0) return defaultVal;
    auto keys = const_cast<Track<u32>&>(track).keys();
    int count = (int)keys.size();
    if (count == 0) return defaultVal;

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
// BindPose3x4ToMatrix44f: 3x4 row-major (a00..a23) → 4x4 Matrix44f
// ============================================================================

Matrix44f BindPose3x4ToMatrix44f(const std::array<f32, 12>& bp) {
    Matrix44f m{};
    m.data[0] = {bp[0], bp[1], bp[2],  0.0f};
    m.data[1] = {bp[3], bp[4], bp[5],  0.0f};
    m.data[2] = {bp[6], bp[7], bp[8],  0.0f};
    m.data[3] = {bp[9], bp[10],bp[11], 1.0f};
    return m;
}

// ============================================================================
// Vec3QuatScaleToMatrix44f: Compose T/R/S around a pivot point into Matrix44f.
//   M = Translate(-pivot) * Scale * Rotate * Translate(pivot) * Translate(t)
// rotation() is column-vector convention; transpose for row-vector order.
// ============================================================================

Matrix44f Vec3QuatScaleToMatrix44f(const Vector3f& t, const Quaternion& r,
                                    const Vector3f& s, const Vector3f& pivot) {
    Matrix44f mS = Matrix44f::scaling(s);
    Matrix44f mR = Matrix44f::rotation(r).transpose();
    Matrix44f mNegPiv  = Matrix44f::translation({-pivot.x, -pivot.y, -pivot.z});
    Matrix44f mPosPivT = Matrix44f::translation({pivot.x + t.x, pivot.y + t.y, pivot.z + t.z});
    return mNegPiv * mS * mR * mPosPivT;
}

// ============================================================================
// MdxHierarchy::Build
// ============================================================================

void MdxHierarchy::Build(const whiteout::mdx::Model& model) {
    nodes_.clear();
    objectIdToIdx_.clear();
    boneCount_ = (int)model.bones.size();

    // Parallel vector to hold each node's source-level parent objectId during
    // Build; resolved to parentIdx after objectIdToIdx_ is populated.
    std::vector<uint32_t> parentIds;

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
        parentIds.push_back(node.parentId);
    };

    auto addAll = [&](const auto& arr, HierarchyNode::Source src) {
        for (size_t i = 0; i < arr.size(); i++)
            addNode(arr[i].node, src, (int)i);
    };

    addAll(model.bones,             HierarchyNode::Source::Bone);
    addAll(model.helpers,           HierarchyNode::Source::Helper);
    addAll(model.particleEmitters,  HierarchyNode::Source::ParticleEmitter);
    addAll(model.particleEmitters2, HierarchyNode::Source::ParticleEmitter2);
    addAll(model.ribbonEmitters,    HierarchyNode::Source::RibbonEmitter);
    addAll(model.collisionShapes,   HierarchyNode::Source::CollisionShape);
    addAll(model.attachments,       HierarchyNode::Source::Attachment);
    addAll(model.lights,            HierarchyNode::Source::Light);
    // EventObjects need their own palette slot so the per-actor
    // EventEmitterPool can resolve `objectId → world matrix` for splat /
    // SPN spawns. Without this, ObjectIdToNodeIndex returns -1 and
    // EventObjects collapse onto actor.worldTransform — which makes
    // every footprint spawn at the model origin instead of under the
    // foot bone the EventObject was authored to ride on.
    addAll(model.eventObjects,      HierarchyNode::Source::EventObject);

    for (int i = 0; i < (int)nodes_.size(); i++)
        objectIdToIdx_[nodes_[i].objectId] = i;

    for (size_t i = 0; i < nodes_.size(); i++) {
        if (parentIds[i] == Node::NO_PARENT) continue;
        auto it = objectIdToIdx_.find((int)parentIds[i]);
        if (it != objectIdToIdx_.end()) nodes_[i].parentIdx = it->second;
    }

    // Topological sort by BFS from roots. Each node has at most one parent, so
    // Kahn's in-degree table collapses to a parentIdx<0 root check.
    std::vector<int> sorted;
    sorted.reserve(nodes_.size());
    for (int i = 0; i < (int)nodes_.size(); i++)
        if (nodes_[i].parentIdx < 0) sorted.push_back(i);

    for (int head = 0; head < (int)sorted.size(); head++) {
        int cur = sorted[head];
        for (int i = 0; i < (int)nodes_.size(); i++)
            if (nodes_[i].parentIdx == cur) sorted.push_back(i);
    }

    // Remap nodes to sorted order (parent always before child).
    if (sorted.size() == nodes_.size()) {
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

        objectIdToIdx_.clear();
        for (int i = 0; i < (int)nodes_.size(); i++)
            objectIdToIdx_[nodes_[i].objectId] = i;
    }

    // Dense bone-index → hierarchy-position table. MATS/SKIN values in Previewd
    // are dense bone indices; we resolve them through this table to our palette
    // position.
    boneIdxToNodeIdx_.assign(boneCount_, -1);
    for (int i = 0; i < (int)nodes_.size(); i++) {
        const auto& n = nodes_[i];
        if (n.source == HierarchyNode::Source::Bone &&
            n.sourceIndex >= 0 && n.sourceIndex < boneCount_) {
            boneIdxToNodeIdx_[n.sourceIndex] = i;
        }
    }
}

// ============================================================================
// MdxHierarchy::Evaluate
// ============================================================================

void MdxHierarchy::Evaluate(int timeMs, int seqStart, int seqEnd,
                             const std::vector<u32>& globalSequences,
                             std::vector<Matrix44f>& boneWorldMatrices,
                             std::vector<Matrix44f>& allNodeMatrices,
                             const Vector3f* cameraPos,
                             int globalTimeMs) const {
    int nc = (int)nodes_.size();
    allNodeMatrices.resize(nc);

    static const Vector3f   defaultT = {0, 0, 0};
    static const Quaternion defaultR = {0, 0, 0, 1};
    static const Vector3f   defaultS = {1, 1, 1};

    // Cache each node's post-TRS stack top — the world-matrix-stack state
    // BEFORE PlaceObjectSimple's T(-pivot) is applied. Children left-multiply
    // their T/R/S onto this parent snapshot to rebuild Previewd's
    // WorldMatrixPush chain.
    std::vector<Matrix44f> stackM(nc, Matrix44f::identity());

    // Per-track effective time, remapped into a global sequence's own [0, duration]
    // window when globalSequenceId is set. duration==0 means a static global seq:
    // hold the sole key permanently by returning a wide bracket range.
    struct EffTime { int time; int start; int end; };
    auto effTime = [&](const auto* track) -> EffTime {
        EffTime e{timeMs, seqStart, seqEnd};
        if (!track || !track->isUsed) return e;
        if (track->globalSequenceId == Track<f32>::kNoGlobalSequence) return e;
        u32 gsId = track->globalSequenceId;
        if (gsId >= (u32)globalSequences.size()) return e;
        u32 duration = globalSequences[gsId];
        if (duration == 0) {
            e.start = 0; e.end = 0x3FFFFFFF; e.time = 0;
            return e;
        }
        int gsTime = (globalTimeMs >= 0) ? globalTimeMs : timeMs;
        e.start = 0;
        e.end   = (int)duration;
        e.time  = (int)std::fmod((float)gsTime, (float)duration);
        return e;
    };

    auto evalVec3 = [&](const Track<Vector3f>* tr, const Vector3f& def) {
        if (!tr) return def;
        auto e = effTime(tr);
        return EvaluateTrackVec3(*tr, e.time, e.start, e.end, def);
    };
    auto evalQuat = [&](const Track<Quaternion>* tr, const Quaternion& def) {
        if (!tr) return def;
        auto e = effTime(tr);
        return EvaluateTrackQuat(*tr, e.time, e.start, e.end, def);
    };

    for (int i = 0; i < nc; i++) {
        const auto& n = nodes_[i];

        const Vector3f   localT = evalVec3(n.translation, defaultT);
        const Quaternion localR = evalQuat(n.rotation,    defaultR);
        const Vector3f   localS = evalVec3(n.scaling,     defaultS);

        // Replicate Previewd's per-bone world-matrix-stack pipeline. Strips
        // are dispatched from the specific view that owns each flag combo —
        // summarized below (flags bit 0/1/2 = rmT/rmR/rmS in our MDX enum):
        //   001 rmT only      → TranslateView : RemoveTranslation
        //   010 rmS only      → ScaleView     : RemoveScale (before local S)
        //   011 rmT+rmS       → TranslateView : RemoveTranslation + RemoveScale
        //   100 rmR only      → RotateView    : RemoveRotation (before local R)
        //   101 rmT+rmR       → TranslateView : RemoveTranslation + RemoveRotation
        //   110 rmR+rmS       → ScaleView     : RemoveRotationAndScaling (before local S)
        //   111 all           → TranslateView : RemoveTranslation + RemoveRotationAndScaling
        //
        // The model basis is identity (model-to-world is applied outside the
        // bone chain), so Previewd's WorldMatrixBasis / WorldMatrixScale
        // (basisScale) / WorldMatrixTranslate(basisPosition) restore calls
        // are all no-ops here.
        //
        // Note: in rmR+rmS (no rmT), the rotation+scale strip in ScaleView
        // happens AFTER RotateView applied the local rotation, so the strip
        // wipes both the accumulated and just-applied local rotation,
        // leaving only the local scale. This is Previewd's actual behaviour.
        const uint32_t flags = n.flags;
        using NF = Node::NodeFlag;
        const bool rmT = flags & (uint32_t)NF::DontInheritTranslation;
        const bool rmR = flags & (uint32_t)NF::DontInheritRotation;
        const bool rmS = flags & (uint32_t)NF::DontInheritScaling;

        const Vector3f& currPivot = n.pivot;
        Vector3f parentPivot = {0, 0, 0};
        Matrix44f M = Matrix44f::identity();
        if (n.parentIdx >= 0 && n.parentIdx < nc) {
            M = stackM[n.parentIdx];
            parentPivot = nodes_[n.parentIdx].pivot;
        }

        // M := T(t) * M (left-multiply): row3 += t expressed in M's basis.
        // Matches Previewd's WorldMatrixTranslate:
        //   d0 += move.x * a0 + move.y * b0 + move.z * c0
        auto applyTranslate = [&](float tx, float ty, float tz) {
            M.data[3][0] += tx * M.data[0][0] + ty * M.data[1][0] + tz * M.data[2][0];
            M.data[3][1] += tx * M.data[0][1] + ty * M.data[1][1] + tz * M.data[2][1];
            M.data[3][2] += tx * M.data[0][2] + ty * M.data[1][2] + tz * M.data[2][2];
        };

        // MatrixRemove variants (Previewd's RemoveX ops on the rotation rows).
        auto stripTranslation = [&]() {
            M.data[3][0] = 0; M.data[3][1] = 0; M.data[3][2] = 0;
        };
        auto stripRotationKeepScale = [&]() {
            // flag 4: axis-align rows, preserving row magnitudes (= scale).
            for (int r = 0; r < 3; ++r) {
                float mx = M.data[r][0], my = M.data[r][1], mz = M.data[r][2];
                float mag = std::sqrt(mx*mx + my*my + mz*mz);
                M.data[r][0] = 0; M.data[r][1] = 0; M.data[r][2] = 0;
                M.data[r][r] = mag;
            }
        };
        auto stripScaleKeepRotation = [&]() {
            // flag 2: normalize rows (preserve rotation direction).
            for (int r = 0; r < 3; ++r) {
                float mx = M.data[r][0], my = M.data[r][1], mz = M.data[r][2];
                float mag = std::sqrt(mx*mx + my*my + mz*mz);
                if (mag > 1e-8f) {
                    float inv = 1.0f / mag;
                    M.data[r][0] = mx * inv;
                    M.data[r][1] = my * inv;
                    M.data[r][2] = mz * inv;
                }
            }
        };
        auto stripRotationAndScale = [&]() {
            // flag 6: rows become pure identity basis.
            for (int r = 0; r < 3; ++r) {
                M.data[r][0] = 0; M.data[r][1] = 0; M.data[r][2] = 0;
                M.data[r][r] = 1.0f;
            }
        };

        // TranslateView — strips when rmT is set (handles all combos with T).
        Vector3f tTrans = localT;
        if (rmT) {
            stripTranslation();
            if (rmR && rmS)       stripRotationAndScale();
            else if (rmR)         stripRotationKeepScale();
            else if (rmS)         stripScaleKeepRotation();
            tTrans.x += parentPivot.x;
            tTrans.y += parentPivot.y;
            tTrans.z += parentPivot.z;
        }
        tTrans.x += currPivot.x - parentPivot.x;
        tTrans.y += currPivot.y - parentPivot.y;
        tTrans.z += currPivot.z - parentPivot.z;
        applyTranslate(tTrans.x, tTrans.y, tTrans.z);

        // RotateView — strips only when rmR is set alone (flags == 0b100).
        if (rmR && !rmT && !rmS) {
            stripRotationKeepScale();
        }
        if (localR.x != 0.0f || localR.y != 0.0f || localR.z != 0.0f || localR.w != 1.0f) {
            // M := R * M. Matrix44f::rotation(q) is column-vector convention;
            // transpose for row-vector (our matrix mul / transform_point).
            M = Matrix44f::rotation(localR).transpose() * M;
        }

        // ScaleView — strips when (flags & 3) == 2, i.e. rmS set AND rmT unset.
        if (rmS && !rmT) {
            if (rmR) stripRotationAndScale();
            else     stripScaleKeepRotation();
        }
        if (localS.x != 1.0f || localS.y != 1.0f || localS.z != 1.0f) {
            for (int c = 0; c < 4; ++c) M.data[0][c] *= localS.x;
            for (int c = 0; c < 4; ++c) M.data[1][c] *= localS.y;
            for (int c = 0; c < 4; ++c) M.data[2][c] *= localS.z;
        }

        // Cache post-TRS stack for children (the WorldMatrixPush snapshot).
        stackM[i] = M;

        // PlaceObjectSimple: final bone matrix = T(-currPivot) * M.
        // Same left-multiply formula as applyTranslate, with negated pivot,
        // written against a fresh copy so the stack cache is unaffected.
        Matrix44f worldM = M;
        worldM.data[3][0] -= currPivot.x * worldM.data[0][0] + currPivot.y * worldM.data[1][0] + currPivot.z * worldM.data[2][0];
        worldM.data[3][1] -= currPivot.x * worldM.data[0][1] + currPivot.y * worldM.data[1][1] + currPivot.z * worldM.data[2][1];
        worldM.data[3][2] -= currPivot.x * worldM.data[0][2] + currPivot.y * worldM.data[1][2] + currPivot.z * worldM.data[2][2];

        // Billboard rotation is applied in Renderer::ApplyFrameState() using
        // billboardFlags (uniform for both Max and MDX adapter paths).
        allNodeMatrices[i] = worldM;
    }

    boneWorldMatrices.resize(boneCount_);
    for (int i = 0; i < nc; i++) {
        if (nodes_[i].source == HierarchyNode::Source::Bone &&
            nodes_[i].sourceIndex < boneCount_) {
            boneWorldMatrices[nodes_[i].sourceIndex] = allNodeMatrices[i];
        }
    }
}

// ============================================================================
// MdxHierarchy lookups
// ============================================================================

int MdxHierarchy::ObjectIdToNodeIndex(int objectId) const {
    auto it = objectIdToIdx_.find(objectId);
    return it != objectIdToIdx_.end() ? it->second : -1;
}

int MdxHierarchy::BoneIndexToNodeIndex(int boneIdx) const {
    if (boneIdx < 0 || boneIdx >= (int)boneIdxToNodeIdx_.size()) return -1;
    return boneIdxToNodeIdx_[boneIdx];
}

} // namespace WhiteoutDex
