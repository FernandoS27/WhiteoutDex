// MDLXExporter — Shared helper for extracting visibility animation tracks
// from any INode (used by Light, Attachment, Particle1, Particle2, Ribbon
// emitter extractors, as well as GeosetAnim extractor for mesh visibility).
//
// Handles all controller types including on_off_float and boolean_float,
// which do NOT support IKeyControl. For these and similar procedural-ish
// keyframeable controllers we fall back to Animatable::NumKeys +
// GetKeyTime + GetValue.
//
// CHANGES 2026-04-18: Global Sequence detection (after-ORT = CYCLE/LOOP)
// tags tracks with ir::Track::globalSequenceIndex so the MDX writer emits
// the correct KGAO/KATV etc. global-sequence reference.
//
// CHANGES 2026-04-23a: NeoDex-compatible interpolation detection.
// bezier_float controllers with ALL keys using #step tangents are
// downgraded to IR InterpolationType::None — this matches NeoDex export
// convention (see Wc3Animation.ms::controllerIsNonInterp + FloatType).
// Both On_Off (our native importer output) AND boolean_float (current
// importer output) AND bezier+step (NeoDex-imported scenes the user
// re-exports with this tool) all resolve to MDX interpolationType = 0
// (None), producing byte-compatible output regardless of which importer
// populated the scene.
//
// Value-snap: for tracks classified as None, we clamp each key's value
// to exactly 0.0 or 1.0 when within 0.01 of those boundaries. This
// defends against Float-noise from Max's internal conversion of
// Boolean/On_Off state to float (Handoff §10 Pitfall #1).
//
// CHANGES 2026-04-23b: Restrict the typed-key (IKeyControl) path to the
// three documented controller classes: Linear, Bezier, TCB. Previously,
// any controller that returned a non-null IKeyControl pointer took the
// typed-key branch and read keys as IBezFloatKey — but boolean_float
// and on_off DO expose IKeyControl while their keys have an entirely
// different binary layout. Reading them as IBezFloatKey silently
// corrupted the time stamps, causing most keys to collide onto the same
// MS after the tick→ms conversion, and the MDX writer then deduped
// them down to a handful of survivors. The fix is to use the
// GetKeyTime + GetValue fallback for any controller that isn't one of
// the three documented IKeyControl clients.
//
#pragma once

#include <core/intermediate_types.h>
#include <animation/global_sequence_helper.h>

#include <max.h>
#include <inode.h>
#include <control.h>
#include <istdplug.h>

#include <cmath>

namespace mdx_extract {

namespace detail {

// ── Step-tangent detection for bezier_float controllers ──────────
//
// NeoDex convention (Wc3Animation.ms::controllerIsNonInterp): a
// bezier_float controller whose *every* key has both inTangentType
// AND outTangentType == #step is semantically equivalent to a
// None / DontInterp controller. Export it as MDX interpolationType=0.
//
// Uses the SDK macros GetInTanType/GetOutTanType and BEZKEY_STEP
// from istdplug.h — these handle the correct bit positions
// (bits 7-12 in the flags DWORD, NOT bits 0-5).
inline bool bezierKeysAllStep(IKeyControl* ikc) {
    if (!ikc) return false;
    int n = ikc->GetNumKeys();
    if (n <= 0) return false;

    for (int i = 0; i < n; ++i) {
        IBezFloatKey k;
        ikc->GetKey(i, &k);
        int inT  = GetInTanType(k.flags);
        int outT = GetOutTanType(k.flags);
        if (inT != BEZKEY_STEP || outT != BEZKEY_STEP) return false;
    }
    return true;
}

// Is this controller class one of the three documented IKeyControl
// clients (linear/bezier/tcb float)? Other controllers — including
// boolean_float and on_off — may return a non-null IKeyControl pointer
// but their key data has a completely different binary layout, so we
// must NOT read them as ILinFloatKey/IBezFloatKey/ITCBFloatKey. Use
// the Animatable GetKeyTime + Control::GetValue fallback for those.
inline bool isTypedFloatController(ULONG cidA) {
    return cidA == LININTERP_FLOAT_CLASS_ID
        || cidA == HYBRIDINTERP_FLOAT_CLASS_ID
        || cidA == TCBINTERP_FLOAT_CLASS_ID;
}

// Map Max controller class → IR interpolation type.
//
// For bezier_float the caller passes the IKeyControl so we can inspect
// tangent types: all-step ⇒ None, otherwise Bezier.
inline ir::InterpolationType detectVisInterp(Control* ctrl, IKeyControl* ikc) {
    if (!ctrl) return ir::InterpolationType::None;
    ULONG cidA = ctrl->ClassID().PartA();

    if (cidA == LININTERP_FLOAT_CLASS_ID) return ir::InterpolationType::Linear;
    if (cidA == TCBINTERP_FLOAT_CLASS_ID) return ir::InterpolationType::Hermite;
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
        // NeoDex-compatible: bezier+all-step is really a None track.
        if (bezierKeysAllStep(ikc)) return ir::InterpolationType::None;
        return ir::InterpolationType::Bezier;
    }
    // On_Off, boolean_float, step, or any other non-interpolating float
    // controller — all map to MDX None / DontInterp.
    return ir::InterpolationType::None;
}

// Snap near-boundary floats to exactly 0.0 or 1.0. Defends against
// Float-noise on Boolean/On_Off conversions and on bezier+step keys
// that may have been touched by Max's internal evaluator.
inline float snapBoolValue(float v) {
    if (std::fabs(v)        < 0.01f) return 0.0f;
    if (std::fabs(v - 1.0f) < 0.01f) return 1.0f;
    return v;
}

} // namespace detail

// Extract visibility animation from a node's visibility controller.
// Returns index into model.floatTracks, or -1 if no animation / static only.
//
// Works for:
//   - Linear / Bezier / TCB float (typed-key IKeyControl path)
//   - On_Off, boolean_float, and any other keyframeable float
//     controller (Animatable::NumKeys + Control::GetValue fallback)
//
// Relies on the importer having already placed boundary stubs at every
// sequence start/end — both NeoDex and our own importer do this. The
// exporter therefore reads raw keys and performs NO per-sequence
// synthesis of its own (matching NeoDex FloatKeys exactly).
//
// Global Sequence support: if the visibility controller's after-ORT is
// ORT_CYCLE or ORT_LOOP, the controller is treated as a Global Sequence:
// its duration (time of last key) is registered in the IR model's
// globalSequenceDurations and the resulting track's globalSequenceIndex
// is set to the 0-based index.
inline int32_t extractVisibilityTrack(INode* node, ir::IRModel& model) {
    if (!node) return -1;
    Control* ctrl = node->GetVisController();
    if (!ctrl) return -1;

    ULONG cidA = ctrl->ClassID().PartA();

    IKeyControl* ikc = GetKeyControlInterface(ctrl);

    // Only take the typed-key path if the controller is one of the three
    // documented IKeyControl clients. boolean_float and on_off can return
    // a non-null IKeyControl pointer but their keys are NOT ILin/IBez/ITCB
    // float keys — reading them as such silently corrupts both time and
    // value fields.
    const bool useTypedKeys = (ikc != nullptr) && detail::isTypedFloatController(cidA);

    int nkeys = useTypedKeys ? ikc->GetNumKeys() : ctrl->NumKeys();
    if (nkeys <= 0) return -1;

    ir::Track<float> track;
    track.interpolation = detail::detectVisInterp(ctrl, ikc);
    const bool isNone = (track.interpolation == ir::InterpolationType::None);

    // ── Bezier with tangents: NeoDex-compatible sampling ──────────
    // Max's IBezFloatKey.intan/.outtan are tangent SLOPES (dQ/dt),
    // NOT the control-point values that MDX expects. NeoDex solves
    // this by sampling the controller at 1/3 and 2/3 points between
    // keys and applying BezierInTan/BezierOutTan formulas.
    const bool isBezierWithTangents = useTypedKeys
        && (cidA == HYBRIDINTERP_FLOAT_CLASS_ID)
        && !isNone;

    if (isBezierWithTangents) {
        // Pass 1: read key times and evaluate values
        struct TK { TimeValue time; float value; float sIn; float sOut; };
        std::vector<TK> tk(nkeys);
        for (int i = 0; i < nkeys; i++) {
            IBezFloatKey k; ikc->GetKey(i, &k);
            tk[i].time = k.time;
            Interval v = FOREVER;
            ctrl->GetValue(k.time, &tk[i].value, v);
        }

        // Pass 2: sample at 1/3 and 2/3 points between adjacent keys
        for (int i = 0; i < nkeys; i++) {
            if (i > 0) {
                TimeValue t23 = tk[i-1].time +
                    (TimeValue)(2.0 * (double)(tk[i].time - tk[i-1].time) / 3.0);
                Interval v = FOREVER;
                ctrl->GetValue(t23, &tk[i].sIn, v);
            } else {
                tk[i].sIn = tk[i].value;
            }
            if (i < nkeys - 1) {
                TimeValue t13 = tk[i].time +
                    (TimeValue)((double)(tk[i+1].time - tk[i].time) / 3.0);
                Interval v = FOREVER;
                ctrl->GetValue(t13, &tk[i].sOut, v);
            } else {
                tk[i].sOut = tk[i].value;
            }
        }

        // Pass 3: compute MDX Bezier control points
        // BezierOutTan(pe, pd, a, d) = 3*pe + (2*d - 5*a - 9*pd) / 6
        // BezierInTan (pe, pd, a, d) = 3*pd + (2*a - 9*pe - 5*d) / 6
        for (int i = 0; i < nkeys; i++) {
            float val = tk[i].value;
            float inTan, outTan;

            if (i > 0) {
                float pe = tk[i-1].sOut, pd = tk[i].sIn;
                float a = tk[i-1].value, d = tk[i].value;
                inTan = 3.0f*pd + (2.0f*a - 9.0f*pe - 5.0f*d) / 6.0f;
            } else {
                inTan = val;
            }
            if (i < nkeys - 1) {
                float pe = tk[i].sOut, pd = tk[i+1].sIn;
                float a = tk[i].value, d = tk[i+1].value;
                outTan = 3.0f*pe + (2.0f*d - 5.0f*a - 9.0f*pd) / 6.0f;
            } else {
                outTan = val;
            }

            // Snap tangents near value (NeoDex ProcessBezier)
            if (std::fabs(inTan - val) < 0.01f) inTan = val;
            if (std::fabs(outTan - val) < 0.01f) outTan = val;

            ir::Keyframe<float> key;
            key.time       = tk[i].time;
            key.value      = val;
            key.inTangent  = inTan;
            key.outTangent = outTan;
            key.hasTangents = true;
            track.keys.push_back(key);
        }
    } else {
    // ── All other controller types: original single-pass loop ────
    for (int i = 0; i < nkeys; i++) {
        track.keys.push_back({});
        auto& key = track.keys.back();

        TimeValue t = 0;
        float value = 0.0f;

        if (useTypedKeys) {
            if (cidA == LININTERP_FLOAT_CLASS_ID) {
                ILinFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                value = k.val;
            } else if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
                // Bezier downgraded to None (all-step): read value only
                IBezFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                value = k.val;
            } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
                ITCBFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                value = k.val;
            }
        } else {
            t = ctrl->GetKeyTime(i);
            Interval valid = FOREVER;
            ctrl->GetValue(t, &value, valid);
        }

        // Snap near-boundary values for None-interpolated tracks.
        if (isNone) value = detail::snapBoolValue(value);

        key.time       = t;
        key.value      = value;
        key.inTangent  = 0.0f;
        key.outTangent = 0.0f;
        key.hasTangents = false;
    }
    } // else

    // ── Global Sequence detection ────────────────────────────────
    int32_t gsIdx = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
    if (gsIdx >= 0)
        track.globalSequenceIndex = gsIdx;

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

} // namespace mdx_extract
