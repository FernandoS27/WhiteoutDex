// MaxCore — Transform sampling engine implementation
// Uses GetValue() on controllers (Autodesk-recommended) for FK nodes.
// Uses GetNodeTM() matrix decomposition for IK nodes (GetValue returns
// pre-IK bind pose, not the solved result).
#include "subsample_engine.h"
#include "../util/max_helpers.h"

#include <decomp.h>
#include <control.h>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <unordered_map>

// IKControl interface ID from the Max SDK (IKHierarchy.h)
#ifndef I_IKCONTROL
#define I_IKCONTROL 0x27ab4f01
#endif

// Debug logging — writes to %TEMP%\mdlx_subsample_debug.log
static std::ofstream& ssLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_subsample_debug.log";
        log.open(path, std::ios::trunc);
    }
    return log;
}
#define SSLOG ssLog()
#define SSFLUSH ssLog().flush()

namespace core {

// Detect if a TM controller is IK-driven.
// IK controllers compute transforms dynamically via the solver —
// GetValue() on their sub-controllers returns the static bind pose,
// NOT the IK-solved result. We must use GetNodeTM() + matrix decomp instead.
static bool isIKDriven(Control* tmCtrl) {
    if (!tmCtrl) return false;

    // Check for IKControl interface (used by HD solver bones)
    if (tmCtrl->GetInterface(I_IKCONTROL) != nullptr)
        return true;

    // Check class name as fallback for IKChainControl and variants
    Class_ID cid = tmCtrl->ClassID();
    // IKChainControl ClassID = (0x7B9A27E5, 0x21181E2A) — the HI solver chain object
    if (cid == Class_ID(0x7B9A27E5, 0x21181E2A))
        return true;

    return false;
}

void SubsampleEngine::evaluateLocalTransform(INode* node, INode* parent, TimeValue t,
                                              Point3& outPos, Quat& outRot, Point3& outScl) {
    Control* tmCtrl = node->GetTMController();
    Control* posCtrl = tmCtrl ? tmCtrl->GetPositionController() : nullptr;
    Control* rotCtrl = tmCtrl ? tmCtrl->GetRotationController() : nullptr;
    Control* sclCtrl = tmCtrl ? tmCtrl->GetScaleController() : nullptr;

    // ── IK detection ──
    bool useMatrixDecomp = isIKDriven(tmCtrl);

    // ── Debug: log first few calls per node to show IK vs FK path ──
    static std::unordered_map<INode*, int> nodeLogCount;
    bool shouldLog = false;
    {
        int& count = nodeLogCount[node];
        if (count < 3) {  // Log first 3 samples per node
            shouldLog = true;
            count++;
        }
    }

    if (shouldLog) {
        const MCHAR* name = node->GetName();
        char nameBuf[256] = {};
        if (name) WideCharToMultiByte(CP_UTF8, 0, name, -1, nameBuf, 255, nullptr, nullptr);

        const char* tmClassName = "unknown";
        if (tmCtrl) {
            Class_ID cid = tmCtrl->ClassID();
            // Simple ID for logging
            if (useMatrixDecomp) tmClassName = "IK";
            else tmClassName = "FK";
        }

        SSLOG << "evalLocal '" << nameBuf << "' t=" << t 
              << " path=" << tmClassName << "\n";
    }

    if (!useMatrixDecomp && posCtrl && rotCtrl) {
        // ── GetValue path (FK nodes) ──
        Interval valid = FOREVER;

        Point3 pos(0, 0, 0);
        posCtrl->GetValue(t, &pos, valid, CTRL_ABSOLUTE);
        outPos = pos;

        Quat rot;
        rot.Identity();
        rotCtrl->GetValue(t, &rot, valid, CTRL_ABSOLUTE);
        outRot = rot;

        if (sclCtrl) {
            ScaleValue sv(Point3(1, 1, 1));
            sclCtrl->GetValue(t, &sv, valid, CTRL_ABSOLUTE);
            outScl = Point3(sv.s.x, sv.s.y, sv.s.z);
        } else {
            outScl = Point3(1, 1, 1);
        }

        if (shouldLog) {
            SSLOG << "  FK GetValue: pos=(" << outPos.x << "," << outPos.y << "," << outPos.z
                  << ") rot=(" << outRot.x << "," << outRot.y << "," << outRot.z << "," << outRot.w
                  << ") scl=(" << outScl.x << "," << outScl.y << "," << outScl.z << ")\n";
            SSFLUSH;
        }
    } else {
        // ── Matrix path (IK nodes + fallback) ──
        // Extract rotation by normalizing matrix rows (same as MaxScript's .rotationPart)
        // NOT decomp_affine — that decomposes scale/shear differently and gives
        // wrong quaternions for IK chains.
        Matrix3 nodeTM = node->GetNodeTM(t);
        Matrix3 localTM;

        INode* root = GetCOREInterface()->GetRootNode();
        if (parent && parent != root) {
            Matrix3 parentTM = parent->GetNodeTM(t);
            localTM = nodeTM * Inverse(parentTM);
        } else {
            localTM = nodeTM;
        }

        // Translation
        outPos = localTM.GetRow(3);

        // Scale: measure row lengths
        Point3 r0 = localTM.GetRow(0);
        Point3 r1 = localTM.GetRow(1);
        Point3 r2 = localTM.GetRow(2);
        float sx = Length(r0);
        float sy = Length(r1);
        float sz = Length(r2);

        // Detect mirror (negative determinant)
        float det = DotProd(r0, CrossProd(r1, r2));
        if (det < 0.0f) {
            sx = -sx;  // Negate one axis to absorb the flip
        }

        outScl = Point3(sx, sy, sz);

        // Build pure rotation matrix by normalizing rows
        // This matches MaxScript's .rotationPart
        Matrix3 rotMat(1);
        if (fabsf(sx) > 0.0001f) r0 = r0 / sx; else r0 = Point3(1, 0, 0);
        if (fabsf(sy) > 0.0001f) r1 = r1 / sy; else r1 = Point3(0, 1, 0);
        if (fabsf(sz) > 0.0001f) r2 = r2 / sz; else r2 = Point3(0, 0, 1);
        rotMat.SetRow(0, r0);
        rotMat.SetRow(1, r1);
        rotMat.SetRow(2, r2);
        rotMat.SetRow(3, Point3(0, 0, 0));

        Quat q(rotMat);
        outRot = q;

        if (shouldLog) {
            SSLOG << "  IK RowNorm: pos=(" << outPos.x << "," << outPos.y << "," << outPos.z
                  << ") rot=(" << outRot.x << "," << outRot.y << "," << outRot.z << "," << outRot.w
                  << ") scl=(" << outScl.x << "," << outScl.y << "," << outScl.z
                  << ") det=" << det << "\n";

            // Compare with decomp_affine for diagnosis
            AffineParts parts;
            decomp_affine(localTM, &parts);
            Quat dq = parts.q;
            SSLOG << "  IK decomp(old): rot=(" << dq.x << "," << dq.y << "," << dq.z << "," << dq.w << ")\n";

            // Also show GetValue for comparison
            if (posCtrl && rotCtrl) {
                Interval valid2 = FOREVER;
                Quat gvRot;
                gvRot.Identity();
                rotCtrl->GetValue(t, &gvRot, valid2, CTRL_ABSOLUTE);
                
                float rotDiff = fabsf(outRot.x - gvRot.x) + fabsf(outRot.y - gvRot.y) + 
                                fabsf(outRot.z - gvRot.z) + fabsf(outRot.w - gvRot.w);
                if (rotDiff > 0.001f) {
                    SSLOG << "  ** ROT DIFFERS by " << rotDiff << " — IK solver is active **\n";
                } else {
                    SSLOG << "  (rot same — IK not changing this bone at this time)\n";
                }
            }
            SSFLUSH;
        }
    }
}

void SubsampleEngine::sampleNode(INode* node, INode* parent,
                                  TimeValue startTime, TimeValue endTime,
                                  const SampleConfig& config,
                                  ir::Vec3Track& outPos, ir::QuatTrack& outRot,
                                  ir::Vec3Track& outScl) {
    // Set interpolation type based on config
    outPos.interpolation = ir::InterpolationType::Hermite;
    outRot.interpolation = ir::InterpolationType::Hermite;
    outScl.interpolation = ir::InterpolationType::Hermite;

    // Generate sample times
    std::vector<TimeValue> sampleTimes;
    for (TimeValue t = startTime; t <= endTime; t += config.tickInterval) {
        sampleTimes.push_back(t);
    }
    // Ensure boundary keys
    if (sampleTimes.empty() || sampleTimes.front() != startTime)
        sampleTimes.insert(sampleTimes.begin(), startTime);
    if (sampleTimes.back() != endTime)
        sampleTimes.push_back(endTime);

    // Remove duplicates
    sampleTimes.erase(std::unique(sampleTimes.begin(), sampleTimes.end()), sampleTimes.end());

    // Sample at each time
    for (TimeValue t : sampleTimes) {
        Point3 pos, scl;
        Quat rot;
        evaluateLocalTransform(node, parent, t, pos, rot, scl);

        // Ensure quaternion shortest path consistency
        if (!outRot.keys.empty()) {
            const Quat& prev = outRot.keys.back().value;
            if (core::quatDot(prev, rot) < 0.0f)
                rot = -rot;
        }

        ir::Keyframe<Point3> posKey;
        posKey.time = t;
        posKey.value = pos;
        outPos.keys.push_back(posKey);

        ir::Keyframe<Quat> rotKey;
        rotKey.time = t;
        rotKey.value = rot;
        outRot.keys.push_back(rotKey);

        ir::Keyframe<Point3> sclKey;
        sclKey.time = t;
        sclKey.value = scl;
        outScl.keys.push_back(sclKey);
    }

    // Adaptive refinement for rotation if requested
    if (config.adaptiveRefine && outRot.keys.size() >= 2) {
        size_t origSize = outRot.keys.size();
        for (size_t i = 0; i + 1 < origSize; ++i) {
            TimeValue t0 = outRot.keys[i].time;
            TimeValue t1 = outRot.keys[i + 1].time;
            Quat q0 = outRot.keys[i].value;
            Quat q1 = outRot.keys[i + 1].value;
            adaptiveRefine(node, parent, t0, t1, q0, q1,
                           config.angleThreshold, 0, config.maxDepth,
                           outPos, outRot, outScl);
        }

        auto sortByTime = [](auto& track) {
            std::sort(track.keys.begin(), track.keys.end(),
                      [](const auto& a, const auto& b) { return a.time < b.time; });
            auto last = std::unique(track.keys.begin(), track.keys.end(),
                                    [](const auto& a, const auto& b) { return a.time == b.time; });
            track.keys.erase(last, track.keys.end());
        };
        sortByTime(outPos);
        sortByTime(outRot);
        sortByTime(outScl);
    }

    // Compute Hermite tangents from neighboring samples
    auto computeTangents = [](auto& track) {
        size_t n = track.keys.size();
        if (n < 2) return;
        for (size_t i = 0; i < n; ++i) {
            auto& key = track.keys[i];
            key.hasTangents = true;
            if (i == 0) {
                auto dt = static_cast<float>(track.keys[1].time - key.time);
                if (dt > 0.0f) {
                    key.inTangent = (track.keys[1].value - key.value) / dt;
                    key.outTangent = key.inTangent;
                }
            } else if (i == n - 1) {
                auto dt = static_cast<float>(key.time - track.keys[n - 2].time);
                if (dt > 0.0f) {
                    key.inTangent = (key.value - track.keys[n - 2].value) / dt;
                    key.outTangent = key.inTangent;
                }
            } else {
                auto dt = static_cast<float>(track.keys[i + 1].time - track.keys[i - 1].time);
                if (dt > 0.0f) {
                    key.inTangent = (track.keys[i + 1].value - track.keys[i - 1].value) / dt;
                    key.outTangent = key.inTangent;
                }
            }
        }
    };

    computeTangents(outPos);
    computeTangents(outScl);

    // Quaternion tangents
    {
        size_t n = outRot.keys.size();
        for (size_t i = 0; i < n; ++i) {
            auto& key = outRot.keys[i];
            key.hasTangents = true;
            key.inTangent = key.value;
            key.outTangent = key.value;
        }
    }
}

void SubsampleEngine::adaptiveRefine(INode* node, INode* parent,
                                      TimeValue t0, TimeValue t1,
                                      const Quat& q0, const Quat& q1,
                                      float angleThreshold, int depth, int maxDepth,
                                      ir::Vec3Track& posTrack, ir::QuatTrack& rotTrack,
                                      ir::Vec3Track& sclTrack) {
    if (depth >= maxDepth) return;
    if (t1 - t0 <= 1) return;

    TimeValue tmid = (t0 + t1) / 2;

    Point3 midPos, midScl;
    Quat midRot;
    evaluateLocalTransform(node, parent, tmid, midPos, midRot, midScl);

    if (core::quatDot(q0, midRot) < 0.0f)
        midRot = -midRot;

    Quat interpRot = Slerp(q0, q1, 0.5f);

    Quat diff = interpRot * Inverse(midRot);
    float angle = 2.0f * acosf(std::min(1.0f, fabsf(diff.w)));
    float angleDeg = angle * (180.0f / 3.14159265f);

    if (angleDeg > angleThreshold) {
        ir::Keyframe<Point3> posKey;
        posKey.time = tmid;
        posKey.value = midPos;
        posTrack.keys.push_back(posKey);

        ir::Keyframe<Quat> rotKey;
        rotKey.time = tmid;
        rotKey.value = midRot;
        rotTrack.keys.push_back(rotKey);

        ir::Keyframe<Point3> sclKey;
        sclKey.time = tmid;
        sclKey.value = midScl;
        sclTrack.keys.push_back(sclKey);

        adaptiveRefine(node, parent, t0, tmid, q0, midRot,
                        angleThreshold, depth + 1, maxDepth, posTrack, rotTrack, sclTrack);
        adaptiveRefine(node, parent, tmid, t1, midRot, q1,
                        angleThreshold, depth + 1, maxDepth, posTrack, rotTrack, sclTrack);
    }
}

} // namespace core
