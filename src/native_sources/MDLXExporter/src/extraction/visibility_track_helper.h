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
// IBezFloatKey::flags layout (from maxsdk istdplug.h):
//   bits 0..2  : in-tangent type (BEZKEY_STEP == 5)
//   bits 3..5  : out-tangent type
// Masks: IN = 0x7, OUT = 0x38 (shift >> 3)
inline bool bezierKeysAllStep(IKeyControl* ikc) {
    if (!ikc) return false;
    int n = ikc->GetNumKeys();
    if (n <= 0) return false;

    constexpr int kStep = 5;
    constexpr DWORD kInMask  = 0x0007;
    constexpr DWORD kOutMask = 0x0038;

    for (int i = 0; i < n; ++i) {
        IBezFloatKey k;
        ikc->GetKey(i, &k);
        int inT  = static_cast<int>(k.flags & kInMask);
        int outT = static_cast<int>((k.flags & kOutMask) >> 3);
        if (inT != kStep || outT != kStep) return false;
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
                // Only keep tangents if the track is actually Bezier
                // (not downgraded to None via all-step detection).
                if (!isNone) {
                    inTan = k.intan;
                    outTan = k.outtan;
                    hasTangents = true;
                }
            } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
                ITCBFloatKey k;
                ikc->GetKey(i, &k);
                t = k.time;
                value = k.val;
            }
            // No default branch — isTypedFloatController guarantees one
            // of the three above.
        } else {
            // On_Off, boolean_float, and any other keyframeable float
            // controller: read via Animatable NumKeys + GetKeyTime and
            // evaluate the controller at that time to get the value.
            t = ctrl->GetKeyTime(i);
            Interval valid = FOREVER;
            ctrl->GetValue(t, &value, valid);
        }

        // Snap near-boundary values for None-interpolated tracks.
        if (isNone) value = detail::snapBoolValue(value);

        key.time       = t;
        key.value      = value;
        key.inTangent  = hasTangents ? inTan  : 0.0f;
        key.outTangent = hasTangents ? outTan : 0.0f;
        key.hasTangents = hasTangents;
    }

    // ── Global Sequence detection ────────────────────────────────
    int32_t gsIdx = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
    if (gsIdx >= 0)
        track.globalSequenceIndex = gsIdx;

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

} // namespace mdx_extract
