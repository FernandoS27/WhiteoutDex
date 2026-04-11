// MaxCore — Link Constraint animation sampler implementation
// NOTE: The anim_dispatcher resamples ALL tracks (translation, rotation, scale)
// via GetNodeTM after the initial sampling phase. The Link Constraint sampler's
// output is completely overwritten. We produce minimal placeholder keys here
// because GetValue on Link Constraint sub-controllers can be unreliable
// when the parent switches during animation.
#include "link_constraint_sampler.h"

namespace core {

void LinkConstraintSampler::sample(INode* node, INode* parent,
                                    TimeValue startTime, TimeValue endTime,
                                    ir::Vec3Track& outPos, ir::QuatTrack& outRot,
                                    ir::Vec3Track& outScl) {
    // Produce boundary-only placeholder keys.
    // anim_dispatcher will overwrite these with GetNodeTM-based values.
    outPos.interpolation = ir::InterpolationType::Linear;
    outRot.interpolation = ir::InterpolationType::Linear;
    outScl.interpolation = ir::InterpolationType::Linear;

    // Start key
    {
        ir::Keyframe<Point3> pk; pk.time = startTime; pk.value = Point3(0,0,0);
        outPos.keys.push_back(pk);
        ir::Keyframe<Quat> rk; rk.time = startTime; rk.value = Quat(0.0f,0.0f,0.0f,1.0f);
        outRot.keys.push_back(rk);
        ir::Keyframe<Point3> sk; sk.time = startTime; sk.value = Point3(1,1,1);
        outScl.keys.push_back(sk);
    }
    // End key
    {
        ir::Keyframe<Point3> pk; pk.time = endTime; pk.value = Point3(0,0,0);
        outPos.keys.push_back(pk);
        ir::Keyframe<Quat> rk; rk.time = endTime; rk.value = Quat(0.0f,0.0f,0.0f,1.0f);
        outRot.keys.push_back(rk);
        ir::Keyframe<Point3> sk; sk.time = endTime; sk.value = Point3(1,1,1);
        outScl.keys.push_back(sk);
    }
}

} // namespace core
