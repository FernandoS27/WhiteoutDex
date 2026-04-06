// MaxCore — Link Constraint animation sampler implementation
#include "link_constraint_sampler.h"
#include "subsample_engine.h"
#include "../util/max_helpers.h"

#include <algorithm>
#include <istdplug.h>

namespace core {

void LinkConstraintSampler::sample(INode* node, INode* parent,
                                    TimeValue startTime, TimeValue endTime,
                                    ir::Vec3Track& outPos, ir::QuatTrack& outRot,
                                    ir::Vec3Track& outScl) {
    // Link constraints need dense sampling with extra keys around parent switches
    SubsampleEngine::SampleConfig config;
    config.tickInterval = 320;
    config.adaptiveRefine = false;

    // First, try to detect parent-switch times from the TM controller
    Control* tmCtrl = node->GetTMController();
    std::vector<TimeValue> switchTimes;

    if (tmCtrl) {
        // Try to get key times from the controller — these often represent switch points
        IKeyControl* ikc = GetKeyControlInterface(tmCtrl);
        if (ikc) {
            int numKeys = ikc->GetNumKeys();
            for (int k = 0; k < numKeys; ++k) {
                TimeValue kt = tmCtrl->GetKeyTime(k);
                if (kt >= startTime && kt <= endTime) {
                    switchTimes.push_back(kt);
                }
            }
        }
    }

    // Run standard dense sampling
    SubsampleEngine::sampleNode(node, parent, startTime, endTime, config,
                                outPos, outRot, outScl);

    // Insert extra samples around switch times (±1 tick)
    for (TimeValue st : switchTimes) {
        TimeValue before = st - 1;
        TimeValue after = st + 1;

        auto insertSample = [&](TimeValue t) {
            if (t < startTime || t > endTime) return;

            Point3 pos, scl;
            Quat rot;
            SubsampleEngine::evaluateLocalTransform(node, parent, t, pos, rot, scl);

            // Check if this time already exists
            auto timeExists = [t](const auto& track) {
                for (const auto& key : track.keys) {
                    if (key.time == t) return true;
                }
                return false;
            };

            if (!timeExists(outPos)) {
                ir::Keyframe<Point3> pk; pk.time = t; pk.value = pos;
                outPos.keys.push_back(pk);
            }
            if (!timeExists(outRot)) {
                if (!outRot.keys.empty())
                    rot = (core::quatDot(outRot.keys.back().value, rot) < 0.0f) ? -rot : rot;
                ir::Keyframe<Quat> rk; rk.time = t; rk.value = rot;
                outRot.keys.push_back(rk);
            }
            if (!timeExists(outScl)) {
                ir::Keyframe<Point3> sk; sk.time = t; sk.value = scl;
                outScl.keys.push_back(sk);
            }
        };

        insertSample(before);
        insertSample(st);
        insertSample(after);
    }

    // Re-sort all tracks by time
    auto sortTrack = [](auto& track) {
        std::sort(track.keys.begin(), track.keys.end(),
                  [](const auto& a, const auto& b) { return a.time < b.time; });
    };
    sortTrack(outPos);
    sortTrack(outRot);
    sortTrack(outScl);

    // Set interpolation to Linear
    outPos.interpolation = ir::InterpolationType::Linear;
    outRot.interpolation = ir::InterpolationType::Linear;
    outScl.interpolation = ir::InterpolationType::Linear;
}

} // namespace core
