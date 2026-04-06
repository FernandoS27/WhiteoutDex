// MaxCore — Link Constraint animation sampler
#pragma once

#include "../core/intermediate_types.h"

#include <max.h>
#include <inode.h>

namespace core {

class LinkConstraintSampler {
public:
    /// Dense-sample a Link Constraint node at 320-tick intervals.
    /// Inserts extra keys at parent-switch boundaries ±1 tick.
    void sample(INode* node, INode* parent,
                TimeValue startTime, TimeValue endTime,
                ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl);
};

} // namespace core
