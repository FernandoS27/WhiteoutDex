// MDLXExporter — IR tracks from keyframed Max controllers
//
// One reading of a controller's keys for every extractor that exports a
// parameter animation (PE1, ribbon, camera). The importer creates these
// controllers in createFloatController / createColorController, so the
// interpolation mapping below is the inverse of what it does:
//
//   MDX None     → bezier with STEP tangents on every key
//   MDX Linear   → linear_float (float) / bezier color with LINEAR tangents
//   MDX Hermite  → tcb_float (float) / TCB point3
//   MDX Bezier   → bezier, tangents converted to slopes
//
// Bezier tangents are read back as MDX control-point values through
// core::anim::readFloatKeys' sampling, never as Max's raw slopes.
#pragma once

#include "visibility_track_helper.h"

#include <animation/global_sequence_helper.h>
#include <core/intermediate_types.h>

#include <max.h>
#include <control.h>
#include <istdplug.h>

#include <cmath>
#include <vector>

namespace mdx_extract {

// MDX interpolation for a keyframed float controller.
inline ir::InterpolationType floatControllerInterp(Control* ctrl) {
    if (!ctrl) return ir::InterpolationType::None;
    ULONG cidA = ctrl->ClassID().PartA();
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
        IKeyControl* ikc = GetKeyControlInterface(ctrl);
        return (ikc && detail::bezierKeysAllStep(ikc)) ? ir::InterpolationType::None
                                                       : ir::InterpolationType::Bezier;
    }
    if (cidA == TCBINTERP_FLOAT_CLASS_ID) return ir::InterpolationType::Hermite;
    return ir::InterpolationType::Linear;
}

// Float track from a keyframed float controller. `scale` converts units
// (degrees → radians, say) and applies to the tangents too. Returns the
// index into model.floatTracks, or -1 when the controller has no keys.
inline int32_t extractFloatControllerTrack(Control* ctrl, ir::IRModel& model,
                                           float scale = 1.0f)
{
    if (!ctrl || ctrl->NumKeys() <= 0) return -1;

    ir::Track<float> track;
    track.interpolation = floatControllerInterp(ctrl);

    std::vector<TimeValue> times;
    std::vector<float> values, inTans, outTans;
    bool hasTangents = false;
    const bool typedKeys = detail::isTypedFloatController(ctrl->ClassID().PartA());
    if (typedKeys)
        core::anim::readFloatKeys(ctrl, times, values, inTans, outTans, hasTangents);
    if (times.empty()) {
        // Procedural or boolean controllers: key times + evaluated values.
        for (int i = 0; i < ctrl->NumKeys(); ++i) {
            TimeValue t = ctrl->GetKeyTime(i);
            times.push_back(t);
            values.push_back(core::anim::evalFloat(ctrl, t));
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
        hasTangents = false;
    }

    const bool useTans = hasTangents &&
                         (track.interpolation == ir::InterpolationType::Hermite ||
                          track.interpolation == ir::InterpolationType::Bezier);
    for (size_t i = 0; i < times.size(); ++i) {
        ir::Keyframe<float> key;
        key.time  = times[i];
        key.value = values[i] * scale;
        if (useTans) {
            key.inTangent   = inTans[i] * scale;
            key.outTangent  = outTans[i] * scale;
            key.hasTangents = true;
        }
        track.keys.push_back(key);
    }

    track.globalSequenceIndex = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

// Integer track (e.g. KRTX) from the float controller of an int parameter.
// Values round to the nearest whole number, and an integer track never
// carries tangents, so Bezier / TCB read as Linear.
inline int32_t extractIntControllerTrack(Control* ctrl, ir::IRModel& model)
{
    if (!ctrl || ctrl->NumKeys() <= 0) return -1;

    ir::IntTrack track;
    track.interpolation = floatControllerInterp(ctrl);
    if (track.interpolation != ir::InterpolationType::None)
        track.interpolation = ir::InterpolationType::Linear;

    for (int i = 0; i < ctrl->NumKeys(); ++i) {
        const TimeValue t = ctrl->GetKeyTime(i);
        ir::Keyframe<int32_t> key;
        key.time  = t;
        // An int parameter's controller may hand back an int or a float.
        float v = core::anim::evalFloat(ctrl, t);
        key.value = static_cast<int32_t>(std::lround(v));
        track.keys.push_back(key);
    }
    if (track.keys.empty()) return -1;

    track.globalSequenceIndex = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
    int32_t idx = static_cast<int32_t>(model.intTracks.size());
    model.intTracks.push_back(std::move(track));
    return idx;
}

// Colour track from a Point3 / colour controller. The importer only makes
// bezier colour controllers, so a bezier whose keys are all STEP is None and
// one whose keys are all LINEAR is Linear; anything else is Bezier, with the
// control points solved per channel from samples at 1/3 and 2/3 of each
// segment (see core::anim::readFloatKeys).
inline int32_t extractColorControllerTrack(Control* ctrl, ir::IRModel& model)
{
    if (!ctrl || ctrl->NumKeys() <= 0) return -1;

    auto evalColor = [ctrl](TimeValue t) {
        Point3 v(0.0f, 0.0f, 0.0f);
        Interval iv = FOREVER;
        ctrl->GetValue(t, &v, iv);
        return v;
    };

    ir::ColorTrack track;
    // Key times through the Animatable API: core::anim::collectKeyTimes reads
    // keys as IBezFloatKey, which is smaller than a Point3 key.
    std::vector<TimeValue> times;
    for (int i = 0; i < ctrl->NumKeys(); ++i) times.push_back(ctrl->GetKeyTime(i));
    if (times.empty()) return -1;

    ULONG cidA = ctrl->ClassID().PartA();
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    const bool bezier = (cidA == HYBRIDINTERP_COLOR_CLASS_ID ||
                         cidA == HYBRIDINTERP_POINT3_CLASS_ID) && ikc;
    if (bezier) {
        bool allStep = true, allLinear = true;
        for (int i = 0; i < ikc->GetNumKeys(); ++i) {
            IBezPoint3Key k;
            ikc->GetKey(i, &k);
            const int inT = GetInTanType(k.flags), outT = GetOutTanType(k.flags);
            if (inT != BEZKEY_STEP || outT != BEZKEY_STEP) allStep = false;
            if (inT != BEZKEY_LINEAR || outT != BEZKEY_LINEAR) allLinear = false;
        }
        track.interpolation = allStep   ? ir::InterpolationType::None
                            : allLinear ? ir::InterpolationType::Linear
                                        : ir::InterpolationType::Bezier;
    } else if (cidA == TCBINTERP_POINT3_CLASS_ID) {
        track.interpolation = ir::InterpolationType::Hermite;
    } else {
        track.interpolation = ir::InterpolationType::Linear;
    }

    const size_t n = times.size();
    std::vector<Point3> kv(n);
    for (size_t i = 0; i < n; ++i) kv[i] = evalColor(times[i]);

    for (size_t i = 0; i < n; ++i) {
        ir::Keyframe<Color> key;
        key.time  = times[i];
        key.value = Color(kv[i].x, kv[i].y, kv[i].z);
        if (track.interpolation == ir::InterpolationType::Bezier) {
            Point3 inTan = kv[i], outTan = kv[i];
            if (i > 0) {
                Point3 pe = evalColor(times[i-1] + static_cast<TimeValue>((times[i] - times[i-1]) / 3.0));
                Point3 pd = evalColor(times[i-1] + static_cast<TimeValue>(2.0 * (times[i] - times[i-1]) / 3.0));
                inTan = 3.0f * pd + (2.0f * kv[i-1] - 9.0f * pe - 5.0f * kv[i]) / 6.0f;
            }
            if (i + 1 < n) {
                Point3 pe = evalColor(times[i] + static_cast<TimeValue>((times[i+1] - times[i]) / 3.0));
                Point3 pd = evalColor(times[i] + static_cast<TimeValue>(2.0 * (times[i+1] - times[i]) / 3.0));
                outTan = 3.0f * pe + (2.0f * kv[i+1] - 5.0f * kv[i] - 9.0f * pd) / 6.0f;
            }
            for (int c = 0; c < 3; ++c) {
                if (std::fabs(inTan[c] - kv[i][c]) < 0.01f)  inTan[c]  = kv[i][c];
                if (std::fabs(outTan[c] - kv[i][c]) < 0.01f) outTan[c] = kv[i][c];
            }
            key.inTangent   = Color(inTan.x, inTan.y, inTan.z);
            key.outTangent  = Color(outTan.x, outTan.y, outTan.z);
            key.hasTangents = true;
        } else if (track.interpolation == ir::InterpolationType::Hermite) {
            key.hasTangents = true;  // zero tangents, as for TCB floats
        }
        track.keys.push_back(key);
    }

    // Same Point3-key caveat for the global sequence duration.
    const int afterORT = ctrl->GetORT(ORT_AFTER);
    if (afterORT == ORT_CYCLE || afterORT == ORT_LOOP)
        track.globalSequenceIndex = core::anim::registerGlobalSequence(model, times.back());
    int32_t idx = static_cast<int32_t>(model.colorTracks.size());
    model.colorTracks.push_back(std::move(track));
    return idx;
}

} // namespace mdx_extract
