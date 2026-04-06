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
    float dot = fabsf(core::quatDot(a, b));
    if (dot > 1.0f) dot = 1.0f;
    return 2.0f * acosf(dot);
}

float distFloat(const float& a, const float& b) {
    return fabsf(a - b);
}

// Linear interpolation helpers
Point3 lerpPoint3(const Point3& a, const Point3& b, float t) {
    return a + (b - a) * t;
}

Quat lerpQuat(const Quat& a, const Quat& b, float t) {
    return Slerp(a, b, t);
}

float lerpFloat(const float& a, const float& b, float t) {
    return a + (b - a) * t;
}

} // anonymous namespace

template <typename T>
void KeyframeOptimizer::removeRedundant(ir::Track<T>& track, float threshold,
                                         float (*distFunc)(const T&, const T&)) {
    if (track.keys.size() <= 2) return;

    // Use a simple iterative pass: for each key i (not first or last),
    // check if removing it would change the interpolation beyond threshold.
    std::vector<bool> keep(track.keys.size(), true);

    for (size_t i = 1; i + 1 < track.keys.size(); ++i) {
        const auto& prev = track.keys[i - 1];
        const auto& curr = track.keys[i];
        const auto& next = track.keys[i + 1];

        // Compute parameter t for the current key time within [prev, next]
        float dt = static_cast<float>(next.time - prev.time);
        if (dt <= 0.0f) continue;
        float t = static_cast<float>(curr.time - prev.time) / dt;

        // Check if the value at this key matches linear interpolation
        float dist;
        if constexpr (std::is_same_v<T, Point3>) {
            Point3 interp = lerpPoint3(prev.value, next.value, t);
            dist = distFunc(curr.value, interp);
        } else if constexpr (std::is_same_v<T, Quat>) {
            Quat interp = lerpQuat(prev.value, next.value, t);
            dist = distFunc(curr.value, interp);
        } else if constexpr (std::is_same_v<T, float>) {
            float interp = lerpFloat(prev.value, next.value, t);
            dist = distFunc(curr.value, interp);
        } else {
            dist = 0.0f;
        }

        if (dist < threshold) {
            keep[i] = false;
        }
    }

    // Compact
    std::vector<ir::Keyframe<T>> filtered;
    for (size_t i = 0; i < track.keys.size(); ++i) {
        if (keep[i]) filtered.push_back(track.keys[i]);
    }
    track.keys = std::move(filtered);
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
