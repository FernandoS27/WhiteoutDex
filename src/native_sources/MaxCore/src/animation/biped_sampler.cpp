// MaxCore — Biped animation sampler implementation
#include "biped_sampler.h"
#include "subsample_engine.h"

namespace core {

void BipedSampler::sample(INode* node, INode* parent,
                           TimeValue startTime, TimeValue endTime,
                           ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl) {
    SubsampleEngine::SampleConfig config;
    config.tickInterval = 320;       // Every 2 frames at 30fps
    config.adaptiveRefine = false;   // Biped: dense is sufficient

    SubsampleEngine::sampleNode(node, parent, startTime, endTime, config,
                                outPos, outRot, outScl);

    // Override interpolation to Hermite (tangents are computed by SubsampleEngine)
    outPos.interpolation = ir::InterpolationType::Hermite;
    outRot.interpolation = ir::InterpolationType::Hermite;
    outScl.interpolation = ir::InterpolationType::Hermite;
}

} // namespace core
