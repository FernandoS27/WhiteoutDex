// MaxCore — IK animation sampler implementation
#include "ik_sampler.h"
#include "subsample_engine.h"

namespace core {

void IKSampler::sample(INode* node, INode* parent,
                        TimeValue startTime, TimeValue endTime,
                        float angleThreshold,
                        ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl) {
    SubsampleEngine::SampleConfig config;
    config.tickInterval = GetTicksPerFrame();  // every frame of the scene
    config.adaptiveRefine = true;
    config.angleThreshold = angleThreshold;
    config.maxDepth = 4;

    SubsampleEngine::sampleNode(node, parent, startTime, endTime, config,
                                outPos, outRot, outScl);

    // IK: use Linear interpolation
    outPos.interpolation = ir::InterpolationType::Linear;
    outRot.interpolation = ir::InterpolationType::Linear;
    outScl.interpolation = ir::InterpolationType::Linear;
}

} // namespace core
