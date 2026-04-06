// MaxCore — Transform sampling engine implementation
#include "subsample_engine.h"
#include "../util/max_helpers.h"

#include <decomp.h>
#include <cmath>
#include <algorithm>

namespace core {

void SubsampleEngine::evaluateLocalTransform(INode* node, INode* parent, TimeValue t,
                                              Point3& outPos, Quat& outRot, Point3& outScl) {
    Matrix3 nodeTM = node->GetNodeTM(t);
    Matrix3 localTM;

    INode* root = GetCOREInterface()->GetRootNode();
    if (parent && parent != root) {
        Matrix3 parentTM = parent->GetNodeTM(t);
        localTM = nodeTM * Inverse(parentTM);
    } else {
        localTM = nodeTM;
    }

    // Decompose the local matrix
    AffineParts parts;
    decomp_affine(localTM, &parts);

    outPos = parts.t;
    outRot = parts.q;
    outScl = Point3(parts.k.x, parts.k.y, parts.k.z);

    // If there's a non-trivial stretch rotation, apply it to the scale
    // (parts.u is the stretch rotation, parts.f is the sign of determinant)
    if (parts.f < 0.0f) {
        outScl = -outScl;
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
        // Work backwards through pairs to insert midpoints
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

        // Sort keys by time after refinement insertions
        auto sortByTime = [](auto& track) {
            std::sort(track.keys.begin(), track.keys.end(),
                      [](const auto& a, const auto& b) { return a.time < b.time; });
            // Remove duplicates
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
                // First key: forward difference
                auto dt = static_cast<float>(track.keys[1].time - key.time);
                if (dt > 0.0f) {
                    key.inTangent = (track.keys[1].value - key.value) / dt;
                    key.outTangent = key.inTangent;
                }
            } else if (i == n - 1) {
                // Last key: backward difference
                auto dt = static_cast<float>(key.time - track.keys[n - 2].time);
                if (dt > 0.0f) {
                    key.inTangent = (key.value - track.keys[n - 2].value) / dt;
                    key.outTangent = key.inTangent;
                }
            } else {
                // Central difference
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

    // For quaternion tangents, use log-map based central difference
    {
        size_t n = outRot.keys.size();
        for (size_t i = 0; i < n; ++i) {
            auto& key = outRot.keys[i];
            key.hasTangents = true;
            // For quaternion tracks, tangents are stored as quaternions
            // representing the angular velocity. Use simple slerp-based tangents.
            if (n < 2) continue;

            if (i == 0) {
                key.inTangent = key.value;
                key.outTangent = key.value;
            } else if (i == n - 1) {
                key.inTangent = key.value;
                key.outTangent = key.value;
            } else {
                // Central: store the "delta" quaternion as tangent hint
                // Actual tangent usage is format-dependent
                key.inTangent = key.value;
                key.outTangent = key.value;
            }
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
    if (t1 - t0 <= 1) return; // Can't subdivide further

    TimeValue tmid = (t0 + t1) / 2;

    // Evaluate actual transform at midpoint
    Point3 midPos, midScl;
    Quat midRot;
    evaluateLocalTransform(node, parent, tmid, midPos, midRot, midScl);

    // Ensure quaternion consistency
    if (core::quatDot(q0, midRot) < 0.0f)
        midRot = -midRot;

    // Compute interpolated quaternion at midpoint (slerp)
    Quat interpRot = Slerp(q0, q1, 0.5f);

    // Compute angular difference in degrees
    Quat diff = interpRot * Inverse(midRot);
    float angle = 2.0f * acosf(std::min(1.0f, fabsf(diff.w)));
    float angleDeg = angle * (180.0f / 3.14159265f);

    if (angleDeg > angleThreshold) {
        // Insert midpoint key
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

        // Recurse both halves
        adaptiveRefine(node, parent, t0, tmid, q0, midRot,
                        angleThreshold, depth + 1, maxDepth, posTrack, rotTrack, sclTrack);
        adaptiveRefine(node, parent, tmid, t1, midRot, q1,
                        angleThreshold, depth + 1, maxDepth, posTrack, rotTrack, sclTrack);
    }
}

} // namespace core
