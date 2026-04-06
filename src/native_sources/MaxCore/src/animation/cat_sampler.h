// MaxCore — CAT animation sampler (dense 160-tick)
#pragma once

#include "../core/intermediate_types.h"

#include <max.h>
#include <inode.h>

namespace core {

class CATSampler {
public:
    /// Dense-sample a CAT node at 160-tick intervals. Produces Linear tracks.
    void sample(INode* node, INode* parent,
                TimeValue startTime, TimeValue endTime,
                ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl);
};

} // namespace core
