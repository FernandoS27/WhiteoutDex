// MaxCore — Keyframe optimizer implementation
#include "keyframe_optimizer.h"
#include "../util/max_helpers.h"
#include <cmath>

namespace core {

namespace {

float distPoint3(const Point3& a, const Point3& b) {
    return Length(a - b);
}

float distQuat(const Quat& a, const Quat& b) {
    return quatAngle(a, b);
}

float distFloat(const float& a, const float& b) {
    return fabsf(a - b);
}

Point3 interpolate(const Point3& a, const Point3& b, float t) {
    return a + (b - a) * t;
}

float interpolate(const float& a, const float& b, float t) {
    return a + (b - a) * t;
}

// Shortest-arc slerp, the way the game plays a Linear rotation track (it
// negates b when dot < 0) — so a segment is judged by the arc it will get.
Quat interpolate(const Quat& a, const Quat& b, float t) {
    float dot = quatDot(a, b);
    Quat end = b;
    if (dot < 0.0f) {
        end.x = -end.x; end.y = -end.y; end.z = -end.z; end.w = -end.w;
        dot = -dot;
    }
    float wa = 1.0f - t, wb = t;
    if (dot < 0.9999f) {
        float theta = acosf(dot);
        float sinTheta = sinf(theta);
        wa = sinf((1.0f - t) * theta) / sinTheta;
        wb = sinf(t * theta) / sinTheta;
    }
    Quat r;
    r.x = wa*a.x + wb*end.x;
    r.y = wa*a.y + wb*end.y;
    r.z = wa*a.z + wb*end.z;
    r.w = wa*a.w + wb*end.w;
    float len = sqrtf(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
    if (len > 0.0f) { r.x /= len; r.y /= len; r.z /= len; r.w /= len; }
    return r;
}

bool spanAllowed(const Point3&, const Point3&) { return true; }
bool spanAllowed(const float&, const float&) { return true; }
bool spanAllowed(const Quat& a, const Quat& b) {
    return fabsf(quatDot(a, b)) > KeyframeOptimizer::kHalfTurnDot;
}

} // anonymous namespace

template <typename T>
void KeyframeOptimizer::removeRedundant(ir::Track<T>& track, float threshold,
                                         float (*distFunc)(const T&, const T&)) {
    if (track.keys.size() <= 2) return;

    // Greedy from the last KEPT key: a key is dropped only if the segment
    // replacing it still reproduces every key dropped since that anchor.
    // Judging each key against its original neighbours alone let whole runs
    // go at once — a constant-speed spin (every key the exact slerp of its
    // neighbours) collapsed to its two end keys, i.e. no rotation at all.
    std::vector<ir::Keyframe<T>> kept{track.keys.front()};
    size_t anchor = 0;
    for (size_t i = 1; i + 1 < track.keys.size(); ++i) {
        const auto& from = track.keys[anchor];
        const auto& to = track.keys[i + 1];
        const float dt = static_cast<float>(to.time - from.time);
        bool canSkip = dt > 0.0f && spanAllowed(from.value, to.value);
        for (size_t j = anchor + 1; canSkip && j <= i; ++j) {
            const float t = static_cast<float>(track.keys[j].time - from.time) / dt;
            canSkip = distFunc(track.keys[j].value,
                               interpolate(from.value, to.value, t)) < threshold;
        }
        if (!canSkip) {
            kept.push_back(track.keys[i]);
            anchor = i;
        }
    }
    kept.push_back(track.keys.back());
    track.keys = std::move(kept);
}

void KeyframeOptimizer::optimize(ir::Vec3Track& track, float threshold) {
    removeRedundant(track, threshold, distPoint3);
}

void KeyframeOptimizer::optimize(ir::QuatTrack& track, float threshold) {
    removeRedundant(track, threshold, distQuat);
}

void KeyframeOptimizer::optimize(ir::FloatTrack& track, float threshold) {
    removeRedundant(track, threshold, distFloat);
}

} // namespace core
