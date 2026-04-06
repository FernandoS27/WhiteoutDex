// MaxCore — Biped animation sampler (dense 320-tick)
#pragma once

#include "../core/intermediate_types.h"

#include <max.h>
#include <inode.h>

namespace core {

class BipedSampler {
public:
    /// Dense-sample a biped node at 320-tick intervals (2 frames at 30fps).
    /// Produces Hermite tracks with tangents computed from central differences.
    void sample(INode* node, INode* parent,
                TimeValue startTime, TimeValue endTime,
                ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl);
};

} // namespace core
