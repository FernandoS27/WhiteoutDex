// MaxCore — Keyframe optimizer (remove redundant keys)
#pragma once

#include "../core/intermediate_types.h"

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

private:
    template <typename T>
    void removeRedundant(ir::Track<T>& track, float threshold,
                          float (*distFunc)(const T&, const T&));
};

} // namespace core
