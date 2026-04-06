// MaxCore — CAT animation sampler implementation
#include "cat_sampler.h"
#include "subsample_engine.h"

namespace core {

void CATSampler::sample(INode* node, INode* parent,
                         TimeValue startTime, TimeValue endTime,
                         ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl) {
    SubsampleEngine::SampleConfig config;
    config.tickInterval = 160;        // Every frame at 30fps
    config.adaptiveRefine = false;

    SubsampleEngine::sampleNode(node, parent, startTime, endTime, config,
                                outPos, outRot, outScl);

    outPos.interpolation = ir::InterpolationType::Linear;
    outRot.interpolation = ir::InterpolationType::Linear;
    outScl.interpolation = ir::InterpolationType::Linear;
}

} // namespace core
