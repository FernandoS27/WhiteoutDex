// MaxCore — IK animation sampler (dense + adaptive refinement)
#pragma once

#include "../core/intermediate_types.h"

#include <max.h>
#include <inode.h>

namespace core {

class IKSampler {
public:
    /// Dense-sample an IK-affected node at 160-tick intervals with adaptive refinement.
    /// Produces Linear tracks.
    void sample(INode* node, INode* parent,
                TimeValue startTime, TimeValue endTime,
                float angleThreshold,
                ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl);
};

} // namespace core
