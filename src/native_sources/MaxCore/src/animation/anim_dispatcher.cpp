// MaxCore — Animation dispatcher implementation
#include "anim_dispatcher.h"
#include "controller_reader.h"
#include "fk_sampler.h"
#include "biped_sampler.h"
#include "ik_sampler.h"
#include "cat_sampler.h"
#include "link_constraint_sampler.h"
#include "subsample_engine.h"

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

// Dedicated PRE2 debug log — writes to %TEMP%\mdlx_pre2_debug.log
// Separated from the main anim log so PRE2-only debugging doesn't drown
// in bone/helper spam, and so it's easy to diff run-to-run.
static std::ofstream& pre2Log() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_pre2_debug.log";
        log.open(path, std::ios::trunc);
        log << "=== PRE2 Cyclic Rotation Debug Log ===\n";
    }
    return log;
}
#define PLOG pre2Log()
#define PFLUSH pre2Log().flush()

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

// Slerp between two quaternions
static Quat slerpQuat(const Quat& a, const Quat& b, float t) {
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    Quat bb = b;
    if (dot < 0.0f) { bb.x=-bb.x; bb.y=-bb.y; bb.z=-bb.z; bb.w=-bb.w; dot=-dot; }
    if (dot > 0.9999f) {
        // Nearly identical — lerp + normalize
        Quat r;
        r.x = a.x + t*(bb.x - a.x);
        r.y = a.y + t*(bb.y - a.y);
        r.z = a.z + t*(bb.z - a.z);
        r.w = a.w + t*(bb.w - a.w);
        float len = sqrtf(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
        if (len > 0.0f) { r.x/=len; r.y/=len; r.z/=len; r.w/=len; }
        return r;
    }
    float theta = acosf(dot);
    float sinTheta = sinf(theta);
    float wa = sinf((1.0f-t)*theta) / sinTheta;
    float wb = sinf(t*theta) / sinTheta;
    Quat r;
    r.x = wa*a.x + wb*bb.x;
    r.y = wa*a.y + wb*bb.y;
    r.z = wa*a.z + wb*bb.z;
    r.w = wa*a.w + wb*bb.w;
    return r;
}

// Quaternion distance (angle between two quaternions)
//
// FIX 2026-05-02: switched from 2*acos(|dot|) to 4*asin(|a-b|/2) to fix
// catastrophic float32 precision loss for near-identical quaternions.
//
// THE BUG: For two quats with relative rotation ~0.022° (well above the
// reducer tolerance of 0.0057°), the dot product equals 1 - 1.8e-8.
// Float32 epsilon at 1.0 is 1.19e-7, so 1.8e-8 falls BELOW representable
// precision — dot rounds to exactly 1.0, acos(1.0) = 0, and the reducer
// reports zero error. Result: every key in a sub-degree rotation animation
// gets marked "redundant" and discarded, leaving only boundary keys.
// Confirmed on Moria_platform: 131-key Chain_R004 track collapsed to 3
// identity keys despite a clearly visible 0.4° peak in the source.
//
// THE FIX: For unit quaternions q1, q2 with relative rotation θ, the
// vector distance |q1 - q2| = 2|sin(θ/4)|, so θ = 4·asin(|q1-q2|/2).
// Computing |q1-q2| via direct subtraction has NO catastrophic
// cancellation — small differences are preserved at full mantissa
// precision, regardless of how close to identity both quats are.
// We also try |q1+q2| (handles q ≡ -q hemisphere ambiguity) and use
// whichever gives the smaller distance.
static float quatError(const Quat& a, const Quat& b) {
    // Same-hemisphere distance
    float dx1 = a.x - b.x, dy1 = a.y - b.y, dz1 = a.z - b.z, dw1 = a.w - b.w;
    float dist1Sq = dx1*dx1 + dy1*dy1 + dz1*dz1 + dw1*dw1;

    // Opposite-hemisphere distance (q and -q represent same rotation)
    float dx2 = a.x + b.x, dy2 = a.y + b.y, dz2 = a.z + b.z, dw2 = a.w + b.w;
    float dist2Sq = dx2*dx2 + dy2*dy2 + dz2*dz2 + dw2*dw2;

    float minDistSq = (dist1Sq < dist2Sq) ? dist1Sq : dist2Sq;
    float halfDist = sqrtf(minDistSq) * 0.5f;
    if (halfDist >= 1.0f) return 3.14159265f; // 180° apart
    return 4.0f * asinf(halfDist);
}

// Remove redundant keys from a rotation track.
// Keys that can be reconstructed by slerp interpolation within the given
// angular tolerance are discarded — this matches NeoDex's key reduction behavior.
static void reduceRotationKeys(ir::QuatTrack& track, float toleranceRad = 0.001f) {
    if (track.keys.size() <= 2) return;

    std::vector<bool> keep(track.keys.size(), false);
    keep.front() = true;
    keep.back() = true;

    // Greedy forward pass: walk from first kept key, skip keys that
    // can be interpolated, keep a key when error exceeds threshold.
    size_t anchor = 0;
    for (size_t i = 1; i < track.keys.size() - 1; ++i) {
        // Check if ALL keys from anchor+1..i can be interpolated from anchor..i+1
        bool canSkip = true;
        size_t next = i + 1;
        for (size_t j = anchor + 1; j <= i; ++j) {
            float tTotal = static_cast<float>(track.keys[next].time - track.keys[anchor].time);
            float tJ = static_cast<float>(track.keys[j].time - track.keys[anchor].time);
            float frac = (tTotal > 0.0f) ? (tJ / tTotal) : 0.0f;
            Quat interp = slerpQuat(track.keys[anchor].value, track.keys[next].value, frac);
            if (quatError(interp, track.keys[j].value) > toleranceRad) {
                canSkip = false;
                break;
            }
        }
        if (!canSkip) {
            keep[i] = true;
            anchor = i;
        }
    }

    // Compact
    std::vector<ir::Keyframe<Quat>> reduced;
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (keep[i]) reduced.push_back(track.keys[i]);
    }
    track.keys = std::move(reduced);
}

// Remove redundant keys from a translation track (linear interpolation check).
static void reduceTranslationKeys(ir::Vec3Track& track, float tolerance = 0.01f) {
    if (track.keys.size() <= 2) return;

    std::vector<bool> keep(track.keys.size(), false);
    keep.front() = true;
    keep.back() = true;

    size_t anchor = 0;
    for (size_t i = 1; i < track.keys.size() - 1; ++i) {
        bool canSkip = true;
        size_t next = i + 1;
        for (size_t j = anchor + 1; j <= i; ++j) {
            float tTotal = static_cast<float>(track.keys[next].time - track.keys[anchor].time);
            float tJ = static_cast<float>(track.keys[j].time - track.keys[anchor].time);
            float frac = (tTotal > 0.0f) ? (tJ / tTotal) : 0.0f;
            Point3 interp = track.keys[anchor].value + frac * (track.keys[next].value - track.keys[anchor].value);
            Point3 diff = interp - track.keys[j].value;
            if (Length(diff) > tolerance) {
                canSkip = false;
                break;
            }
        }
        if (!canSkip) {
            keep[i] = true;
            anchor = i;
        }
    }

    std::vector<ir::Keyframe<Point3>> reduced;
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (keep[i]) reduced.push_back(track.keys[i]);
    }
    track.keys = std::move(reduced);
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
    return Quat(Matrix3(r0, r1, r2, Point3(0,0,0)));
}

// ── Key-time sampling helpers ─────────────────────────────────────
// Instead of sampling at every frame (160 ticks), FK nodes are sampled
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
                        for (TimeValue t = seqStart; t <= seqEnd; t += 160)
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
                                                  int frameInterval = 160) {
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

// ─── Patch C: PRE2 cyclic rotation extraction ────────────────────────
//
// Particle2 emitters often have their rotation sub-controller set to
// ORT_CYCLE/ORT_LOOP with a short period (e.g. 333ms, 667ms). In MDX
// this maps to a KGRT track with globalSequenceId referring to a GLBS
// entry with the cycle duration.
//
// The regular per-sequence FK sampling path destroys this:
//   * Each regular sequence (Stand, Walk...) resamples GetNodeTM every
//     frame over seq.startTime..endTime.
//   * A 3-second sequence on a 333ms cyclic rotation contains ~9 full
//     rotations; the delta-against-bind evaluation either produces
//     garbage or resolves to near-identity at sequence boundaries.
//   * isTrackAllIdentityRot then strips the track entirely.
//
// This helper detects the cyclic case and extracts the rotation keys
// directly from the controller (one pass, not per-sequence), tagging
// the resulting NodeAnimation with the correct globalSequenceIndex.
//
// Scope: intentionally limited to Wc3Particles2 nodes. BONE and HELP
// nodes with cyclic rotations may have similar issues but their
// regular-rotation pipelines are currently validated and should not
// be disturbed by this targeted fix.

static bool isWc3Particles2Node(INode* maxNode) {
    if (!maxNode) return false;
    Object* obj = maxNode->GetObjectRef();
    if (!obj) return false;
    // Walk derived-object chain to get to the base object
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(obj);
        obj = d->GetObjRef();
    }
    if (!obj) return false;
    // WC3_PARTICLES2 from mdx_class_ids.h
    const Class_ID kWc3Particles2(0xD9F33BC9u, 0x7A0DA37Au);
    return obj->ClassID() == kWc3Particles2;
}

// Returns true if any of the rotation sub-controller's after-ORT is
// CYCLE or LOOP (the two values Max treats as "looping" for GS purposes).
static bool hasCyclicRotation(Control* rotCtrl) {
    if (!rotCtrl) return false;
    int afterORT = rotCtrl->GetORT(ORT_AFTER);
    return afterORT == ORT_CYCLE || afterORT == ORT_LOOP;
}

// Extract the cyclic rotation for a PRE2 node into a single NodeAnimation
// with globalSequenceIndex set. Returns true on success (keys + GS registered).
// This function uses GetNodeTM-based delta just like the regular rotation
// pipeline, but samples ONLY within one cycle period (0..duration), not
// across multiple sequences.
static bool extractPre2CyclicRotation(INode* maxNode, int32_t nodeIdx,
                                      Control* rotCtrl, ir::IRModel& irModel)
{
    if (!maxNode || !rotCtrl) return false;

    MSTR nodeName = maxNode->GetName();
    std::string nameA;
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, nodeName.data(), -1,
                                       nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            nameA.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, nodeName.data(), -1,
                                 nameA.data(), len, nullptr, nullptr);
        }
    }

    PLOG << "\n──────────────────────────────────────────────────\n";
    PLOG << "Node[" << nodeIdx << "] '" << nameA << "'\n";

    // Duration = last key's time
    int numKeys = rotCtrl->NumKeys();
    if (numKeys <= 0) {
        PLOG << "  REJECT: no keys on rotation controller\n";
        PFLUSH;
        ALOG << "    [PRE2-cyclic] no keys on rotation controller — skip\n";
        return false;
    }
    TimeValue duration = rotCtrl->GetKeyTime(numKeys - 1);
    if (duration <= 0) {
        PLOG << "  REJECT: duration=" << duration << " (must be > 0)\n";
        PFLUSH;
        ALOG << "    [PRE2-cyclic] duration<=0 — skip\n";
        return false;
    }

    // Convert ticks to milliseconds for logging only
    uint32_t durMs = static_cast<uint32_t>(
        static_cast<int64_t>(duration) * 1000 / 4800);

    PLOG << "  rotCtrl ClassID=(0x" << std::hex << rotCtrl->ClassID().PartA()
         << "," << rotCtrl->ClassID().PartB() << std::dec << ")\n";
    PLOG << "  numKeys=" << numKeys << " duration=" << duration
         << " ticks (" << durMs << " ms)\n";
    PLOG << "  afterORT=" << rotCtrl->GetORT(ORT_AFTER)
         << " beforeORT=" << rotCtrl->GetORT(ORT_BEFORE) << "\n";

    ALOG << "    [PRE2-cyclic] numKeys=" << numKeys
         << " duration=" << duration << " ticks\n";

    // Register the GlobalSequence (de-duped by duration)
    // IMPORTANT: globalSequenceDurations stores TICKS (matches
    // GEOA/material convention). Writer converts ticks→ms at MDX output.
    uint32_t durU = static_cast<uint32_t>(duration);
    int32_t gsIdx = -1;
    for (size_t i = 0; i < irModel.globalSequenceDurations.size(); ++i) {
        if (irModel.globalSequenceDurations[i] == durU) {
            gsIdx = static_cast<int32_t>(i);
            break;
        }
    }
    if (gsIdx < 0) {
        irModel.globalSequenceDurations.push_back(durU);
        gsIdx = static_cast<int32_t>(irModel.globalSequenceDurations.size() - 1);
        PLOG << "  registered NEW GLBS[" << gsIdx << "] durationTicks=" << durU
             << " (=" << durMs << "ms)\n";
        ALOG << "    [PRE2-cyclic] registered new GLBS[" << gsIdx << "] dur=" << durU << "\n";
    } else {
        PLOG << "  reusing GLBS[" << gsIdx << "] durationTicks=" << durU
             << " (=" << durMs << "ms)\n";
        ALOG << "    [PRE2-cyclic] reusing GLBS[" << gsIdx << "] dur=" << durU << "\n";
    }

    // Extract local rotation via GetNodeTM for each key time.
    //
    // Earlier attempts failed:
    //   1. GetNodeTM-delta against bind pose → identity for ORT_CYCLE at t=0
    //      (bind sampled at t=0, where cyclic already active).
    //   2. rotCtrl->GetValue(t, &quat, iv) directly → returns identity for t=0
    //      because Max's PRS rotation sub-controller stores *deltas* relative
    //      to the node's rest pose, not absolute local rotation.
    //
    // Correct method: maxNode->GetNodeTM(t) * Inverse(parentTM) yields the
    // ABSOLUTE local transform at time t, including the rest pose.
    INode* parentNode = maxNode->GetParentNode();
    INode* rootNode = GetCOREInterface()->GetRootNode();
    bool hasParent = (parentNode && parentNode != rootNode && !parentNode->IsRootNode());

    PLOG << "  parent=" << (hasParent ? "yes" : "no-or-root")
         << " parentName='";
    if (hasParent) {
        MSTR pname = parentNode->GetName();
        std::string pnameA;
        int len = WideCharToMultiByte(CP_UTF8, 0, pname.data(), -1,
                                       nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            pnameA.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, pname.data(), -1,
                                 pnameA.data(), len, nullptr, nullptr);
        }
        PLOG << pnameA;
    }
    PLOG << "'\n";

    ir::NodeAnimation nodeAnim;
    nodeAnim.nodeIndex = nodeIdx;
    nodeAnim.rotation.interpolation = ir::InterpolationType::Linear;
    nodeAnim.rotation.globalSequenceIndex = gsIdx;

    PLOG << "  --- Key extraction ---\n";

    for (int i = 0; i < numKeys; ++i) {
        TimeValue t = rotCtrl->GetKeyTime(i);

        Matrix3 nodeTM = maxNode->GetNodeTM(t);
        Matrix3 localTM = nodeTM;
        if (hasParent) {
            Matrix3 parentTM = parentNode->GetNodeTM(t);
            localTM = nodeTM * Inverse(parentTM);
        }
        localTM.NoTrans();

        // Normalize row vectors to strip any scale component
        Point3 r0 = Normalize(localTM.GetRow(0));
        Point3 r1 = Normalize(localTM.GetRow(1));
        Point3 r2 = Normalize(localTM.GetRow(2));
        float det = DotProd(r0, CrossProd(r1, r2));
        if (det < 0.0f) r0 = -r0;
        Quat localRot = Quat(Matrix3(r0, r1, r2, Point3(0, 0, 0)));

        // Log RAW value before any adjustments
        PLOG << "    key[" << i << "] t=" << t << " ticks ("
             << (static_cast<int64_t>(t) * 1000 / 4800) << "ms)"
             << " raw=(" << localRot.x << "," << localRot.y
             << "," << localRot.z << "," << localRot.w << ")";

        // Hemisphere consistency: ensure continuity with previous key
        bool flipped = false;
        if (!nodeAnim.rotation.keys.empty()) {
            const Quat& prev = nodeAnim.rotation.keys.back().value;
            if ((prev.x*localRot.x + prev.y*localRot.y +
                 prev.z*localRot.z + prev.w*localRot.w) < 0.0f) {
                localRot.x = -localRot.x; localRot.y = -localRot.y;
                localRot.z = -localRot.z; localRot.w = -localRot.w;
                flipped = true;
            }
        }

        // Snap near-identity components
        if (fabsf(localRot.x) < 0.00001f) localRot.x = 0.0f;
        if (fabsf(localRot.y) < 0.00001f) localRot.y = 0.0f;
        if (fabsf(localRot.z) < 0.00001f) localRot.z = 0.0f;
        if (fabsf(localRot.w - 1.0f) < 0.00001f) localRot.w = 1.0f;
        if (fabsf(localRot.w + 1.0f) < 0.00001f) localRot.w = -1.0f;

        PLOG << " final=(" << localRot.x << "," << localRot.y
             << "," << localRot.z << "," << localRot.w << ")"
             << (flipped ? " [HEMI-FLIPPED]" : "") << "\n";

        ir::Keyframe<Quat> key;
        key.time = t;
        key.value = localRot;
        nodeAnim.rotation.keys.push_back(key);
    }

    PLOG << "  --- DONE: pushing NodeAnimation with "
         << numKeys << " keys, gsIdx=" << gsIdx << " ---\n";
    PFLUSH;

    irModel.nodeAnimations.push_back(std::move(nodeAnim));
    ALOG << "    [PRE2-cyclic] emitted NodeAnimation with "
         << numKeys << " rotation keys, gsIdx=" << gsIdx << "\n";
    return true;
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

        ALOG << "  node[" << nodeIdx << "] '" << irNode.name << "'"
             << " bindPos=(" << bindPos.x << "," << bindPos.y << "," << bindPos.z << ")"
             << " bindRot=(" << bindRot.x << "," << bindRot.y << "," << bindRot.z << "," << bindRot.w << ")"
             << " bindScl=(" << bindScl.x << "," << bindScl.y << "," << bindScl.z << ")"
             << " rotDelta=" << (needsRotDelta ? "ACTIVE" : "none(identity)")
             << "\n";

        // ── Patch C: PRE2 cyclic rotation short-circuit ────────────
        //
        // For Wc3Particles2 emitters with ORT_CYCLE/ORT_LOOP rotations,
        // we extract ONCE from the controller (respecting the cycle
        // period as a Global Sequence) instead of per-sequence resampling
        // which destroys the cyclic nature.
        //
        // IMPORTANT: we still fall through to the regular translation/
        // scale pipeline below. Only the rotation is short-circuited.
        //
        // Scope: PE2 nodes only. Light rotation global sequences are
        // handled separately in the light extractor.
        bool pre2CyclicHandled = false;
        if (isWc3Particles2Node(maxNode)) {
            Control* rotCtrl = tmCtrl->GetRotationController();
            if (hasCyclicRotation(rotCtrl)) {
                ALOG << "  [PRE2-cyclic] node[" << nodeIdx << "] '" << irNode.name
                     << "' has cyclic rotation — using GlobalSequence path\n";
                pre2CyclicHandled = extractPre2CyclicRotation(
                    maxNode, static_cast<int32_t>(nodeIdx), rotCtrl, irModel);
            }
        }

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

        for (const auto& seq : sequences) {
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

            // ── Patch C: PRE2 cyclic rotation — clear sampler output ──
            //
            // The samplers above unconditionally write rotation keys into
            // nodeAnim.rotation. For PRE2 nodes handled by the cyclic path,
            // those keys would later merge into the per-node KGRT and
            // overwrite/duplicate our correct cyclic values.
            //
            // The fix: clear the rotation keys right after sampling so only
            // our cyclic-path NodeAnimation feeds into the merge.
            // Also zero the interpolation so isTrackAllIdentityRot passes,
            // keeping hasAnyRealRotation=false (→ no identity rest-pose key).
            if (pre2CyclicHandled) {
                nodeAnim.rotation.keys.clear();
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
                const int frameInterval = 160;

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

                int transLogCount = 0;
                auto transTimes = isIKNode
                    ? filterIKKeyTimes(ikMergedKeyTimes, seq.startTime, seq.endTime)
                    : (!needsPerFrame && isFKPosNode)
                        ? collectKeyTimes(posCtrlFK, seq.startTime, seq.endTime)
                        : generateFrameTimes(seq.startTime, seq.endTime, frameInterval);
                for (TimeValue t : transTimes) {
                    Point3 cValue_t;
                    Point3 worldPos = maxNode->GetNodeTM(t).GetTrans();

                    if (parentNode && !parentNode->IsRootNode()) {
                        Matrix3 parentTM_t_full = parentNode->GetNodeTM(t);
                        cValue_t = (maxNode->GetNodeTM(t) * Inverse(parentTM_t_full)).GetTrans();
                    } else {
                        cValue_t = worldPos;
                    }

                    Point3 delta = cValue_t - cValue_0;

                    delta.x *= parentScale_bind.x;
                    delta.y *= parentScale_bind.y;
                    delta.z *= parentScale_bind.z;

                    delta = delta * parentRot_bind;

                    if (fabsf(delta.x) < 0.0001f) delta.x = 0.0f;
                    if (fabsf(delta.y) < 0.0001f) delta.y = 0.0f;
                    if (fabsf(delta.z) < 0.0001f) delta.z = 0.0f;

                    if (transLogCount < 5 || (t % (frameInterval * 100)) == 0) {
                        ALOG << "      t=" << t
                             << " world=(" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")"
                             << " cValue_t=(" << cValue_t.x << "," << cValue_t.y << "," << cValue_t.z << ")"
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
            //
            // Skip entirely for PRE2 nodes that used the cyclic path above —
            // their rotation is already emitted as a single NodeAnimation
            // with globalSequenceIndex. The earlier clear block (right after
            // the samplers) already stripped any FK/IK sampler output, so
            // skipping here keeps rotation empty for this per-sequence nodeAnim.
            if (needsRotDelta && !pre2CyclicHandled) {
                nodeAnim.rotation.keys.clear();
                nodeAnim.rotation.interpolation = ir::InterpolationType::Linear;

                Quat invWorldBind = Inverse(worldBindRot);
                Quat invParentWorldBind = Inverse(parentWorldBindRot);
                int logCount = 0;
                const int frameInterval = 160; // 1 frame at 30fps
                auto rotTimes = isIKNode
                    ? filterIKKeyTimes(ikMergedKeyTimes, seq.startTime, seq.endTime)
                    : (!needsPerFrame && isFKRotNode)
                        ? (rotTypeFK == ControllerType::Euler_XYZ
                            ? collectEulerKeyTimes(rotCtrlFK, seq.startTime, seq.endTime)
                            : collectKeyTimes(rotCtrlFK, seq.startTime, seq.endTime))
                        : generateFrameTimes(seq.startTime, seq.endTime, frameInterval);

                if (logCount == 0) {
                    ALOG << "    ROT seq[" << seq.startTime << "-" << seq.endTime
                         << "] rotTimes=" << rotTimes.size()
                         << " ctrlNumKeys=" << (rotCtrlFK ? rotCtrlFK->NumKeys() : -1)
                         << " isFKRot=" << isFKRotNode
                         << " isIK=" << isIKNode
                         << " needsPerFrame=" << needsPerFrame << "\n";
                }

                for (TimeValue t : rotTimes) {
                    Quat worldRot = extractRotation(maxNode->GetNodeTM(t));

                    Quat parentWorldRot(0.0f,0.0f,0.0f,1.0f);
                    if (parentNode && !parentNode->IsRootNode()) {
                        parentWorldRot = extractRotation(parentNode->GetNodeTM(t));
                    }

                    // Delta-from-bind rotation (matches NeoDex GetRot):
                    // MDX KGRT stores rotation CHANGE from bind pose.
                    Quat worldDelta = invWorldBind * worldRot;
                    Quat parentDelta = invParentWorldBind * parentWorldRot;
                    Quat localRot = worldDelta * Inverse(parentDelta);

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
                             << " worldRot=(" << worldRot.x << "," << worldRot.y << "," << worldRot.z << "," << worldRot.w << ")"
                             << " wDelta=(" << worldDelta.x << "," << worldDelta.y << "," << worldDelta.z << "," << worldDelta.w << ")"
                             << " pDelta=(" << parentDelta.x << "," << parentDelta.y << "," << parentDelta.z << "," << parentDelta.w << ")"
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
                const int frameInterval = 160;

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

                auto sclTimes = isIKNode
                    ? filterIKKeyTimes(ikMergedKeyTimes, seq.startTime, seq.endTime)
                    : (!needsPerFrame)
                        ? collectKeyTimes(sclCtrlFK, seq.startTime, seq.endTime)
                        : generateFrameTimes(seq.startTime, seq.endTime, frameInterval);
                for (TimeValue t : sclTimes) {
                    Matrix3 nodeTM = maxNode->GetNodeTM(t);
                    Matrix3 parTM;
                    parTM.IdentityMatrix();
                    if (parentNode && !parentNode->IsRootNode())
                        parTM = parentNode->GetNodeTM(t);
                    Matrix3 localTM = nodeTM * Inverse(parTM);

                    float sx = Length(localTM.GetRow(0)) / bindSx;
                    float sy = Length(localTM.GetRow(1)) / bindSy;
                    float sz = Length(localTM.GetRow(2)) / bindSz;

                    if (fabsf(sx - 1.0f) < 0.001f) sx = 1.0f;
                    if (fabsf(sy - 1.0f) < 0.001f) sy = 1.0f;
                    if (fabsf(sz - 1.0f) < 0.001f) sz = 1.0f;

                    ir::Keyframe<Point3> key;
                    key.time = t;
                    key.value = Point3(sx, sy, sz);
                    nodeAnim.scale.keys.push_back(key);
                }
            }

            // ── Key reduction: remove keys reconstructable by interpolation ──
            // Tolerances tuned for WC3 MDX quality:
            //   Rotation: 0.01 rad ≈ 0.57° — barely visible, good reduction
            //   Translation: 0.05 units — adequate for scenes up to 300 units
            //   Scale: 0.005 — 0.5% deviation allowed
            {
                size_t trBefore = nodeAnim.translation.keys.size();
                size_t rtBefore = nodeAnim.rotation.keys.size();
                size_t scBefore = nodeAnim.scale.keys.size();

                // Translation tolerance: 0.001 units. Original value before
                // the drift-investigation detour. Drift was caused by a
                // mesh bind-pose bug in mesh_extractor.cpp, not by key
                // density — see useSkinned policy there.
                reduceTranslationKeys(nodeAnim.translation, 0.001f);

                // Detect multi-revolution rotation (>360°) by summing
                // frame-to-frame angular changes. Quaternion slerp can't
                // represent >180° per segment, so key reduction would break
                // continuous rotations like 720° spins.
                float cumulativeAngle = 0.0f;
                for (size_t k = 1; k < nodeAnim.rotation.keys.size(); ++k) {
                    const Quat& q0 = nodeAnim.rotation.keys[k-1].value;
                    const Quat& q1 = nodeAnim.rotation.keys[k].value;
                    float dot = q0.x*q1.x + q0.y*q1.y + q0.z*q1.z + q0.w*q1.w;
                    float absDot = fabsf(dot);
                    if (absDot > 1.0f) absDot = 1.0f;
                    cumulativeAngle += 2.0f * acosf(absDot);
                }
                bool isMultiRevolution = (cumulativeAngle > 6.28318f); // > 360°

                if (isMultiRevolution) {
                    ALOG << "    MULTI-REV detected: cumulative=" << (cumulativeAngle * 57.2958f)
                         << " deg — skipping rotation reduction\n";
                } else {
                    // Tolerance 0.0001 rad ≈ 0.0057°. Tightened from the
                    // original 0.01 rad (0.573°) which was too aggressive
                    // for fine mechanical motion such as chain-link sway:
                    // any animation whose largest delta was below ~0.5°
                    // would be slerp-merged into the boundary keys, leaving
                    // only Identity start/end keys. Anything below 0.0001
                    // rad is true numeric noise (well under one quat-
                    // component LSB at typical float precision).
                    reduceRotationKeys(nodeAnim.rotation, 0.0001f);
                }

                // Scale tolerance: 0.005 — 0.5% deviation allowed.
                reduceTranslationKeys(nodeAnim.scale, 0.005f);

                if (trBefore > 2 || rtBefore > 2 || scBefore > 2) {
                    ALOG << "    REDUCE node[" << nodeIdx << "] '" << irNode.name << "'"
                         << " tr=" << trBefore << "->" << nodeAnim.translation.keys.size()
                         << " rt=" << rtBefore << "->" << nodeAnim.rotation.keys.size()
                         << " sc=" << scBefore << "->" << nodeAnim.scale.keys.size()
                         << "\n";
                }
            }

            // ── Check if this sequence has real animation ──
            if (!isTrackAllZero(nodeAnim.translation))
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
            if (!nodeAnim.rotation.keys.empty())
                hasAnyRealRotation = true;
            if (!isTrackAllIdentityScale(nodeAnim.scale))
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
                key.value = Quat(0.0f, 0.0f, 0.0f, 1.0f);
                restAnim.rotation.keys.push_back(key);
                restAnim.rotation.interpolation = ir::InterpolationType::Linear;
            }

            if (hasAnyRealScale) {
                ir::Keyframe<Point3> key;
                key.time = 0;
                key.value = Point3(1, 1, 1);
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
