// MaxCore — Animation dispatcher implementation
#include "anim_dispatcher.h"
#include "controller_reader.h"
#include "fk_sampler.h"
#include "biped_sampler.h"
#include "ik_sampler.h"
#include "cat_sampler.h"
#include "link_constraint_sampler.h"
#include "subsample_engine.h"
#include "global_sequence_helper.h"
#include "../optimization/keyframe_optimizer.h"
#include "../util/max_helpers.h"

#include <modstack.h>  // IDerivedObject (full definition for GetObjRef etc.)
#include <istdplug.h> // IKeyControl, ILinFloatKey, etc.

#include <cmath>
#include <algorithm>
#include <functional>
#include <fstream>

// Debug logging — writes to %TEMP%\mdlx_anim_debug.log
static std::ofstream& animLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_anim_debug.log";
        log.open(path, std::ios::trunc);
    }
    return log;
}
#define ALOG animLog()
#define AFLUSH animLog().flush()

namespace core {

// ── Helper: check if a value is "identity" (no real animation) ──
//
// FIX 2026-05-02: epsilon raised from 0.0001 to 0.001.
// GetNodeTM resampling produces sub-mm float drift on bones whose parent
// is animated. The old 0.0001 epsilon let phantom-translation values
// (e.g. 0.00012) through, producing spurious 2-key tracks on bones that
// have NO source animation. 0.001 (= 1 mm at typical scene scales)
// strips drift while preserving any real animation.
static bool isZeroVec(const Point3& v, float eps = 0.001f) {
    return fabsf(v.x) < eps && fabsf(v.y) < eps && fabsf(v.z) < eps;
}

static bool isIdentityQuat(const Quat& q, float eps = 0.0001f) {
    return fabsf(q.x) < eps && fabsf(q.y) < eps &&
           fabsf(q.z) < eps && fabsf(fabsf(q.w) - 1.0f) < eps;
}

static bool isIdentityScale(const Point3& s, float eps = 0.001f) {
    return fabsf(s.x - 1.0f) < eps && fabsf(s.y - 1.0f) < eps && fabsf(s.z - 1.0f) < eps;
}

// Check if ALL keys in a track are identity (no real animation content)
static bool isTrackAllZero(const ir::Vec3Track& track) {
    for (auto& key : track.keys) {
        if (!isZeroVec(key.value)) return false;
    }
    return true;
}

static bool isTrackAllIdentityRot(const ir::QuatTrack& track) {
    for (auto& key : track.keys) {
        if (!isIdentityQuat(key.value)) return false;
    }
    return true;
}

static bool isTrackAllIdentityScale(const ir::Vec3Track& track) {
    for (auto& key : track.keys) {
        if (!isIdentityScale(key.value)) return false;
    }
    return true;
}

// ── Mirror-safe rotation extraction ──────────────────────────────
// Extract a pure rotation quaternion from a world TM that may have
// negative determinant (mirrored transforms from scale [-1,-1,-1]).
// Normalizes rows to remove scale, then flips one row if det < 0
// so that Quat() receives a proper rotation matrix (det = +1).
// For non-mirrored transforms (det > 0) this is identical to the
// old code — the det check never fires.
static Quat extractRotation(const Matrix3& tm) {
    Matrix3 m = tm;
    m.NoTrans();
    Point3 r0 = Normalize(m.GetRow(0));
    Point3 r1 = Normalize(m.GetRow(1));
    Point3 r2 = Normalize(m.GetRow(2));
    // Negative determinant = mirrored transform.
    // Absorb the mirror into scale by flipping one axis.
    float det = DotProd(r0, CrossProd(r1, r2));
    if (det < 0.0f) r0 = -r0;
    Quat q(Matrix3(r0, r1, r2, Point3(0,0,0)));
    q.Normalize();
    return q;
}

// A world TM with a zero-length row — scale 0 on the node or an ancestor —
// carries no rotation: extractRotation would normalize the zero rows into a
// junk quaternion. A row of length 1e-6 still extracts cleanly.
// The rotation/scale part of `tm`, divided by its longest row: relative row
// lengths and directions are kept, magnitudes stay near 1 for an inverse.
static Matrix3 unitSized(const Matrix3& tm) {
    Matrix3 m = tm;
    m.NoTrans();
    const float len = (std::max)({Length(m.GetRow(0)), Length(m.GetRow(1)),
                                Length(m.GetRow(2))});
    if (len > 0.0f)
        for (int i = 0; i < 3; ++i) m.SetRow(i, m.GetRow(i) / len);
    return m;
}

// Whether a TM mirrors: negative determinant of its rotation/scale part
// (measured at unit size, so a tiny scale cannot underflow it to 0).
static bool isMirrored(const Matrix3& tm) {
    const Matrix3 m = unitSized(tm);
    return DotProd(CrossProd(m.GetRow(0), m.GetRow(1)), m.GetRow(2)) < 0.0f;
}

// A node can mirror only for a while: a scale curve that crosses zero
// (Carrot's Bone_tail001 runs its uniform scale 1.5% -> -5.4% -> 0.6% in
// frames 6-10 of Attack 1). extractRotation absorbs a mirror into a flipped
// axis, which for a point reflection is a half turn, and the exported scale is
// a row length, always positive: the node was exported turned 180° and not
// mirrored, and the tail pointed the wrong way (8.7 units off at the tip).
// Whenever the local TM's mirroring differs from the bind pose's, the whole
// TM is negated before the rotation is read and the scale exported negative:
// L = R * S  ->  -L = R * |S|, so L = R * (-|S|). The game and the renderer
// multiply by KGSC as is (mdx-m3-viewer fromRotationTranslationScale,
// WhiteoutFlakes MdxHierarchy), so a negative KGSC mirrors there too. A node
// mirrored at bind and throughout (Max's mirrored bones) is unaffected.
static Matrix3 negated(const Matrix3& tm) {
    Matrix3 m = tm;
    for (int i = 0; i < 3; ++i) m.SetRow(i, -m.GetRow(i));
    return m;
}

static bool isFiniteQuat(const Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z) && std::isfinite(q.w);
}

static bool hasRotation(const Matrix3& tm) {
    const float kMinRowLengthSq = 1e-12f;
    return LengthSquared(tm.GetRow(0)) > kMinRowLengthSq &&
           LengthSquared(tm.GetRow(1)) > kMinRowLengthSq &&
           LengthSquared(tm.GetRow(2)) > kMinRowLengthSq;
}

// A Birth that grows from scale 0 samples such a TM at its start key, and
// with no other key until the end the junk slerped across the whole visible
// sequence (blabla3: root tilted ~66°, its attachments ~90°). The rotation
// at a scale-0 instant never shows — only its continuity with the
// neighbouring samples does — so evaluate the nearest time inside [lo, hi]
// that has one: forward from the start, backward from the end. Steps double
// because nested 0→1 scales multiply: a tick in can still be ~1e-20.
// Returns false when the whole window is degenerate; otherwise the last
// evaluateAt call is the one that succeeded.
static bool evaluateNearestRotation(TimeValue t, TimeValue lo, TimeValue hi,
                                    const std::function<bool(TimeValue)>& evaluateAt) {
    if (evaluateAt(t)) return true;
    for (TimeValue step = 1;; step *= 2) {
        const TimeValue fwd = std::min(t + step, hi);
        const TimeValue back = std::max(t - step, lo);
        if (fwd > t && evaluateAt(fwd)) return true;
        if (back < t && evaluateAt(back)) return true;
        if (fwd == hi && back == lo) return false;
    }
}

// ── Key-time sampling helpers ─────────────────────────────────────
// Instead of sampling at every frame (GetTicksPerFrame ticks), FK nodes are sampled
// only at their controller's key times. This matches NeoDex's approach
// and reduces key count from ~257 to ~8-33 per bone per sequence.

// Collect key times from a controller within a sequence range.
// Always includes sequence start and end as boundary keys.
static std::vector<TimeValue> collectKeyTimes(Control* ctrl,
                                               TimeValue seqStart,
                                               TimeValue seqEnd) {
    std::vector<TimeValue> times;
    if (ctrl) {
        int nk = ctrl->NumKeys();
        if (nk > 0) {
            // First try: Control::GetKeyTime (works for most controllers)
            for (int i = 0; i < nk; i++) {
                TimeValue kt = ctrl->GetKeyTime(i);
                if (kt >= seqStart && kt <= seqEnd)
                    times.push_back(kt);
            }

            // Sanity check: if controller reports many keys but we found
            // very few in this sequence range, GetKeyTime might be broken.
            // Fall back to per-frame sampling for this sequence.
            if (times.size() <= 2 && nk > 100) {
                // Estimate how many keys should be in this range
                TimeValue totalRange = ctrl->GetKeyTime(nk - 1) - ctrl->GetKeyTime(0);
                TimeValue seqRange = seqEnd - seqStart;
                if (totalRange > 0) {
                    int expected = static_cast<int>((float)nk * seqRange / totalRange);
                    if (expected > 5 && times.size() <= 2) {
                        // GetKeyTime is likely broken — fall back to per-frame
                        times.clear();
                        for (TimeValue t = seqStart; t <= seqEnd; t += GetTicksPerFrame())
                            times.push_back(t);
                        if (times.back() != seqEnd)
                            times.push_back(seqEnd);
                        return times;
                    }
                }
            }
        }
    }
    if (times.empty() || times.front() != seqStart)
        times.insert(times.begin(), seqStart);
    if (times.back() != seqEnd)
        times.push_back(seqEnd);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return times;
}

// For Euler_XYZ rotation: merge key times from all 3 sub-controllers.
static std::vector<TimeValue> collectEulerKeyTimes(Control* rotCtrl,
                                                     TimeValue seqStart,
                                                     TimeValue seqEnd) {
    std::vector<TimeValue> times;
    if (rotCtrl) {
        for (int axis = 0; axis < 3 && axis < rotCtrl->NumSubs(); axis++) {
            Animatable* sub = rotCtrl->SubAnim(axis);
            Control* subCtrl = sub ? GetControlInterface(sub) : nullptr;
            if (subCtrl) {
                int nk = subCtrl->NumKeys();
                for (int i = 0; i < nk; i++) {
                    TimeValue kt = subCtrl->GetKeyTime(i);
                    if (kt >= seqStart && kt <= seqEnd)
                        times.push_back(kt);
                }
            }
        }
    }
    times.push_back(seqStart);
    times.push_back(seqEnd);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return times;
}

// Generate per-frame times (fallback for IK/Biped/CAT/Link/procedural)
static std::vector<TimeValue> generateFrameTimes(TimeValue seqStart,
                                                  TimeValue seqEnd,
                                                  int frameInterval = GetTicksPerFrame()) {
    std::vector<TimeValue> times;
    for (TimeValue t = seqStart; t <= seqEnd; t += frameInterval)
        times.push_back(t);
    if (times.empty() || times.back() != seqEnd)
        times.push_back(seqEnd);
    return times;
}

// Filter pre-collected IK key times to a sequence range + boundaries.
static std::vector<TimeValue> filterIKKeyTimes(
        const std::vector<TimeValue>& allIKKeys,
        TimeValue seqStart, TimeValue seqEnd) {
    std::vector<TimeValue> times;
    times.push_back(seqStart);
    for (TimeValue t : allIKKeys) {
        if (t > seqStart && t < seqEnd)
            times.push_back(t);
    }
    times.push_back(seqEnd);
    // Already sorted (allIKKeys is sorted), just dedup
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return times;
}

// Key-time sampling assumes each span between samples is the short arc that
// linear playback takes. A half turn or more between two keys (a TCB key at
// 360°, Euler keys 270° apart, a cycling ORT with no key in the sequence)
// samples as a small turn the wrong way or none at all. Probe every span and
// sample the ones that travel that far densely; the reducer trims them back.
static std::vector<TimeValue> densifyRotationTimes(
        const std::vector<TimeValue>& times,
        const std::function<Quat(TimeValue)>& localRotAt) {
    if (times.size() < 2) return times;
    const float maxTravel = 2.0f * acosf(KeyframeOptimizer::kHalfTurnDot);
    std::vector<TimeValue> out;
    std::vector<TimeValue> probes;
    const TimeValue tpf = GetTicksPerFrame();
    Quat q0 = localRotAt(times.front());
    for (size_t i = 0; i + 1 < times.size(); ++i) {
        const TimeValue t0 = times[i], t1 = times[i + 1];
        out.push_back(t0);

        // Per frame, but at least 8 probes so a spin between keys one frame
        // apart is still seen; >= 10 ticks (~2 ms) apart so MDX times stay
        // distinct after rounding to milliseconds.
        //
        // Not between two keys on whole frames, though: Max shows an animation
        // keyed that way only on whole frames, so a turn between two of them
        // is nothing the animator ever saw. The chicken man's baked CAT rig
        // has linear_rotation keys every frame, one of which turns the hand
        // ~355° the long way between frames 453 and 454 of Stand 3 - probed
        // at sub-frames, the export spun the hand once around in the game.
        // Those spans are probed per whole frame only, so a spin over several
        // frames still counts. Keys between frames (an imported MDX's
        // millisecond times) keep the fine probes.
        const TimeValue span = t1 - t0;
        const bool wholeFrames = tpf > 0 && t0 % tpf == 0 && t1 % tpf == 0;
        const int n = wholeFrames
            ? static_cast<int>(span / tpf)
            : std::min(std::max(8, static_cast<int>((span + tpf - 1) / std::max<TimeValue>(tpf, 1))),
                       static_cast<int>(span / 10));
        probes.clear();
        float travel = 0.0f;
        Quat prev = q0;
        for (int k = 1; k < n; ++k) {
            TimeValue t = t0 + static_cast<TimeValue>(static_cast<int64_t>(span) * k / n);
            Quat q = localRotAt(t);
            travel += quatAngle(prev, q);
            prev = q;
            probes.push_back(t);
        }
        Quat q1 = localRotAt(t1);
        travel += quatAngle(prev, q1);
        q0 = q1;

        if (travel >= maxTravel)
            out.insert(out.end(), probes.begin(), probes.end());
    }
    out.push_back(times.back());
    return out;
}

// Key-time sampling of position and scale assumes the curve runs straight
// from key to key, which is how the game plays the exported Linear track. A
// Bezier or TCB curve does not: it eases, overshoots and bulges between its
// keys. Carrot's Bone_tail001 has scale keys 1.0 and 1.0 whose tangents lift
// the curve to 1.13 in between, and the export kept a flat 1.0 - the tail
// tip ended up 26 units off. Every span is probed per frame; where the
// straight line misses the curve by more than |tolerance|, the probes needed
// to follow it are added (greedy from the last kept probe, the way
// KeyframeOptimizer reduces). Straight spans - linear controllers, holds,
// most tangents - gain nothing.
static std::vector<TimeValue> densifyCurveTimes(
        const std::vector<TimeValue>& times,
        const std::function<Point3(TimeValue)>& valueAt,
        float tolerance) {
    if (times.size() < 2) return times;
    const TimeValue kFrame = std::max<TimeValue>(GetTicksPerFrame(), 1);  // one frame of the scene
    std::vector<TimeValue> out;
    std::vector<TimeValue> st;
    std::vector<Point3> sv;
    for (size_t i = 0; i + 1 < times.size(); ++i) {
        const TimeValue t0 = times[i], t1 = times[i + 1];
        out.push_back(t0);
        if (t1 - t0 <= kFrame) continue;

        st.clear();
        sv.clear();
        for (TimeValue t = t0; t < t1; t += kFrame) {
            st.push_back(t);
            sv.push_back(valueAt(t));
        }
        st.push_back(t1);
        sv.push_back(valueAt(t1));

        size_t anchor = 0;
        for (size_t j = 1; j + 1 < st.size(); ++j) {
            const size_t to = j + 1;
            const float dt = static_cast<float>(st[to] - st[anchor]);
            bool straight = true;
            for (size_t k = anchor + 1; straight && k <= j; ++k) {
                const float u = static_cast<float>(st[k] - st[anchor]) / dt;
                straight = Length(sv[k] - (sv[anchor] + (sv[to] - sv[anchor]) * u)) <= tolerance;
            }
            if (!straight) {
                out.push_back(st[j]);
                anchor = j;
            }
        }
    }
    out.push_back(times.back());
    return out;
}

// How far the exported straight line may miss a Bezier/TCB curve between
// keys before densifyCurveTimes follows it: 0.01 units of translation (the
// models are ~100-300 units tall) and 0.5% of scale, the scale reducer's own
// tolerance.
constexpr float kCurveTolTranslation = 0.01f;
constexpr float kCurveTolScale = 0.005f;

// ─── Global-sequence transform channels ──────────────────────────────
//
// A position, rotation or scale controller that cycles after its last key
// (ORT_CYCLE / ORT_LOOP) runs on a Global Sequence: it loops on its own
// clock whatever sequence plays, with the last key time as its duration.
// That is NeoDex's rule (Wc3Animation.ms GetAnimationData), the importer's
// layout of a GLBS-driven KGTR/KGRT/KGSC (keys at the global sequence's own
// times, ORT_CYCLE after), and the rule every other exported track follows
// (global_sequence_helper.h).
//
// Baked per sequence instead, the loop restarts at every sequence start
// and jumps whenever a sequence loops that isn't a whole number of cycles
// long: CatapultMissile's Sphere01 spins on a 500 ms global sequence and
// its 1167 ms Stand jumped ~120° every loop.
//
// Control::NumKeys/GetKeyTime rather than IKeyControl::GetKey: position and
// rotation key structs are larger than the float key GetKey would fill.
static TimeValue cycleEndOf(Control* ctrl) {
    const int after = ctrl->GetORT(ORT_AFTER);
    if (after != ORT_CYCLE && after != ORT_LOOP) return 0;
    const int n = ctrl->NumKeys();
    return n > 0 ? ctrl->GetKeyTime(n - 1) : 0;
}

// Global Sequence duration in ticks for a transform channel's controller,
// or 0 when the channel is sequence-driven. Euler XYZ / Position XYZ keep
// keys and ORTs on their float sub-controllers: the channel cycles when
// every keyed one does, for as long as the longest.
static TimeValue globalSequenceDuration(Control* ctrl) {
    if (!ctrl) return 0;
    TimeValue end = cycleEndOf(ctrl);
    if (end == 0) {
        for (int i = 0; i < ctrl->NumSubs(); ++i) {
            Animatable* sub = ctrl->SubAnim(i);
            Control* c = sub ? GetControlInterface(sub) : nullptr;
            if (!c || c->NumKeys() <= 0) continue;
            const TimeValue subEnd = cycleEndOf(c);
            if (subEnd <= 0) return 0;
            end = std::max(end, subEnd);
        }
    }
    return end > 0 ? end : 0;
}

// Walk the derived-object chain down to the base object.
static Object* baseObjectOf(INode* maxNode) {
    if (!maxNode) return nullptr;
    Object* obj = maxNode->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(obj);
        obj = d->GetObjRef();
    }
    return obj;
}

// ── Orientation-critical nodes: export ABSOLUTE rotation ─────────────
//
// The delta-from-bind KGRT convention below is correct for bones and
// plain helpers because their bind orientation is baked into the skinned
// vertices (exported in world space at bind). Emitter-like nodes carry
// no geometry that could bake the bind orientation: in game their rest
// orientation is exactly what KGRT says — identity when there are no
// keys. Exporting the delta silently discards any static aim (e.g. a
// PE2 rotated 90° to emit sideways exports identity and emits straight
// up in game) and offsets every animated rotation by the bind rotation.
//
// For these node types KGRT must reproduce the node's Max WORLD
// orientation: local = worldRot * Inverse(parentGameRot), where the
// parent's in-game rotation is its bind delta (bone convention) or its
// own world rotation (parent is itself an absolute node).
//
// ClassIDs duplicated from MDLXExporter mdx_class_ids.h (MaxCore cannot
// include exporter headers) — keep in sync.
static bool isOrientationAbsoluteNode(INode* maxNode) {
    Object* obj = baseObjectOf(maxNode);
    if (!obj) return false;
    const Class_ID id = obj->ClassID();
    static const Class_ID kAbsoluteIds[] = {
        Class_ID(0x12E4F5A6u, 0x3B7C8D9Eu), // WC3_PARTICLES1
        Class_ID(0xD9F33BC9u, 0x7A0DA37Au), // WC3_PARTICLES2
        Class_ID(0x937AA064u, 0x9EFFA3DAu), // WC3_RIBBON
        Class_ID(0x7A1B2C04u, 0x3D4E5F04u), // WC3_LIGHT
        Class_ID(0x7A1B2C05u, 0x3D4E5F05u), // WC3_EVENT_V2021
        Class_ID(0x7A1B2C06u, 0x3D4E5F06u), // WC3_EVENT_V2020
        Class_ID(0x7A1B2C09u, 0x3D4E5F09u), // WC3_POPCORN
        Class_ID(0x7A1B2C01u, 0x3D4E5F01u), // WC3_ATTACH_POINT
        Class_ID(0x750735E3u, 0x21F2D857u), // NEODEX_PARTICLES1
        Class_ID(0x02942cacu, 0x43c6a3d9u), // NEODEX_PARTICLES2
        Class_ID(0x179527d0u, 0x1c217376u), // NEODEX_RIBBON
        Class_ID(0x456E2573u, 0x2A456757u), // NEODEX_LIGHT
        Class_ID(0x189dc89eu, 0x2e9f652du), // NEODEX_EVENT
        Class_ID(0x956e6a9bu, 0x87f39a9eu), // NEODEX_EVENT_V2020
        Class_ID(0x6a17b48cu, 0x291672feu), // NEODEX_POPCORN
        Class_ID(0x1136ac20u, 0x6f9cfeb7u), // NEODEX_ATTACH_POINT
    };
    for (const auto& k : kAbsoluteIds)
        if (id == k) return true;
    return false;
}

void AnimDispatcher::bakeAll(ir::IRModel& irModel,
                              const std::vector<ir::Sequence>& sequences,
                              const Config& config,
                              ExportErrorReporter& reporter) {
    if (sequences.empty()) return;

    FKSampler fkSampler;
    BipedSampler bipedSampler;
    IKSampler ikSampler;
    CATSampler catSampler;
    LinkConstraintSampler linkSampler;

    ALOG << "=== AnimDispatcher::bakeAll === nodes=" << irModel.nodes.size()
         << " sequences=" << sequences.size() << "\n";
    ALOG << "  Using GetValue() approach (Autodesk-recommended)\n";
    AFLUSH;

    // ── Pre-scan: collect key times from all IK chains ──────────
    // IK-affected bones are driven by IK goals, not by their own
    // controllers. NeoDex reads keys from the IK chain helpers and
    // their targets, then samples GetNodeTM only at those times.
    // We do the same: collect key times from all IK_Chain_Object
    // helpers + their parent hierarchies + VH targets.
    std::vector<TimeValue> ikMergedKeyTimes;
    {
        auto collectKeysFromNode = [](INode* n, std::vector<TimeValue>& out) {
            if (!n) return;
            Control* tmC = n->GetTMController();
            if (!tmC) return;
            // Position keys
            Control* pc = tmC->GetPositionController();
            if (pc) {
                int nk = pc->NumKeys();
                for (int i = 0; i < nk; i++)
                    out.push_back(pc->GetKeyTime(i));
            }
            // Rotation keys
            Control* rc = tmC->GetRotationController();
            if (rc) {
                int nk = rc->NumKeys();
                for (int i = 0; i < nk; i++)
                    out.push_back(rc->GetKeyTime(i));
            }
        };

        // Walk all scene nodes looking for IK_Chain_Object helpers
        INode* sceneRoot = GetCOREInterface()->GetRootNode();
        std::function<void(INode*)> scanForIKChains = [&](INode* parent) {
            for (int i = 0; i < parent->NumberOfChildren(); i++) {
                INode* child = parent->GetChildNode(i);
                if (!child) continue;

                ObjectState os = child->EvalWorldState(0);
                if (os.obj) {
                    MSTR className;
                    os.obj->GetClassName(className);
                    const wchar_t* cn = className.data();
                    if (cn && (wcsstr(cn, L"IK") != nullptr ||
                               wcsstr(cn, L"ik") != nullptr)) {
                        // Collect keys from this IK helper
                        collectKeysFromNode(child, ikMergedKeyTimes);
                        // Collect from its parent chain
                        INode* p = child->GetParentNode();
                        while (p && !p->IsRootNode()) {
                            collectKeysFromNode(p, ikMergedKeyTimes);
                            p = p->GetParentNode();
                        }
                        // Collect from children (potential swivel targets)
                        for (int c = 0; c < child->NumberOfChildren(); c++)
                            collectKeysFromNode(child->GetChildNode(c), ikMergedKeyTimes);
                    }
                }
                scanForIKChains(child);
            }
        };
        scanForIKChains(sceneRoot);

        // Deduplicate and sort
        std::sort(ikMergedKeyTimes.begin(), ikMergedKeyTimes.end());
        ikMergedKeyTimes.erase(
            std::unique(ikMergedKeyTimes.begin(), ikMergedKeyTimes.end()),
            ikMergedKeyTimes.end());

        ALOG << "  IK chain scan: " << ikMergedKeyTimes.size() << " merged key times\n";
        AFLUSH;
    }

    for (size_t nodeIdx = 0; nodeIdx < irModel.nodes.size(); ++nodeIdx) {
        if (config.onNode) config.onNode(nodeIdx, irModel.nodes.size());
        auto& irNode = irModel.nodes[nodeIdx];
        INode* maxNode = irNode.maxNode;
        if (!maxNode) continue;

        INode* parentNode = maxNode->GetParentNode();
        Control* tmCtrl = maxNode->GetTMController();
        if (!tmCtrl) continue;

        // ── Sample bind pose at frame 0 (tick 0) ──
        Point3 bindPos, bindScl;
        Quat   bindRot;

        ALOG << "  >> evaluating bind pose for node[" << nodeIdx << "] '" << irNode.name << "'...\n";
        AFLUSH;

        // Biped controllers crash on GetValue() — use GetNodeTM decomposition instead.
        // Link Constraint nodes also need GetNodeTM because their effective parent
        // changes during animation, making GetValue unreliable for bind pose.
        // CAT controllers can return incorrect rotations via GetValue (known bug).
        bool isBipedNode = ControllerReader::isBiped(maxNode);
        bool isLinkNode = ControllerReader::isLinkConstraint(tmCtrl);
        // Fallback: check ClassID directly (Link Constraint ClassID not always recognized)
        if (!isLinkNode && tmCtrl) {
            Class_ID lcID(2269112164u, 2864612865u);
            isLinkNode = (tmCtrl->ClassID() == lcID);
        }
        bool isCATNode = ControllerReader::isCAT(maxNode);
        bool needsSafeBindPose = isBipedNode || isLinkNode || isCATNode;
        if (needsSafeBindPose) {
            // Safe path: decompose GetNodeTM (same as IK path in subsample_engine)
            Matrix3 nodeTM = maxNode->GetNodeTM(0);
            Matrix3 localTM;
            INode* root = GetCOREInterface()->GetRootNode();
            if (parentNode && parentNode != root) {
                Matrix3 parentTM = parentNode->GetNodeTM(0);
                localTM = nodeTM * Inverse(parentTM);
            } else {
                localTM = nodeTM;
            }
            bindPos = localTM.GetRow(3);
            Point3 r0 = localTM.GetRow(0);
            Point3 r1 = localTM.GetRow(1);
            Point3 r2 = localTM.GetRow(2);
            float sx = Length(r0); float sy = Length(r1); float sz = Length(r2);
            if (sx < 0.0001f) sx = 1.0f;
            if (sy < 0.0001f) sy = 1.0f;
            if (sz < 0.0001f) sz = 1.0f;
            bindScl = Point3(sx, sy, sz);
            if (sx > 0.0001f) r0 = r0 / sx;
            if (sy > 0.0001f) r1 = r1 / sy;
            if (sz > 0.0001f) r2 = r2 / sz;
            Matrix3 rotMat;
            rotMat.SetRow(0, r0); rotMat.SetRow(1, r1);
            rotMat.SetRow(2, r2); rotMat.SetRow(3, Point3(0,0,0));
            bindRot = Quat(rotMat);
        } else {
            SubsampleEngine::evaluateLocalTransform(maxNode, parentNode, 0, bindPos, bindRot, bindScl);
        }

        ALOG << "  >> bind pose OK\n";
        AFLUSH;

        // Always resample rotation via GetNodeTM world transforms.
        // Bones can have identity bind rotation yet significant animation
        // (e.g. spine_03_bind_jnt in book golem). The FK sampler alone
        // produces too few keys — GetNodeTM captures every frame correctly.
        bool needsRotDelta = true;
        bool needsScaleDelta = !isIdentityScale(bindScl);

        // ── World rotation bind poses for NeoDex-style delta ──
        // Uses GetNodeTM() which is universally correct for FK, IK, constraints, etc.
        Quat worldBindRot(0.0f,0.0f,0.0f,1.0f), parentWorldBindRot(0.0f,0.0f,0.0f,1.0f);
        if (needsRotDelta) {
            Matrix3 wb = maxNode->GetNodeTM(0);
            worldBindRot = extractRotation(wb);

            if (parentNode && !parentNode->IsRootNode()) {
                Matrix3 pb = parentNode->GetNodeTM(0);
                parentWorldBindRot = extractRotation(pb);
            }
        }

        // Whether the local TM mirrors at bind (see negated): a sample that
        // differs is a temporary mirror, exported as a negative scale.
        const bool bindMirrored = [&] {
            const Matrix3 nodeTM0 = maxNode->GetNodeTM(0);
            if (parentNode && !parentNode->IsRootNode())
                return isMirrored(unitSized(nodeTM0) * Inverse(unitSized(parentNode->GetNodeTM(0))));
            return isMirrored(nodeTM0);
        }();

        // Emitter-like nodes export absolute rotation AND scale instead of
        // the bind delta/ratio (see isOrientationAbsoluteNode) — nothing
        // bakes their bind transform the way skinned vertices do for bones.
        // Translation stays delta-based for all nodes: the pivot is the
        // world bind position, so absolute positions are preserved anyway.
        // The parent's convention decides how its in-game rotation is
        // reconstructed when solving for this node's local rotation.
        const bool useAbsoluteRotScale = isOrientationAbsoluteNode(maxNode);
        const bool parentIsAbsolute =
            (parentNode && !parentNode->IsRootNode()) &&
            isOrientationAbsoluteNode(parentNode);

        ALOG << "  node[" << nodeIdx << "] '" << irNode.name << "'"
             << " bindPos=(" << bindPos.x << "," << bindPos.y << "," << bindPos.z << ")"
             << " bindRot=(" << bindRot.x << "," << bindRot.y << "," << bindRot.z << "," << bindRot.w << ")"
             << " bindScl=(" << bindScl.x << "," << bindScl.y << "," << bindScl.z << ")"
             << " rotDelta=" << (needsRotDelta ? "ACTIVE" : "none(identity)")
             << " rotMode=" << (useAbsoluteRotScale ? "ABSOLUTE" : "bind-delta")
             << "\n";

        // Collect all sequence animations for this node
        std::vector<ir::NodeAnimation> nodeAnims;
        bool hasAnyRealTranslation = false;
        bool hasAnyRealRotation = false;
        bool hasAnyRealScale = false;

        // ── FK key-time sampling: detect controller types once per node ──
        Control* posCtrlFK = tmCtrl->GetPositionController();
        Control* rotCtrlFK = tmCtrl->GetRotationController();
        Control* sclCtrlFK = tmCtrl->GetScaleController();
        ControllerType posTypeFK = ControllerReader::detect(posCtrlFK);
        ControllerType rotTypeFK = ControllerReader::detect(rotCtrlFK);

        bool isFKPosNode = (posTypeFK == ControllerType::Bezier_Position ||
                            posTypeFK == ControllerType::TCB_Position ||
                            posTypeFK == ControllerType::Linear_Position);
        bool isFKRotNode = (rotTypeFK == ControllerType::Bezier_Rotation ||
                            rotTypeFK == ControllerType::TCB_Rotation ||
                            rotTypeFK == ControllerType::Linear_Rotation ||
                            rotTypeFK == ControllerType::Euler_XYZ);
        bool needsPerFrame = isBipedNode || isLinkNode || isCATNode;
        bool isIKNode = ControllerReader::isIKAffected(maxNode);

        ALOG << "    keyTimeMode: pos=" << (isFKPosNode && !needsPerFrame && !isIKNode ? "KEY" : (isIKNode ? "IK-KEY" : "FRAME"))
             << " rot=" << (isFKRotNode && !needsPerFrame && !isIKNode ? "KEY" : (isIKNode ? "IK-KEY" : "FRAME")) << "\n";

        // ── Sampling windows: every sequence, plus one per global-sequence
        // channel (see globalSequenceDuration) sampled once over
        // [0, duration], the loop's own clock. A channel lives in exactly one
        // of the two. Per-frame and IK nodes are baked whole from GetNodeTM;
        // they have no channel controller to loop.
        enum { kTrans, kRot, kScale };
        struct SampleWindow {
            TimeValue startTime = 0, endTime = 0;
            int channel = -1;  // -1: a sequence; else the global-sequence channel
        };
        std::vector<SampleWindow> windows;
        for (const auto& s : sequences)
            windows.push_back({s.startTime, s.endTime, -1});
        bool channelIsGlobal[3] = {false, false, false};
        if (!needsPerFrame && !isIKNode) {
            Control* const channelCtrls[3] = {posCtrlFK, rotCtrlFK, sclCtrlFK};
            for (int ch = 0; ch < 3; ++ch) {
                const TimeValue dur = globalSequenceDuration(channelCtrls[ch]);
                if (dur <= 0) continue;
                channelIsGlobal[ch] = true;
                windows.push_back({0, dur, ch});
                ALOG << "    GLOBAL-SEQ channel " << "TRS"[ch]
                     << " duration=" << dur << " ticks\n";
            }
        }

        for (const auto& seq : windows) {
            ir::NodeAnimation nodeAnim;
            nodeAnim.nodeIndex = static_cast<int32_t>(nodeIdx);

            // Determine sampler priority
            if (isLinkNode) {
                linkSampler.sample(maxNode, parentNode,
                                    seq.startTime, seq.endTime,
                                    nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (isBipedNode) {
                bipedSampler.sample(maxNode, parentNode,
                                     seq.startTime, seq.endTime,
                                     nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (ControllerReader::isIKAffected(maxNode)) {
                ikSampler.sample(maxNode, parentNode,
                                  seq.startTime, seq.endTime,
                                  config.angleThreshold,
                                  nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else if (isCATNode) {
                catSampler.sample(maxNode, parentNode,
                                   seq.startTime, seq.endTime,
                                   nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
            } else {
                Control* posCtrl = tmCtrl->GetPositionController();
                Control* rotCtrl = tmCtrl->GetRotationController();
                ControllerType posType = ControllerReader::detect(posCtrl);
                ControllerType rotType = ControllerReader::detect(rotCtrl);

                bool isFKPos = (posType == ControllerType::Bezier_Position ||
                                posType == ControllerType::TCB_Position ||
                                posType == ControllerType::Linear_Position);
                bool isFKRot = (rotType == ControllerType::Bezier_Rotation ||
                                rotType == ControllerType::TCB_Rotation ||
                                rotType == ControllerType::Linear_Rotation ||
                                rotType == ControllerType::Euler_XYZ);

                if (isFKPos || isFKRot) {
                    fkSampler.sample(maxNode, parentNode,
                                      seq.startTime, seq.endTime,
                                      nodeAnim.translation, nodeAnim.rotation, nodeAnim.scale);
                } else {
                    SubsampleEngine::SampleConfig sampleCfg;
                    sampleCfg.tickInterval = config.tickInterval;
                    sampleCfg.adaptiveRefine = false;
                    SubsampleEngine::sampleNode(maxNode, parentNode,
                                                 seq.startTime, seq.endTime, sampleCfg,
                                                 nodeAnim.translation, nodeAnim.rotation,
                                                 nodeAnim.scale);
                }
            }

            // ── Translation delta — literal NeoDex PositionKeys port ──
            // Reference: NeoDex Wc3Animation.ms, fn PositionKeys, default branch:
            //   correctionQuat = at time 0f o.parent.transform.rotationPart as quat
            //   parentScale    = at time 0f o.parent.scale
            //   p              = at time 0f c.value
            //   for t in allKeyTimes do (
            //       relativeAnimVector = (at time t c.value) - p
            //       if o.parent != undefined then
            //           relativeAnimVector = relativeAnimVector * parentScale
            //       relativeAnimVector *= correctionQuat
            //       append keys (KeyData t relativeAnimVector)
            //   )
            //
            // Notes:
            //   - For this scene's constant-uniform parent scale, this produces
            //     the same KGTR values as the older normalize-then-rotate logic.
            //     Both are byte-identical to NeoDex's output (verified). Kept the
            //     literal port because it's clearer semantically and handles
            //     animated parent scale correctly (the older code would diverge
            //     in that case by losing scale info via double normalization).
            {
                // Log what the FK sampler produced BEFORE we replace it
                int fkKeyCount = static_cast<int>(nodeAnim.translation.keys.size());
                Point3 fkFirstVal(0,0,0), fkLastVal(0,0,0);
                if (fkKeyCount > 0) {
                    fkFirstVal = nodeAnim.translation.keys[0].value;
                    fkLastVal = nodeAnim.translation.keys.back().value;
                }

                nodeAnim.translation.keys.clear();
                nodeAnim.translation.interpolation = ir::InterpolationType::Linear;
                const int frameInterval = GetTicksPerFrame();

                // ── Bind-time setup (NeoDex constants) ─────────────────────────
                Point3 parentScale_bind(1.0f, 1.0f, 1.0f);
                Matrix3 parentRot_bind;
                parentRot_bind.IdentityMatrix();
                Point3 cValue_0(0, 0, 0);

                Point3 worldBindPos = maxNode->GetNodeTM(0).GetTrans();

                if (parentNode && !parentNode->IsRootNode()) {
                    Matrix3 parentBindTM_full = parentNode->GetNodeTM(0);
                    cValue_0 = (maxNode->GetNodeTM(0) * Inverse(parentBindTM_full)).GetTrans();

                    Matrix3 parentBindTM_noT = parentBindTM_full;
                    parentBindTM_noT.NoTrans();
                    parentScale_bind = Point3(
                        Length(parentBindTM_noT.GetRow(0)),
                        Length(parentBindTM_noT.GetRow(1)),
                        Length(parentBindTM_noT.GetRow(2))
                    );
                    parentRot_bind = parentBindTM_noT;
                    parentRot_bind.SetRow(0, Normalize(parentRot_bind.GetRow(0)));
                    parentRot_bind.SetRow(1, Normalize(parentRot_bind.GetRow(1)));
                    parentRot_bind.SetRow(2, Normalize(parentRot_bind.GetRow(2)));
                } else {
                    cValue_0 = worldBindPos;
                }

                ALOG << "    TRANS node[" << nodeIdx << "] '" << irNode.name << "'"
                     << " fkKeys=" << fkKeyCount
                     << " fkFirst=(" << fkFirstVal.x << "," << fkFirstVal.y << "," << fkFirstVal.z << ")"
                     << " fkLast=(" << fkLastVal.x << "," << fkLastVal.y << "," << fkLastVal.z << ")"
                     << "\n";
                ALOG << "      worldBindPos=(" << worldBindPos.x << "," << worldBindPos.y << "," << worldBindPos.z << ")"
                     << " cValue_0=(" << cValue_0.x << "," << cValue_0.y << "," << cValue_0.z << ")"
                     << " parentScale_bind=(" << parentScale_bind.x << "," << parentScale_bind.y << "," << parentScale_bind.z << ")"
                     << " evalLocalBind=(" << bindPos.x << "," << bindPos.y << "," << bindPos.z << ")"
                     << "\n";

                // KGTR value at t (NeoDex PositionKeys, see above).
                auto translationAt = [&](TimeValue t) {
                    Point3 cValue_t;
                    if (parentNode && !parentNode->IsRootNode()) {
                        Matrix3 parentTM_t_full = parentNode->GetNodeTM(t);
                        cValue_t = (maxNode->GetNodeTM(t) * Inverse(parentTM_t_full)).GetTrans();
                    } else {
                        cValue_t = maxNode->GetNodeTM(t).GetTrans();
                    }

                    Point3 delta = cValue_t - cValue_0;

                    delta.x *= parentScale_bind.x;
                    delta.y *= parentScale_bind.y;
                    delta.z *= parentScale_bind.z;

                    return delta * parentRot_bind;
                };

                int transLogCount = 0;
                const bool transAtKeys = !isIKNode && !needsPerFrame && isFKPosNode;
                auto transTimes = isIKNode
                    ? filterIKKeyTimes(ikMergedKeyTimes, seq.startTime, seq.endTime)
                    : transAtKeys
                        ? collectKeyTimes(posCtrlFK, seq.startTime, seq.endTime)
                        : generateFrameTimes(seq.startTime, seq.endTime, frameInterval);
                if (transAtKeys)
                    transTimes = densifyCurveTimes(transTimes, translationAt, kCurveTolTranslation);
                for (TimeValue t : transTimes) {
                    Point3 delta = translationAt(t);

                    if (fabsf(delta.x) < 0.0001f) delta.x = 0.0f;
                    if (fabsf(delta.y) < 0.0001f) delta.y = 0.0f;
                    if (fabsf(delta.z) < 0.0001f) delta.z = 0.0f;

                    if (transLogCount < 5 || (t % (frameInterval * 100)) == 0) {
                        ALOG << "      t=" << t
                             << " delta=(" << delta.x << "," << delta.y << "," << delta.z << ")"
                             << "\n";
                    }
                    transLogCount++;

                    ir::Keyframe<Point3> key;
                    key.time = t;
                    key.value = delta;
                    nodeAnim.translation.keys.push_back(key);
                }

                ALOG << "      totalTransKeys=" << nodeAnim.translation.keys.size() << "\n";
            }

            // ── Rotation delta using world TMs (NeoDex method) ──
            // Resample at EVERY FRAME using GetNodeTM() world transforms.
            // NeoDex samples all bones at every frame — GetNodeTM() captures
            // IK, constraints, and all controller effects correctly.
            if (needsRotDelta) {
                nodeAnim.rotation.keys.clear();
                nodeAnim.rotation.interpolation = ir::InterpolationType::Linear;

                Quat invWorldBind = Inverse(worldBindRot);

                const bool hasParentTM = parentNode && !parentNode->IsRootNode();
                auto localRotAt = [&](TimeValue t) {
                    // The node's rotation relative to its parent, taken from
                    // the local TM. Differencing the two world rotations
                    // instead (worldRot * Inverse(parentWorldRot)) only works
                    // while no ancestor scales non-uniformly: Max composes a
                    // child's world TM through the parent's scale, so under a
                    // parent scaled (0.73, 0.69, 0.73) the child's world TM is
                    // sheared, its extracted "rotation" is not a unit
                    // quaternion and points the wrong way. Mandrake's Death
                    // shrinks its root like that and every bone below it
                    // drifted, up to 3 units at the toes. nodeTM *
                    // Inverse(parentTM) is exactly the local controller TM,
                    // which holds only the node's own scale, so no shear —
                    // translation and scale are already read from it.
                    //
                    // Both TMs are brought to unit size before the inverse. A
                    // chain that shrinks to 0 together (Mandrake's last Death
                    // frame) leaves a deep bone's parent TM near 1e-8 per
                    // axis; its determinant underflows float and the inverse
                    // came out NaN. Uniform rescaling does not change the
                    // rotation extractRotation reads from normalized rows.
                    Quat localRot(0.0f, 0.0f, 0.0f, 1.0f);
                    TimeValue evalTime = t;
                    const auto evaluateAt = [&](TimeValue s) {
                        evalTime = s;
                        const Matrix3 nodeTM = maxNode->GetNodeTM(s);
                        if (!hasRotation(nodeTM)) return false;
                        Matrix3 local = nodeTM;
                        if (hasParentTM) {
                            const Matrix3 parentTM = parentNode->GetNodeTM(s);
                            if (!hasRotation(parentTM)) return false;
                            local = unitSized(nodeTM) * Inverse(unitSized(parentTM));
                        }
                        // A temporary mirror is carried by a negative KGSC.
                        if (isMirrored(local) != bindMirrored)
                            local = negated(local);
                        localRot = extractRotation(local);
                        return isFiniteQuat(localRot);
                    };
                    // Scale 0 for the whole window: the rotation never shows.
                    if (!evaluateNearestRotation(t, seq.startTime, seq.endTime, evaluateAt))
                        return Quat(0.0f, 0.0f, 0.0f, 1.0f);
                    if (evalTime != t) {
                        ALOG << "    ROT t=" << t << " scale-0 TM, rotation taken from t="
                             << evalTime << "\n";
                    }

                    // Bone convention (matches NeoDex GetRot): KGRT stores the
                    // rotation CHANGE from bind pose — the bind orientation
                    // itself is baked into the world-space skinned vertices.
                    // Orientation-critical nodes (emitters, lights, events,
                    // attachments) have nothing that bakes the bind
                    // orientation, so their KGRT must reproduce the absolute
                    // Max world orientation instead.
                    //
                    // With localRot == worldRot * Inverse(parentWorldRot) these
                    // are the old formulas expanded:
                    //   bone:     (invWorldBind * worldRot) * Inverse(parentDelta)
                    //   absolute: worldRot * Inverse(parentWorldRot | parentDelta)
                    // where parentDelta = invParentWorldBind * parentWorldRot.
                    Quat q;
                    if (useAbsoluteRotScale)
                        q = parentIsAbsolute ? localRot
                                             : localRot * parentWorldBindRot;
                    else
                        q = invWorldBind * localRot * parentWorldBindRot;
                    q.Normalize();
                    return q;
                };

                int logCount = 0;
                const int frameInterval = GetTicksPerFrame(); // one frame of the scene
                const bool keyTimeRot = isIKNode || (!needsPerFrame && isFKRotNode);
                auto rotTimes = isIKNode
                    ? filterIKKeyTimes(ikMergedKeyTimes, seq.startTime, seq.endTime)
                    : keyTimeRot
                        ? (rotTypeFK == ControllerType::Euler_XYZ
                            ? collectEulerKeyTimes(rotCtrlFK, seq.startTime, seq.endTime)
                            : collectKeyTimes(rotCtrlFK, seq.startTime, seq.endTime))
                        : generateFrameTimes(seq.startTime, seq.endTime, frameInterval);
                const size_t keyTimeCount = rotTimes.size();
                if (keyTimeRot)
                    rotTimes = densifyRotationTimes(rotTimes, localRotAt);

                if (logCount == 0) {
                    ALOG << "    ROT seq[" << seq.startTime << "-" << seq.endTime
                         << "] rotTimes=" << rotTimes.size()
                         << " (key times " << keyTimeCount << ")"
                         << " ctrlNumKeys=" << (rotCtrlFK ? rotCtrlFK->NumKeys() : -1)
                         << " isFKRot=" << isFKRotNode
                         << " isIK=" << isIKNode
                         << " needsPerFrame=" << needsPerFrame << "\n";
                }

                for (TimeValue t : rotTimes) {
                    Quat localRot = localRotAt(t);

                    // Hemisphere consistency: force w >= 0, then check prev-key
                    if (localRot.w < 0.0f) {
                        localRot.x = -localRot.x; localRot.y = -localRot.y;
                        localRot.z = -localRot.z; localRot.w = -localRot.w;
                    }
                    if (!nodeAnim.rotation.keys.empty()) {
                        const Quat& prev = nodeAnim.rotation.keys.back().value;
                        if ((prev.x*localRot.x + prev.y*localRot.y + prev.z*localRot.z + prev.w*localRot.w) < 0.0f) {
                            localRot.x = -localRot.x; localRot.y = -localRot.y;
                            localRot.z = -localRot.z; localRot.w = -localRot.w;
                        }
                    }

                    ir::Keyframe<Quat> key;
                    key.time = t;
                    key.value = localRot;
                    nodeAnim.rotation.keys.push_back(key);

                    if (logCount < 6) {
                        ALOG << "    ROT t=" << t
                             << " localRot=(" << localRot.x << "," << localRot.y << "," << localRot.z << "," << localRot.w << ")\n";
                        logCount++;
                    }
                }
            }
            // Snap near-identity rotation components
            for (auto& key : nodeAnim.rotation.keys) {
                if (fabsf(key.value.x) < 0.00001f) key.value.x = 0.0f;
                if (fabsf(key.value.y) < 0.00001f) key.value.y = 0.0f;
                if (fabsf(key.value.z) < 0.00001f) key.value.z = 0.0f;
                if (fabsf(key.value.w - 1.0f) < 0.00001f) key.value.w = 1.0f;
                if (fabsf(key.value.w + 1.0f) < 0.00001f) key.value.w = -1.0f;
            }

            // ── Scale: resample using GetNodeTM (same approach as rotation) ──
            // The FK sampler may miss keys or sample across sequence boundaries.
            // Resampling via GetNodeTM at every frame captures the true scale.
            {
                nodeAnim.scale.keys.clear();
                nodeAnim.scale.interpolation = ir::InterpolationType::Linear;
                const int frameInterval = GetTicksPerFrame();

                // Bind-time local scale (row lengths of local TM)
                Matrix3 bindNodeTM = maxNode->GetNodeTM(0);
                Matrix3 bindParentTM;
                bindParentTM.IdentityMatrix();
                if (parentNode && !parentNode->IsRootNode())
                    bindParentTM = parentNode->GetNodeTM(0);
                Matrix3 bindLocalTM = bindNodeTM * Inverse(bindParentTM);
                float bindSx = Length(bindLocalTM.GetRow(0));
                float bindSy = Length(bindLocalTM.GetRow(1));
                float bindSz = Length(bindLocalTM.GetRow(2));
                if (bindSx < 0.0001f) bindSx = 1.0f;
                if (bindSy < 0.0001f) bindSy = 1.0f;
                if (bindSz < 0.0001f) bindSz = 1.0f;

                // Absolute nodes export their local scale verbatim: KGSC is
                // the in-game scale, and nothing bakes an emitter's bind
                // scale the way world-space vertices do for bones. Dividing
                // by bind scale would silently discard a statically scaled
                // emitter (ratio 1.0 → all-identity → track stripped).
                if (useAbsoluteRotScale) {
                    bindSx = 1.0f;
                    bindSy = 1.0f;
                    bindSz = 1.0f;
                }

                // KGSC value at t: local scale relative to the bind scale.
                auto scaleAt = [&](TimeValue t) {
                    Matrix3 nodeTM = maxNode->GetNodeTM(t);
                    Matrix3 parTM;
                    parTM.IdentityMatrix();
                    if (parentNode && !parentNode->IsRootNode())
                        parTM = parentNode->GetNodeTM(t);
                    Matrix3 localTM = nodeTM * Inverse(parTM);
                    // A temporary mirror is a negative scale (see negated).
                    const float sign = isMirrored(localTM) != bindMirrored ? -1.0f : 1.0f;
                    return Point3(sign * Length(localTM.GetRow(0)) / bindSx,
                                  sign * Length(localTM.GetRow(1)) / bindSy,
                                  sign * Length(localTM.GetRow(2)) / bindSz);
                };

                const bool sclAtKeys = !isIKNode && !needsPerFrame;
                auto sclTimes = isIKNode
                    ? filterIKKeyTimes(ikMergedKeyTimes, seq.startTime, seq.endTime)
                    : sclAtKeys
                        ? collectKeyTimes(sclCtrlFK, seq.startTime, seq.endTime)
                        : generateFrameTimes(seq.startTime, seq.endTime, frameInterval);
                if (sclAtKeys)
                    sclTimes = densifyCurveTimes(sclTimes, scaleAt, kCurveTolScale);
                for (TimeValue t : sclTimes) {
                    const Point3 s = scaleAt(t);
                    float sx = s.x, sy = s.y, sz = s.z;

                    if (fabsf(sx - 1.0f) < 0.001f) sx = 1.0f;
                    if (fabsf(sy - 1.0f) < 0.001f) sy = 1.0f;
                    if (fabsf(sz - 1.0f) < 0.001f) sz = 1.0f;

                    ir::Keyframe<Point3> key;
                    key.time = t;
                    key.value = Point3(sx, sy, sz);
                    nodeAnim.scale.keys.push_back(key);
                }
            }

            // A global-sequence channel is kept only in its own window.
            const auto inWindow = [&](int ch) {
                return seq.channel < 0 ? !channelIsGlobal[ch] : seq.channel == ch;
            };
            if (!inWindow(kTrans)) nodeAnim.translation.keys.clear();
            if (!inWindow(kRot))   nodeAnim.rotation.keys.clear();
            if (!inWindow(kScale)) nodeAnim.scale.keys.clear();

            // ── Key reduction: remove keys reconstructable by interpolation ──
            // Tolerances tuned for WC3 MDX quality:
            //   Rotation: 0.01 rad ≈ 0.57° — barely visible, good reduction
            //   Translation: 0.05 units — adequate for scenes up to 300 units
            //   Scale: 0.005 — 0.5% deviation allowed
            {
                size_t trBefore = nodeAnim.translation.keys.size();
                size_t rtBefore = nodeAnim.rotation.keys.size();
                size_t scBefore = nodeAnim.scale.keys.size();

                KeyframeOptimizer reducer;

                // Translation tolerance: 0.001 units. Original value before
                // the drift-investigation detour. Drift was caused by a
                // mesh bind-pose bug in mesh_extractor.cpp, not by key
                // density — see useSkinned policy there.
                reducer.optimize(nodeAnim.translation, 0.001f);

                // Tolerance 0.0001 rad ≈ 0.0057°. Tightened from the
                // original 0.01 rad (0.573°) which was too aggressive
                // for fine mechanical motion such as chain-link sway:
                // any animation whose largest delta was below ~0.5°
                // would be slerp-merged into the boundary keys, leaving
                // only Identity start/end keys. Anything below 0.0001
                // rad is true numeric noise (well under one quat-
                // component LSB at typical float precision).
                // Multi-revolution spins are safe to reduce: the optimizer
                // never lets one segment reach a half turn.
                reducer.optimize(nodeAnim.rotation, 0.0001f);

                // Scale tolerance: 0.005 — 0.5% deviation allowed.
                reducer.optimize(nodeAnim.scale, 0.005f);

                if (trBefore > 2 || rtBefore > 2 || scBefore > 2) {
                    ALOG << "    REDUCE node[" << nodeIdx << "] '" << irNode.name << "'"
                         << " tr=" << trBefore << "->" << nodeAnim.translation.keys.size()
                         << " rt=" << rtBefore << "->" << nodeAnim.rotation.keys.size()
                         << " sc=" << scBefore << "->" << nodeAnim.scale.keys.size()
                         << "\n";
                }
            }

            // ── Check if this sequence has real animation ──
            // Global-sequence windows don't count: their tracks are complete on
            // their own clock, and a t=0 rest key would merge into them.
            const bool isSequence = seq.channel < 0;
            if (isSequence && !isTrackAllZero(nodeAnim.translation))
                hasAnyRealTranslation = true;
            // ROTATION FIX (Bone_maozi2 "ein bisschen daneben" bug):
            // The OLD check used isTrackAllIdentityRot, which returned true for
            // bones whose rotation tracks were animated in Max but always landed
            // near identity after bind-delta correction (typical for bones whose
            // local rotation doesn't change but whose parent rotates a lot —
            // e.g. Bone_maozi2 under Bone_maozi). The rotation track was then
            // stripped on lines below, causing the renderer to fall back on the
            // bone's bind-pose rotation instead of explicit identity quats.
            // For bones with non-identity bind rotation (Bone_maozi2 has ~91° on
            // Y from FBX import), these two paths give visually different
            // results — hence the small position drift on the chain end.
            // NeoDex (Wc3Animation.ms PositionKeys/RotationKeys) emits these
            // identity tracks intact; we now match that behavior by treating
            // ANY non-empty rotation track as real.
            if (isSequence && !nodeAnim.rotation.keys.empty())
                hasAnyRealRotation = true;
            if (isSequence && !isTrackAllIdentityScale(nodeAnim.scale))
                hasAnyRealScale = true;

            // ── Strip identity-only tracks for this sequence ──
            // Translation/scale: stripping all-zero / all-identity is safe — the
            //   renderer's "no track" path produces the same runtime values.
            // Rotation: NOT safe to strip when bind rotation is non-identity,
            //   because "no KGRT" leaves bind rotation in effect at runtime,
            //   while "KGRT with identity" overrides it with zero rotation.
            //   The two paths give different visual results, so we must emit
            //   the track even when all keys are identity. (NeoDex behavior.)
            if (isTrackAllZero(nodeAnim.translation))
                nodeAnim.translation.keys.clear();
            // [REMOVED] isTrackAllIdentityRot strip — see comment above.
            if (isTrackAllIdentityScale(nodeAnim.scale))
                nodeAnim.scale.keys.clear();

            if (!isSequence && !(nodeAnim.translation.empty() &&
                                 nodeAnim.rotation.empty() && nodeAnim.scale.empty())) {
                const int32_t gs = anim::registerGlobalSequence(irModel, seq.endTime);
                nodeAnim.translation.globalSequenceIndex = gs;
                nodeAnim.rotation.globalSequenceIndex = gs;
                nodeAnim.scale.globalSequenceIndex = gs;
                ALOG << "    GLOBAL-SEQ node[" << nodeIdx << "] '" << irNode.name
                     << "' channel " << "TRS"[seq.channel] << " -> GLBS[" << gs << "]\n";
            }

            nodeAnims.push_back(std::move(nodeAnim));
        }

        // ── Add t=0 rest-pose key (identity) for tracks with real animation ──
        // MDX needs a key at t=0 to define the rest/default pose.
        // Without it, viewers don't know what to show when no sequence plays.
        bool needsRestKey = hasAnyRealTranslation || hasAnyRealRotation || hasAnyRealScale;

        if (needsRestKey) {
            ir::NodeAnimation restAnim;
            restAnim.nodeIndex = static_cast<int32_t>(nodeIdx);

            if (hasAnyRealTranslation) {
                ir::Keyframe<Point3> key;
                key.time = 0;
                key.value = Point3(0, 0, 0);
                restAnim.translation.keys.push_back(key);
                restAnim.translation.interpolation = ir::InterpolationType::Linear;
            }

            if (hasAnyRealRotation) {
                ir::Keyframe<Quat> key;
                key.time = 0;
                // Bones rest at identity (delta convention). Absolute nodes
                // rest at their bind orientation so a static aim survives
                // outside every sequence range too.
                key.value = Quat(0.0f, 0.0f, 0.0f, 1.0f);
                if (useAbsoluteRotScale) {
                    key.value = parentIsAbsolute
                        ? worldBindRot * Inverse(parentWorldBindRot)
                        : worldBindRot;
                }
                restAnim.rotation.keys.push_back(key);
                restAnim.rotation.interpolation = ir::InterpolationType::Linear;
            }

            if (hasAnyRealScale) {
                ir::Keyframe<Point3> key;
                key.time = 0;
                // Absolute nodes rest at their bind scale (see the scale
                // sampling block above); bones rest at 1.
                key.value = useAbsoluteRotScale ? bindScl : Point3(1, 1, 1);
                restAnim.scale.keys.push_back(key);
                restAnim.scale.interpolation = ir::InterpolationType::Linear;
            }

            irModel.nodeAnimations.push_back(std::move(restAnim));
        }

        // ── Push sequence animations (only if they have actual data) ──
        for (auto& na : nodeAnims) {
            if (!na.translation.empty() || !na.rotation.empty() || !na.scale.empty()) {
                irModel.nodeAnimations.push_back(std::move(na));
            }
        }

        // ── Log summary ──
        ALOG << "    hasRealAnim: TR=" << hasAnyRealTranslation
             << " RT=" << hasAnyRealRotation
             << " SC=" << hasAnyRealScale
             << " restKey=" << needsRestKey << "\n";
        AFLUSH;
    }

    ALOG << "=== AnimDispatcher complete: " << irModel.nodeAnimations.size() << " nodeAnimations ===\n";
    AFLUSH;
}

} // namespace core
