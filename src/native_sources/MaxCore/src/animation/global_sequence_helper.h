// MaxCore — Global Sequence & float-controller helpers
//
// Shared infrastructure for extracting animation data that originates from
// IParamBlock2 params or INode sub-controllers. Everything in here is
// format-agnostic — it works on ir::IRModel and Max SDK types and knows
// nothing about MDX.
//
// Intended destination:
//     MaxCore/src/animation/global_sequence_helper.h
// Include as:
//     #include <animation/global_sequence_helper.h>
//
// Provenance: the helpers in this file were first written as static
// functions inside MDLXExporter/src/extraction/wc3_material_extractor.cpp.
// They've been lifted here verbatim so other extractors can reuse them
// without copy-paste drift. The material extractor's statics can later
// be migrated to call into this header; for now both can coexist (they
// live in different namespaces and the material-extractor ones are
// internal-linkage so there is no ODR conflict).
//
// ─── Global Sequence model ──────────────────────────────────────────
// A Max controller qualifies as a Global Sequence iff its after-ORT is
// ORT_CYCLE (2) or ORT_LOOP (3). MaxScript's `#cycle` maps to ORT_CYCLE;
// both values are treated identically by Max's built-in controller code
// (both dispatch to CycleTime), so we accept both.
//
// The GS duration is defined as the time of the last key, in TimeValue
// ticks (4800 ticks/sec). ir::IRModel::globalSequenceDurations stores
// these durations *in ticks*; the MDX model builder converts to
// milliseconds via mdx_transform::ticksToMs() when writing the GLBS
// chunk.
//
#pragma once

#include <core/intermediate_types.h>

#include <max.h>
#include <control.h>
#include <iparamb2.h>
#include <istdplug.h>
#include <ref.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace core::anim {

// ─── ParamBlock2 helpers ────────────────────────────────────────────
// IParamBlock2 doesn't expose "get the controller for parameter X by name"
// directly. Animated params are published as sub-anims; the path is:
//     ParamID pid  =  desc->paramdefs[i].ID where int_name matches
//     int animIdx  =  pb->GetAnimNum(pid, 0)
//     Animatable*  =  pb->SubAnim(animIdx)
//     Control*     =  GetControlInterface(anim)
// ────────────────────────────────────────────────────────────────────

// Find a named parameter in any IParamBlock2 on the reference target.
// Returns true on match and sets outPB/outPID; false otherwise.
inline bool findAnimParam(ReferenceTarget* ref, const wchar_t* name,
                          IParamBlock2*& outPB, ParamID& outPID)
{
    if (!ref || !name) return false;
    for (int i = 0; i < ref->NumParamBlocks(); i++) {
        IParamBlock2* pb = ref->GetParamBlock(i);
        if (!pb) continue;
        ParamBlockDesc2* desc = pb->GetDesc();
        if (!desc) continue;
        for (int j = 0; j < desc->Count(); j++) {
            ParamID pid = desc->IndextoID(j);
            const ParamDef& pd = desc->GetParamDef(pid);
            if (pd.int_name && _wcsicmp(pd.int_name, name) == 0) {
                outPB = pb;
                outPID = pid;
                return true;
            }
        }
    }
    return false;
}

// Get the Control* assigned to a named animatable parameter, or nullptr
// if no such parameter exists or it isn't animated.
inline Control* getParamController(ReferenceTarget* ref, const wchar_t* name) {
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    if (!findAnimParam(ref, name, pb, pid)) return nullptr;
    int animIdx = pb->GetAnimNum(pid, 0);
    if (animIdx < 0 || animIdx >= pb->NumSubs()) return nullptr;
    Animatable* anim = pb->SubAnim(animIdx);
    if (!anim) return nullptr;
    return GetControlInterface(anim);
}

// Direct controller access — Autodesk's recommended API per IParamBlock2 docs.
// GetControllerByID bypasses the SubAnim wrapper chain which is unreliable
// for scripted plugins (MSPlugin returns a Max-internal container that
// GetControlInterface can't cast to Control*, producing NULL or worse,
// a garbage pointer that crashes on deref).
//
// Used exclusively by the KGAC path (Wc3VertexMod) — the material extractor
// still uses the old SubAnim path which works fine for its float params.
inline Control* getParamControllerDirect(ReferenceTarget* ref, const wchar_t* name) {
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    if (!findAnimParam(ref, name, pb, pid)) return nullptr;
    if (!pb) return nullptr;
    return pb->GetControllerByID(pid, 0);
}

// ─── Controller evaluation & classification ─────────────────────────

// Evaluate a float controller at a given time; returns defaultVal for null.
inline float evalFloat(Control* ctrl, TimeValue t, float defaultVal = 0.0f) {
    if (!ctrl) return defaultVal;
    float val = defaultVal;
    Interval valid = FOREVER;
    ctrl->GetValue(t, &val, valid);
    return val;
}

// Map a Max float-controller ClassID to an IR interpolation type.
// Returns None for unknown / non-interpolating controllers (on_off,
// boolean, procedural — those need special-case handling by callers).
inline ir::InterpolationType detectInterpFromController(Control* ctrl) {
    if (!ctrl) return ir::InterpolationType::None;
    ULONG cidA = ctrl->ClassID().PartA();
    if (cidA == LININTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Linear;
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) return ir::InterpolationType::Bezier;
    if (cidA == TCBINTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Hermite;
    return ir::InterpolationType::None;
}

// Collect all key times from any keyframed float controller.
// Falls back to Animatable::NumKeys + GetKeyTime for controllers that
// don't expose IKeyControl (on_off_float, boolean, procedural).
inline std::vector<TimeValue> collectKeyTimes(Control* ctrl) {
    std::vector<TimeValue> times;
    if (!ctrl) return times;
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (ikc) {
        int n = ikc->GetNumKeys();
        times.reserve(n);
        ULONG cidA = ctrl->ClassID().PartA();
        for (int i = 0; i < n; i++) {
            TimeValue t = 0;
            if (cidA == LININTERP_FLOAT_CLASS_ID) {
                ILinFloatKey k; ikc->GetKey(i, &k); t = k.time;
            } else if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
                IBezFloatKey k; ikc->GetKey(i, &k); t = k.time;
            } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
                ITCBFloatKey k; ikc->GetKey(i, &k); t = k.time;
            } else {
                // All float key struct layouts begin with TimeValue;
                // IBezFloatKey is safe to read just for the time field.
                IBezFloatKey k; ikc->GetKey(i, &k); t = k.time;
            }
            times.push_back(t);
        }
    } else {
        int n = ctrl->NumKeys();
        times.reserve(n);
        for (int i = 0; i < n; i++) times.push_back(ctrl->GetKeyTime(i));
    }
    return times;
}

// Read a float controller's keys into parallel time/value/in-tan/out-tan
// vectors. hasTangents is set to true iff the controller is Hermite or
// Bezier (only those have meaningful tangents). For Linear / unknown /
// TCB, tangents are zero-filled (TCB→Bezier conversion is non-trivial;
// MDX readers that get Hermite with zero tangents interpolate smoothly
// which is visually close enough in practice).
//
// Does nothing for controllers without IKeyControl — use collectKeyTimes
// + evalFloat in that case.
inline void readFloatKeys(Control* ctrl,
                          std::vector<TimeValue>& times,
                          std::vector<float>& values,
                          std::vector<float>& inTans,
                          std::vector<float>& outTans,
                          bool& hasTangents)
{
    hasTangents = false;
    if (!ctrl) return;
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) return;
    int n = ikc->GetNumKeys();
    if (n == 0) return;

    ULONG cidA = ctrl->ClassID().PartA();
    times.reserve(n); values.reserve(n);
    inTans.reserve(n); outTans.reserve(n);

    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
        hasTangents = true;
        for (int i = 0; i < n; i++) {
            IBezFloatKey k; ikc->GetKey(i, &k);
            times.push_back(k.time);
            values.push_back(k.val);
            inTans.push_back(k.intan);
            outTans.push_back(k.outtan);
        }
    } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
        // TCB keys get zero tangents — proper TCB→Hermite conversion
        // requires the tens/cont/bias coefficients. Zero tangents give
        // smooth interpolation in viewers, which is acceptable.
        hasTangents = true;
        for (int i = 0; i < n; i++) {
            ITCBFloatKey k; ikc->GetKey(i, &k);
            times.push_back(k.time);
            values.push_back(k.val);
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    } else if (cidA == LININTERP_FLOAT_CLASS_ID) {
        for (int i = 0; i < n; i++) {
            ILinFloatKey k; ikc->GetKey(i, &k);
            times.push_back(k.time);
            values.push_back(k.val);
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    } else {
        // Unknown / other: read time via Bez header, value via GetValue.
        for (int i = 0; i < n; i++) {
            IBezFloatKey k; ikc->GetKey(i, &k);
            TimeValue kt = k.time;
            float v = 0.0f;
            Interval iv = FOREVER;
            ctrl->GetValue(kt, &v, iv);
            times.push_back(kt);
            values.push_back(v);
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    }
}

// ─── Global Sequence detection & registration ───────────────────────

// Returns the Global Sequence duration (in TimeValue ticks) for a
// controller whose after-ORT is CYCLE or LOOP, or 0 otherwise.
//
// Handles both IKeyControl-backed controllers (Linear/Bezier/TCB) and
// non-IKeyControl controllers (on_off_float, boolean, procedural) by
// falling back to Animatable::GetKeyTime.
inline TimeValue getGlobalSequenceDuration(Control* ctrl) {
    if (!ctrl) return 0;

    // Accept both ORT_CYCLE (MaxScript `#cycle` → 2) and ORT_LOOP (3).
    // Max's controller internals treat both identically for cycling.
    int afterORT = ctrl->GetORT(ORT_AFTER);
    if (afterORT != ORT_CYCLE && afterORT != ORT_LOOP) return 0;

    // Primary path: IKeyControl (typed-key controllers).
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (ikc) {
        int n = ikc->GetNumKeys();
        if (n == 0) return 0;
        // All float key struct layouts start with TimeValue, so reading
        // as IBezFloatKey is safe just for the time field.
        IBezFloatKey key;
        ikc->GetKey(n - 1, &key);
        return key.time;
    }

    // Fallback: Animatable keying API (on_off_float, boolean, etc.).
    // These controllers don't expose IKeyControl but can still be set to
    // cyclic ORT — the visibility track helper relies on this path.
    int n = ctrl->NumKeys();
    if (n <= 0) return 0;
    return ctrl->GetKeyTime(n - 1);
}

// Register a duration with the IR model; identical durations are merged.
// Returns 0-based index into ir.globalSequenceDurations, or -1 if the
// duration is not positive.
inline int32_t registerGlobalSequence(ir::IRModel& model, TimeValue duration) {
    if (duration <= 0) return -1;
    uint32_t d = static_cast<uint32_t>(duration);
    for (size_t i = 0; i < model.globalSequenceDurations.size(); i++) {
        if (model.globalSequenceDurations[i] == d)
            return static_cast<int32_t>(i);
    }
    model.globalSequenceDurations.push_back(d);
    return static_cast<int32_t>(model.globalSequenceDurations.size() - 1);
}

// Detect + register in one step. Returns -1 if the controller isn't a
// Global Sequence; otherwise the 0-based index into
// ir.globalSequenceDurations (create-or-lookup).
inline int32_t detectAndRegisterGlobalSeq(Control* ctrl, ir::IRModel& model) {
    TimeValue dur = getGlobalSequenceDuration(ctrl);
    if (dur == 0) return -1;
    return registerGlobalSequence(model, dur);
}

// Scan several controllers and register the first one that's a GS.
// Useful when multiple source controllers feed a single IR track
// (e.g. U + V + W offset floats → one Vec3 translation track).
inline int32_t detectGlobalSeqAny(ir::IRModel& model,
                                  std::initializer_list<Control*> ctrls)
{
    for (Control* c : ctrls) {
        int32_t idx = detectAndRegisterGlobalSeq(c, model);
        if (idx >= 0) return idx;
    }
    return -1;
}

} // namespace core::anim
