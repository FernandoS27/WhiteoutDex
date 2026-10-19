// MaxCore — Keyframe optimizer (remove redundant keys)
#pragma once

#include "../core/intermediate_types.h"
#include <utility>
#include <vector>

namespace core {

class KeyframeOptimizer {
public:
    // A rotation segment never ends within ~1° of a half turn from its start:
    // the runtime slerps the shorter arc, and at exactly 180° which arc that
    // is comes down to the sign of a float dot product.
    static constexpr float kHalfTurnDot = 0.01f;

    void optimize(ir::Vec3Track& track, float threshold = 0.001f);
    void optimize(ir::QuatTrack& track, float threshold = 0.0001f);
    void optimize(ir::FloatTrack& track, float threshold = 0.001f);

    // The sequences' time windows. A track that does not run on a global
    // sequence keeps the first and last key inside each window: Warcraft III
    // evaluates a track only by the keys inside the playing sequence, and a
    // sequence left without any falls to the track's default (0, identity),
    // not to the value the keys around it describe.
    void setSequenceWindows(std::vector<std::pair<TimeValue, TimeValue>> windows) {
        windows_ = std::move(windows);
    }

    // Reduce only Linear tracks. A key is judged against the straight line
    // between its neighbours, which is how a Linear track plays: a DontInterp
    // track would step at another frame without it, and a Hermite or Bezier
    // track keeps tangents solved for the spans it had (a TCB 0/50/100 lost
    // its middle key and played an S-curve). Set for parameter tracks.
    void setLinearOnly(bool on) { linearOnly_ = on; }

private:
    std::vector<std::pair<TimeValue, TimeValue>> windows_;
    bool linearOnly_ = false;

    template <typename T>
    void removeRedundant(ir::Track<T>& track, float threshold,
                          float (*distFunc)(const T&, const T&));
};

} // namespace core
