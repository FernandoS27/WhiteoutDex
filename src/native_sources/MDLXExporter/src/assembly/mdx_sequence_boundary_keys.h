// MDLXExporter — a key on every sequence's first and last frame
//
// Warcraft III reads a track only through the keys inside the sequence that
// plays (WhiteoutLib `FindBracket`, as read off the 3.0 client). A sequence
// without keys plays the caller's default: 0 for emitter, ribbon and light
// values, 1 for an alpha. Before its first key, or past its last, a sequence
// runs the wrap from the last key round to the first, so a DontInterp track
// shows its LAST key there. Max holds the nearest key instead. A key on each
// sequence's first and last frame, holding what Max shows there, makes the
// game read the same values.
//
// Models our importer made already carry these keys (its timeline remap puts
// a stub on every sequence boundary), so their export does not change.
#pragma once

#include <core/intermediate_types.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace mdx_assembly {

namespace boundary_detail {

template <typename T>
inline T lerp(const T& a, const T& b, float t) { return a + (b - a) * t; }
inline int32_t lerp(int32_t a, int32_t b, float t) {
    return static_cast<int32_t>(std::lround(a + (b - a) * static_cast<double>(t)));
}

template <typename T> inline T times(const T& v, float s) { return v * s; }
inline int32_t times(int32_t v, float s) { return static_cast<int32_t>(std::lround(v * s)); }

// The key Max shows at `t`, inserted into `keys` in time order. Inside a
// Bezier or Hermite span the span is split (de Casteljau), so the curve on
// either side stays exactly what it was.
template <typename T>
void insertKeyAt(ir::Track<T>& track, TimeValue t) {
    auto& keys = track.keys;
    const auto it = std::lower_bound(keys.begin(), keys.end(), t,
        [](const ir::Keyframe<T>& k, TimeValue time) { return k.time < time; });
    if (it != keys.end() && it->time == t) return;

    const bool bezier  = track.interpolation == ir::InterpolationType::Bezier;
    const bool hermite = track.interpolation == ir::InterpolationType::Hermite;
    ir::Keyframe<T> key;
    key.time = t;

    if (it == keys.begin() || it == keys.end()) {
        // Before the first key or past the last: Max holds that key.
        const ir::Keyframe<T>& held = (it == keys.begin()) ? keys.front() : keys.back();
        key.value = held.value;
        if (bezier) { key.inTangent = key.value; key.outTangent = key.value; key.hasTangents = true; }
        if (hermite) {
            // Zero tangents. Max's Color leaves T{} uninitialised.
            key.inTangent = times(key.value, 0.0f); key.outTangent = key.inTangent; key.hasTangents = true;
        }
        keys.insert(it, key);
        return;
    }

    const size_t hi = static_cast<size_t>(it - keys.begin());
    ir::Keyframe<T>& a = keys[hi - 1];
    ir::Keyframe<T>& b = keys[hi];
    const float u = static_cast<float>(t - a.time) / static_cast<float>(b.time - a.time);

    switch (track.interpolation) {
    case ir::InterpolationType::None:
        key.value = a.value;
        break;
    case ir::InterpolationType::Linear:
        key.value = lerp(a.value, b.value, u);
        break;
    default: {
        // Bezier control points; a Hermite tangent m is 3 * (control - key).
        const T p0 = a.value, p1 = b.value;
        const T c1 = bezier ? a.outTangent : p0 + times(a.outTangent, 1.0f / 3.0f);
        const T c2 = bezier ? b.inTangent  : p1 - times(b.inTangent, 1.0f / 3.0f);
        const T q0 = lerp(p0, c1, u), q1 = lerp(c1, c2, u), q2 = lerp(c2, p1, u);
        const T r0 = lerp(q0, q1, u), r1 = lerp(q1, q2, u);
        key.value = lerp(r0, r1, u);
        key.hasTangents = true;
        if (bezier) {
            a.outTangent = q0; key.inTangent = r0; key.outTangent = r1; b.inTangent = q2;
        } else {
            a.outTangent   = times(q0 - p0, 3.0f);
            key.inTangent  = times(key.value - r0, 3.0f);
            key.outTangent = times(r1 - key.value, 3.0f);
            b.inTangent    = times(p1 - q2, 3.0f);
        }
        break;
    }
    }
    keys.insert(keys.begin() + hi, key);
}

template <typename T>
void addBoundaryKeys(ir::Track<T>& track, const std::vector<ir::Sequence>& sequences) {
    // A global sequence runs on its own clock.
    if (track.keys.empty() || track.globalSequenceIndex >= 0) return;
    if (!std::is_sorted(track.keys.begin(), track.keys.end(),
                        [](const ir::Keyframe<T>& x, const ir::Keyframe<T>& y) { return x.time < y.time; }))
        std::stable_sort(track.keys.begin(), track.keys.end(),
                         [](const ir::Keyframe<T>& x, const ir::Keyframe<T>& y) { return x.time < y.time; });
    for (const auto& seq : sequences) {
        if (seq.endTime < seq.startTime) continue;
        insertKeyAt(track, seq.startTime);
        insertKeyAt(track, seq.endTime);
    }
}

} // namespace boundary_detail

// Every parameter track of the model (the pools the extractors fill: emitter,
// ribbon, light, popcorn, camera, geoset, layer and texture values).
// Returns how many keys were added.
inline size_t addSequenceBoundaryKeys(ir::IRModel& model) {
    size_t before = 0, after = 0;
    auto run = [&](auto& pool) {
        for (auto& track : pool) {
            before += track.keys.size();
            boundary_detail::addBoundaryKeys(track, model.sequences);
            after += track.keys.size();
        }
    };
    run(model.floatTracks);
    run(model.vec3Tracks);
    run(model.colorTracks);
    run(model.intTracks);
    return after - before;
}

} // namespace mdx_assembly
