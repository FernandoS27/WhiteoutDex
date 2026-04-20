// MDLXExporter — Shared helper for extracting visibility animation tracks
// from any INode (used by Light, Attachment, Particle1, Particle2, Ribbon
// emitter extractors, as well as GeosetAnim extractor for mesh visibility).
//
// Handles all controller types including on_off_float, which does NOT
// support IKeyControl. For on_off and similar procedural-ish keyframeable
// controllers we fall back to Animatable::NumKeys + GetKeyTime + GetValue.
//
// CHANGES 2026-04-18: Now also detects Global Sequences on the visibility
// controller (after-ORT = CYCLE or LOOP) and tags the resulting track
// with ir::Track::globalSequenceIndex so the MDX writer emits the correct
// KGAO/KATV etc. global-sequence reference.
//
#pragma once

#include <core/intermediate_types.h>
#include <animation/global_sequence_helper.h>

#include <max.h>
#include <inode.h>
#include <control.h>
#include <istdplug.h>

namespace mdx_extract {

namespace detail {

// Map Max controller class → IR interpolation type
inline ir::InterpolationType detectVisInterp(Control* ctrl) {
    if (!ctrl) return ir::InterpolationType::None;
    ULONG cidA = ctrl->ClassID().PartA();
    if (cidA == LININTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Linear;
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) return ir::InterpolationType::Bezier;
    if (cidA == TCBINTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Hermite;
    // On/Off, step, Boolean, or any other non-interpolating float controller
    return ir::InterpolationType::None;
}

} // namespace detail

// Extract visibility animation from a node's visibility controller.
// Returns index into model.floatTracks, or -1 if no animation / static only.
//
// Works for:
//   - Linear/Bezier/TCB float (via IKeyControl, typed key structs)
//   - On/Off, Boolean, and other procedural keyframeable controllers
//     (via Animatable::NumKeys + Control::GetValue)
//
// Global Sequence support: if the visibility controller's after-ORT is
// ORT_CYCLE or ORT_LOOP, the controller is treated as a Global Sequence:
// its duration (time of last key) is registered in the IR model's
// globalSequenceDurations and the resulting track's globalSequenceIndex
// is set to the 0-based index. The MDX writer will then emit the correct
// global-sequence reference in the KGAO / KATV / etc. chunk for the
// containing object (GeosetAnim, Light, Attachment, Particle, Ribbon).
inline int32_t extractVisibilityTrack(INode* node, ir::IRModel& model) {
    if (!node) return -1;
    Control* ctrl = node->GetVisController();
    if (!ctrl) return -1;

    ULONG cidA = ctrl->ClassID().PartA();

    // Try typed-key path first
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    bool useTypedKeys = (ikc != nullptr);

    int nkeys = useTypedKeys ? ikc->GetNumKeys() : ctrl->NumKeys();
    if (nkeys <= 0) return -1;

    ir::Track<float> track;
    track.interpolation = detail::detectVisInterp(ctrl);

    for (int i = 0; i < nkeys; i++) {
        track.keys.push_back({});
        auto& key = track.keys.back();

        TimeValue t = 0;
        float value = 0.0f;
        float inTan = 0.0f, outTan = 0.0f;
        bool hasTangents = false;

        if (useTypedKeys) {
            if (cidA == LININTERP_FLOAT_CLASS_ID) {
                ILinFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                value = k.val;
            } else if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
                IBezFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                value = k.val;
                inTan = k.intan;
                outTan = k.outtan;
                hasTangents = true;
            } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
                ITCBFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                value = k.val;
            } else {
                // Unknown typed-key controller — use Bez struct for time,
                // evaluate for the value.
                IBezFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                Interval valid = FOREVER;
                ctrl->GetValue(t, &value, valid);
            }
        } else {
            // On_Off, Boolean, or similar: use Animatable key API.
            t = ctrl->GetKeyTime(i);
            Interval valid = FOREVER;
            ctrl->GetValue(t, &value, valid);
        }

        key.time       = t;
        key.value      = value;
        key.inTangent  = hasTangents ? inTan  : 0.0f;
        key.outTangent = hasTangents ? outTan : 0.0f;
        key.hasTangents = hasTangents;
    }

    // ── Global Sequence detection ────────────────────────────────
    // If the visibility controller's after-ORT is cyclic (ORT_CYCLE
    // or ORT_LOOP), register the duration in the IR model and tag
    // this track with the Global Sequence index so the MDX writer
    // emits the correct global-seq reference.
    //
    // Handles both IKeyControl-backed controllers (Linear/Bezier/TCB)
    // and non-IKeyControl ones (on_off_float, boolean, procedural) —
    // the helper has a fallback for the latter.
    int32_t gsIdx = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
    if (gsIdx >= 0)
        track.globalSequenceIndex = gsIdx;

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

} // namespace mdx_extract
