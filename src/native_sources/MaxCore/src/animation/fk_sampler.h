// MaxCore — FK animation sampler
#pragma once

#include "../core/intermediate_types.h"

#include <max.h>
#include <inode.h>
#include <control.h>

namespace core {

class FKSampler {
public:
    /// Extract keyframes from native FK controllers (Bezier, TCB, Linear).
    /// Converts world-space keys to local-space using SubsampleEngine.
    /// Returns true if successful, false if controller is unsupported.
    bool sample(INode* node, INode* parent,
                TimeValue startTime, TimeValue endTime,
                ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl);

private:
    void samplePosition(Control* posCtrl, INode* node, INode* parent,
                         TimeValue startTime, TimeValue endTime,
                         ir::Vec3Track& outPos);
    void sampleRotation(Control* rotCtrl, INode* node, INode* parent,
                         TimeValue startTime, TimeValue endTime,
                         ir::QuatTrack& outRot);
    void sampleScale(Control* sclCtrl, INode* node, INode* parent,
                      TimeValue startTime, TimeValue endTime,
                      ir::Vec3Track& outScl);
};

} // namespace core
