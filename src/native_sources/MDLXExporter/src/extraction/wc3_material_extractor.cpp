// MDLXExporter — Wc3Material extractor implementation
#include "wc3_material_extractor.h"
#include "../mdx_class_ids.h"

#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <max.h>
#include <maxversion.h>
#include <stdmat.h>
#include <bitmap.h>
#include <inode.h>
#include <istdplug.h>
#include <control.h>
#include <iparamb2.h>
#include <tchar.h>
#include <windows.h>

#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <utility>
#include <fstream>
#include <cctype>

namespace mdx_extract {

namespace {

std::string wstrToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// Return just the filename (no directory) from the bitmap's disk path.
// The WC3 texture path prefix (e.g. "Textures\", "war3mapImported\") is
// stored separately on the Wc3Material and must be prepended by the caller
// via getMaterialPrefix().
std::string extractBitmapFileName(Texmap* tex) {
    auto stripPath = [](const std::string& p) -> std::string {
        if (p.empty()) return p;
        // Find last \ or /
        size_t lastSep = std::string::npos;
        for (size_t i = p.size(); i > 0; --i) {
            char c = p[i - 1];
            if (c == '\\' || c == '/') { lastSep = i - 1; break; }
        }
        return (lastSep != std::string::npos) ? p.substr(lastSep + 1) : p;
    };

    if (!tex) return {};
    // Standard Bitmaptexture
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        auto* bmt = static_cast<BitmapTex*>(tex);
        return stripPath(wstrToUtf8(bmt->GetMapName()));
    }
    // Wc3Bitmap — find BitmapTex delegate in references
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                auto* bmt = static_cast<BitmapTex*>(ref);
                return stripPath(wstrToUtf8(bmt->GetMapName()));
            }
        }
    }
    return {};
}

// Read a texture path prefix from the Wc3Material. The prefix is per-slot
// and was stored by the importer (e.g. diffusePrefix = "Textures\").
std::string readMaterialPrefix(ReferenceTarget* mtlRef, const wchar_t* paramName) {
    if (!mtlRef) return {};
    using PBR = core::ParamBlockReader;
    std::wstring prefix;
    if (PBR::readStringByName(mtlRef, paramName, 0, prefix) && !prefix.empty())
        return wstrToUtf8(prefix.c_str());
    return {};
}

// Combine a prefix and a filename into an MDX texture path.
// If the filename already contains a path separator, return it unchanged
// (safety net matching Wc3Material.ms buildTexturePath).
std::string buildTexturePath(const std::string& prefix, const std::string& fileName) {
    if (fileName.empty()) return {};
    if (fileName.find('\\') != std::string::npos ||
        fileName.find('/')  != std::string::npos)
    {
        return fileName;  // already full
    }
    return prefix + fileName;
}

// Kept for backward-compat — callers should migrate to extractBitmapFileName +
// buildTexturePath + readMaterialPrefix. This now returns JUST the filename
// (no absolute path), which is safer even for callers that don't know about
// the material-level prefix.
std::string extractBitmapPath(Texmap* tex) {
    return extractBitmapFileName(tex);
}

} // end anonymous namespace — findOrAddTexture is exported via header

int32_t findOrAddTexture(ir::IRModel& model, const std::string& path,
                         int32_t replaceableId, bool wrapU, bool wrapV)
{
    // Look for an existing match
    for (size_t i = 0; i < model.textures.size(); i++) {
        auto& t = model.textures[i];
        if (t.filePath == path && t.replaceableId == replaceableId &&
            t.wrapU == wrapU && t.wrapV == wrapV)
            return static_cast<int32_t>(i);
    }
    ir::Texture tex;
    tex.filePath = path;
    tex.replaceableId = replaceableId;
    tex.wrapU = wrapU;
    tex.wrapV = wrapV;
    model.textures.push_back(std::move(tex));
    return static_cast<int32_t>(model.textures.size() - 1);
}

namespace {  // reopen anon namespace for remaining internal helpers

// Extract properties from a Wc3Bitmap texture plugin, with Standard-BitmapTex
// fallback for wrapU/wrapV so that materials whose texmap is a plain BitmapTex
// (e.g. new Wc3Material-based imports) still get correct wrap flags instead of
// silently defaulting to false.
struct BitmapProperties {
    int replaceableId = 0;
    bool wrapU = false;
    bool wrapV = false;
    bool sphereEnvMap = false;
    std::string prefixPath;
};

// Read wrapU/wrapV from a Standard BitmapTex's UVGen. The flag bits are
// defined in imtl.h (Max SDK):
//   U_WRAP   = (1<<0) = 0x1
//   V_WRAP   = (1<<1) = 0x2
//   U_MIRROR = (1<<2) = 0x4
//   V_MIRROR = (1<<3) = 0x8
// We use the named macros if available so we pick up any SDK change.
#ifndef U_WRAP
  #define U_WRAP (1<<0)
#endif
#ifndef V_WRAP
  #define V_WRAP (1<<1)
#endif
static void readWrapFromBitmapTex(BitmapTex* bmt, bool& wrapU, bool& wrapV) {
    if (!bmt) return;
    UVGen* uv = bmt->GetTheUVGen();
    if (!uv) return;
    StdUVGen* stdUv = dynamic_cast<StdUVGen*>(uv);
    if (!stdUv) {
        // Not a StdUVGen — fall back to "wrap" (most common case)
        wrapU = true;
        wrapV = true;
        return;
    }
    int flags = stdUv->GetTextureTiling();
    wrapU = (flags & U_WRAP) != 0;
    wrapV = (flags & V_WRAP) != 0;
}

BitmapProperties extractBitmapProperties(Texmap* tex) {
    BitmapProperties props;
    if (!tex) return props;

    // Path A: Wc3Bitmap — read properties directly from its ParamBlock
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        auto* ref = dynamic_cast<ReferenceTarget*>(tex);
        if (!ref) return props;

        using PBR = core::ParamBlockReader;
        TimeValue t = 0;

        int replId = 1; // 1-based: 1 = Not Used
        PBR::readIntByName(ref, L"replaceableId", t, replId);
        props.replaceableId = std::max(0, replId - 1);

        BOOL flag = FALSE;
        if (PBR::readBoolByName(ref, L"wrapU", t, flag) && flag) props.wrapU = true;
        flag = FALSE;
        if (PBR::readBoolByName(ref, L"wrapV", t, flag) && flag) props.wrapV = true;
        flag = FALSE;
        if (PBR::readBoolByName(ref, L"sphereEnvMap", t, flag) && flag) props.sphereEnvMap = true;

        std::wstring prefix;
        if (PBR::readStringByName(ref, L"prefixPath", t, prefix) && !prefix.empty())
            props.prefixPath = wstrToUtf8(prefix.c_str());

        // If Wc3Bitmap wraps a Standard BitmapTex delegate and didn't store
        // explicit wrap flags, try the delegate as a secondary source.
        if (!props.wrapU && !props.wrapV) {
            for (int i = 0; i < tex->NumRefs(); i++) {
                ReferenceTarget* delegate = tex->GetReference(i);
                if (delegate && delegate->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                    readWrapFromBitmapTex(static_cast<BitmapTex*>(delegate),
                                          props.wrapU, props.wrapV);
                    break;
                }
            }
        }
        return props;
    }

    // Path B: Standard BitmapTex — no replaceableId/sphereEnvMap/prefixPath,
    // but we can still read wrap flags from its UVGen so imports that use
    // plain BitmapTex (new Wc3Material-based workflow) get correct TEXS flags.
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        readWrapFromBitmapTex(static_cast<BitmapTex*>(tex),
                              props.wrapU, props.wrapV);
        return props;
    }

    return props;
}

// ── UV animation extraction (TXAN) ──────────────────────────────────
// WC3 Texture Animations are animated UV Translation / Rotation / Scaling
// tracks. They live as Controllers on the Wc3Material's ParamBlock2 params
// `anim_UOffset`, `anim_VOffset`, `anim_WAngle`, `anim_UTiling`, `anim_VTiling`.
//
// The Importer writes controllers on BOTH the BitmapTex's StdUVGen (for
// viewport playback) and on these Wc3Material params (for export roundtrip).
// Both are shared — the Wc3Material plugin's handlers route access to the
// delegate's coords. We read from the ParamBlock side.

// Find a named parameter in any IParamBlock2 on the target.
static bool findAnimParam(ReferenceTarget* ref, const wchar_t* name,
                          IParamBlock2*& outPB, ParamID& outPID)
{
    if (!ref) return false;
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

// Get the controller assigned to a named animatable parameter.
// IParamBlock2 exposes animated parameters as sub-anims; GetAnimNum maps
// ParamID → sub-anim index, SubAnim returns the Animatable (a Control*).
static Control* getParamController(ReferenceTarget* ref, const wchar_t* name) {
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    if (!findAnimParam(ref, name, pb, pid)) return nullptr;
    int animIdx = pb->GetAnimNum(pid, 0);
    if (animIdx < 0 || animIdx >= pb->NumSubs()) return nullptr;
    Animatable* anim = pb->SubAnim(animIdx);
    if (!anim) return nullptr;
    return GetControlInterface(anim);
}

// Evaluate a float controller at a specific time.
static float evalFloat(Control* ctrl, TimeValue t, float defaultVal = 0.0f) {
    if (!ctrl) return defaultVal;
    float val = defaultVal;
    Interval valid = FOREVER;
    ctrl->GetValue(t, &val, valid);
    return val;
}

// Collect all key times from any keyframe controller.
static std::vector<TimeValue> collectKeyTimes(Control* ctrl) {
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

// Map a controller's ClassID to an interpolation type.
static ir::InterpolationType detectInterpFromController(Control* ctrl) {
    if (!ctrl) return ir::InterpolationType::None;
    ULONG cidA = ctrl->ClassID().PartA();
    if (cidA == LININTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Linear;
    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) return ir::InterpolationType::Bezier;
    if (cidA == TCBINTERP_FLOAT_CLASS_ID)    return ir::InterpolationType::Hermite;
    return ir::InterpolationType::None;
}

// Read a float controller's keys into parallel vectors: time, value, in-tangent,
// out-tangent. hasTangents is set to true iff the controller is Hermite or
// Bezier (only those have meaningful tangents). For Linear/TCB/other, tangents
// are zero-filled.
static void readFloatKeys(Control* ctrl,
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
        // Bezier: inTan/outTan are the tangent slopes
        hasTangents = true;
        for (int i = 0; i < n; i++) {
            IBezFloatKey k; ikc->GetKey(i, &k);
            times.push_back(k.time);
            values.push_back(k.val);
            inTans.push_back(k.intan);
            outTans.push_back(k.outtan);
        }
    } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
        // TCB: store as Hermite-style tangents (tens/cont/bias converted)
        hasTangents = true;
        for (int i = 0; i < n; i++) {
            ITCBFloatKey k; ikc->GetKey(i, &k);
            times.push_back(k.time);
            values.push_back(k.val);
            // Use zero tangents — TCB→Bezier conversion is non-trivial;
            // MDX readers that receive Hermite with zero tangents will
            // interpolate smoothly which is visually close enough.
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    } else if (cidA == LININTERP_FLOAT_CLASS_ID) {
        // Linear: no tangents used
        for (int i = 0; i < n; i++) {
            ILinFloatKey k; ikc->GetKey(i, &k);
            times.push_back(k.time);
            values.push_back(k.val);
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    } else {
        // Unknown/other — fall back to Bezier key read + GetValue for value
        for (int i = 0; i < n; i++) {
            IBezFloatKey k; ikc->GetKey(i, &k);
            TimeValue t = k.time;
            float v = 0.0f;
            Interval iv = FOREVER;
            ctrl->GetValue(t, &v, iv);
            times.push_back(t);
            values.push_back(v);
            inTans.push_back(0.0f);
            outTans.push_back(0.0f);
        }
    }
}

// Extract a Vec3 track from two float params (U, V). The third component
// gets `defaultW`. negateFactor is applied to the U component (use -1.0f
// to match importer convention where MDX X = -U_Offset).
static int32_t extractVec3FromTwoFloats(ReferenceTarget* mtlRef,
                                        const wchar_t* uParamName,
                                        const wchar_t* vParamName,
                                        float defaultU, float defaultV,
                                        float defaultW,
                                        ir::InterpolationType interp,
                                        ir::IRModel& model,
                                        float negateFactor = 1.0f)
{
    Control* ctrlU = getParamController(mtlRef, uParamName);
    Control* ctrlV = getParamController(mtlRef, vParamName);
    if (!ctrlU && !ctrlV) return -1;

    // Read per-controller keys with tangents
    std::vector<TimeValue> kTimesU, kTimesV;
    std::vector<float> kValsU, kValsV, kTiU, kToU, kTiV, kToV;
    bool tanU = false, tanV = false;
    readFloatKeys(ctrlU, kTimesU, kValsU, kTiU, kToU, tanU);
    readFloatKeys(ctrlV, kTimesV, kValsV, kTiV, kToV, tanV);

    if (kTimesU.empty() && kTimesV.empty()) return -1;

    // Union of key times
    std::vector<TimeValue> times;
    times.reserve(kTimesU.size() + kTimesV.size());
    for (auto t : kTimesU) times.push_back(t);
    for (auto t : kTimesV) times.push_back(t);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    // Tangent lookup by time
    std::unordered_map<TimeValue, size_t> tanIdxU, tanIdxV;
    for (size_t i = 0; i < kTimesU.size(); i++) tanIdxU[kTimesU[i]] = i;
    for (size_t i = 0; i < kTimesV.size(); i++) tanIdxV[kTimesV[i]] = i;

    bool useTans = (tanU || tanV) &&
                   (interp == ir::InterpolationType::Hermite ||
                    interp == ir::InterpolationType::Bezier);

    ir::Track<Point3> track;
    track.interpolation = interp;

    for (TimeValue t : times) {
        float u = evalFloat(ctrlU, t, defaultU);
        float v = evalFloat(ctrlV, t, defaultV);

        track.keys.push_back({});
        auto& key = track.keys.back();
        key.time  = t;
        key.value = Point3(u * negateFactor, v, defaultW);

        if (useTans) {
            float uiIn = 0.0f, uiOut = 0.0f, viIn = 0.0f, viOut = 0.0f;
            auto itU = tanIdxU.find(t);
            if (itU != tanIdxU.end() && tanU) {
                uiIn  = kTiU[itU->second] * negateFactor;
                uiOut = kToU[itU->second] * negateFactor;
            }
            auto itV = tanIdxV.find(t);
            if (itV != tanIdxV.end() && tanV) {
                viIn  = kTiV[itV->second];
                viOut = kToV[itV->second];
            }
            key.inTangent  = Point3(uiIn,  viIn,  0.0f);
            key.outTangent = Point3(uiOut, viOut, 0.0f);
            key.hasTangents = true;
        }
    }

    int32_t idx = static_cast<int32_t>(model.vec3Tracks.size());
    model.vec3Tracks.push_back(std::move(track));
    return idx;
}

// Extract UV rotation from "anim_WAngle" (degrees → quaternion around Z).
// Always uses Linear interpolation; Bezier tangent→quat conversion is
// non-trivial and rarely needed for UV rotation.
static int32_t extractQuatFromAngle(ReferenceTarget* mtlRef, ir::IRModel& model)
{
    Control* ctrl = getParamController(mtlRef, L"anim_WAngle");
    if (!ctrl) return -1;
    auto keyTimes = collectKeyTimes(ctrl);
    if (keyTimes.empty()) return -1;

    ir::Track<Quat> track;
    track.interpolation = ir::InterpolationType::Linear;

    for (TimeValue t : keyTimes) {
        float angleDeg = evalFloat(ctrl, t, 0.0f);
        float angleRad = angleDeg * (3.14159265358979323846f / 180.0f);

        ir::Keyframe<Quat> key;
        key.time  = t;
        key.value = Quat(0.0f, 0.0f, sinf(angleRad * 0.5f), cosf(angleRad * 0.5f));
        track.keys.push_back(key);
    }

    int32_t idx = static_cast<int32_t>(model.quatTracks.size());
    model.quatTracks.push_back(std::move(track));
    return idx;
}

// Extract the opacity track (KMTA) from the "opacity" param. Values are
// stored 0..100 in the Wc3Material; MDX expects 0..1.
static int32_t extractOpacityTrack(ReferenceTarget* mtlRef,
                                   ir::InterpolationType interp,
                                   ir::IRModel& model)
{
    Control* ctrl = getParamController(mtlRef, L"opacity");
    if (!ctrl) return -1;

    std::vector<TimeValue> times;
    std::vector<float> values, inTans, outTans;
    bool hasTangents = false;
    readFloatKeys(ctrl, times, values, inTans, outTans, hasTangents);
    if (times.empty()) return -1;

    bool useTans = hasTangents &&
                   (interp == ir::InterpolationType::Hermite ||
                    interp == ir::InterpolationType::Bezier);

    ir::Track<float> track;
    track.interpolation = interp;

    for (size_t i = 0; i < times.size(); i++) {
        ir::Keyframe<float> key;
        key.time       = times[i];
        key.value      = values[i] / 100.0f;
        if (useTans) {
            key.inTangent   = inTans[i]  / 100.0f;
            key.outTangent  = outTans[i] / 100.0f;
            key.hasTangents = true;
        }
        track.keys.push_back(key);
    }

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

// ── Global Sequence detection ─────────────────────────────────────
// A controller is a Global Sequence when its after-ORT is set to a
// cyclic behaviour: ORT_CYCLE (2) or ORT_LOOP (3). These are treated
// identically in Max SDK's own controller code (see how Control::GetValue
// handles them — both dispatch to CycleTime). MaxScript's `#cycle`
// maps to ORT_CYCLE (2), but some scenes end up with ORT_LOOP, so we
// accept both.
//
// The duration is the time of the last key.
//
// The IRModel maintains `globalSequenceDurations` — each unique
// duration gets an index. Tracks that share a duration share an index.
// The model builder writes this as the GLBS chunk.

// Check if a controller is a Global Sequence. Returns the duration
// (time of last key) in TimeValue ticks, or 0 if not a Global Sequence.
static TimeValue getGlobalSequenceDuration(Control* ctrl) {
    if (!ctrl) return 0;

    // Max SDK: GetORT(ORT_AFTER) returns the after-range behaviour.
    // MaxScript's `#cycle` maps to SDK's ORT_CYCLE (2). ORT_LOOP (3)
    // is treated identically for cycle semantics — accept both.
    int afterORT = ctrl->GetORT(ORT_AFTER);
    if (afterORT != ORT_CYCLE && afterORT != ORT_LOOP) return 0;

    // Duration = time of last key
    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) return 0;
    int n = ikc->GetNumKeys();
    if (n == 0) return 0;

    // All float key struct layouts start with TimeValue, so reading
    // as IBezFloatKey is safe just for the time field.
    IBezFloatKey key;
    ikc->GetKey(n - 1, &key);
    return key.time;
}

// Register a duration with the IRModel, returning its 0-based index.
// Identical durations are merged — they share the same index.
static int32_t registerGlobalSequence(ir::IRModel& model, TimeValue duration) {
    if (duration <= 0) return -1;
    uint32_t d = static_cast<uint32_t>(duration);

    for (size_t i = 0; i < model.globalSequenceDurations.size(); i++) {
        if (model.globalSequenceDurations[i] == d)
            return static_cast<int32_t>(i);
    }

    model.globalSequenceDurations.push_back(d);
    return static_cast<int32_t>(model.globalSequenceDurations.size() - 1);
}

// Convenience: detect + register for a single controller. Returns -1
// if the controller isn't a Global Sequence.
static int32_t detectAndRegisterGlobalSeq(Control* ctrl, ir::IRModel& model) {
    TimeValue dur = getGlobalSequenceDuration(ctrl);
    if (dur == 0) return -1;
    return registerGlobalSequence(model, dur);
}

// Scan several controllers and register the first Global Sequence found.
// Used when multiple source controllers (U+V offset, etc.) feed one track.
static int32_t detectGlobalSeqAny(ir::IRModel& model,
                                  std::initializer_list<Control*> ctrls)
{
    for (Control* c : ctrls) {
        int32_t idx = detectAndRegisterGlobalSeq(c, model);
        if (idx >= 0) return idx;
    }
    return -1;
}

// ── Three-float Vec3 extractor (U, V, W) ──────────────────────────
// Used for UV translation where MDX's 3rd component is a real
// animatable W_Offset stored on the Wc3Material as `anim_WOffset`.
// (Max's StdUVGen has no W_Offset — this param is MDX round-trip only.)
// signA is applied to the first (U) component — MDX X = -U_Offset.
static int32_t extractVec3FromThreeFloats(ReferenceTarget* mtlRef,
                                          const wchar_t* paramA,
                                          const wchar_t* paramB,
                                          const wchar_t* paramC,
                                          float defaultA, float defaultB, float defaultC,
                                          ir::InterpolationType interp,
                                          ir::IRModel& model,
                                          float signA = 1.0f)
{
    Control* ctrlA = getParamController(mtlRef, paramA);
    Control* ctrlB = getParamController(mtlRef, paramB);
    Control* ctrlC = getParamController(mtlRef, paramC);
    if (!ctrlA && !ctrlB && !ctrlC) return -1;

    // Read per-controller keys with tangents
    std::vector<TimeValue> kTimesA, kTimesB, kTimesC;
    std::vector<float> kValsA, kValsB, kValsC;
    std::vector<float> kTiA, kToA, kTiB, kToB, kTiC, kToC;
    bool tanA = false, tanB = false, tanC = false;
    readFloatKeys(ctrlA, kTimesA, kValsA, kTiA, kToA, tanA);
    readFloatKeys(ctrlB, kTimesB, kValsB, kTiB, kToB, tanB);
    readFloatKeys(ctrlC, kTimesC, kValsC, kTiC, kToC, tanC);

    if (kTimesA.empty() && kTimesB.empty() && kTimesC.empty()) return -1;

    std::vector<TimeValue> times;
    times.reserve(kTimesA.size() + kTimesB.size() + kTimesC.size());
    for (auto t : kTimesA) times.push_back(t);
    for (auto t : kTimesB) times.push_back(t);
    for (auto t : kTimesC) times.push_back(t);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());

    // Tangent lookup by time
    std::unordered_map<TimeValue, size_t> tanIdxA, tanIdxB, tanIdxC;
    for (size_t i = 0; i < kTimesA.size(); i++) tanIdxA[kTimesA[i]] = i;
    for (size_t i = 0; i < kTimesB.size(); i++) tanIdxB[kTimesB[i]] = i;
    for (size_t i = 0; i < kTimesC.size(); i++) tanIdxC[kTimesC[i]] = i;

    bool useTans = (tanA || tanB || tanC) &&
                   (interp == ir::InterpolationType::Hermite ||
                    interp == ir::InterpolationType::Bezier);

    ir::Track<Point3> track;
    track.interpolation = interp;

    for (TimeValue t : times) {
        float a = evalFloat(ctrlA, t, defaultA) * signA;
        float b = evalFloat(ctrlB, t, defaultB);
        float c = evalFloat(ctrlC, t, defaultC);

        track.keys.push_back({});
        auto& key = track.keys.back();
        key.time  = t;
        key.value = Point3(a, b, c);

        if (useTans) {
            float aiIn = 0.0f, aiOut = 0.0f;
            float biIn = 0.0f, biOut = 0.0f;
            float ciIn = 0.0f, ciOut = 0.0f;
            auto itA = tanIdxA.find(t);
            if (itA != tanIdxA.end() && tanA) {
                aiIn  = kTiA[itA->second] * signA;
                aiOut = kToA[itA->second] * signA;
            }
            auto itB = tanIdxB.find(t);
            if (itB != tanIdxB.end() && tanB) {
                biIn  = kTiB[itB->second];
                biOut = kToB[itB->second];
            }
            auto itC = tanIdxC.find(t);
            if (itC != tanIdxC.end() && tanC) {
                ciIn  = kTiC[itC->second];
                ciOut = kToC[itC->second];
            }
            key.inTangent  = Point3(aiIn,  biIn,  ciIn);
            key.outTangent = Point3(aiOut, biOut, ciOut);
            key.hasTangents = true;
        }
    }

    int32_t idx = static_cast<int32_t>(model.vec3Tracks.size());
    model.vec3Tracks.push_back(std::move(track));
    return idx;
}

// Per-export dedup cache, keyed by diffuseMap pointer.
//
// When multiple material layers share the same Bitmaptexture (composite
// materials), they share the same UV-animation semantics because the
// controllers live on the bitmap's coords, not per-layer. Dedup by the
// bitmap pointer rather than controller pointers — Material-level
// controllers can diverge (Layer 1+2 may share one controller, Layer 3
// may have its own) but the underlying bitmap is the source of truth.
//
// Reset at the start of each top-level extractMaterials() call.
static std::unordered_map<Texmap*, int32_t> g_texAnimCache;

// Extract TextureAnimation for a Wc3Material. Returns the TextureAnimation
// index, or -1 if no UV animation controllers are present. Uses g_texAnimCache
// (keyed by the material's diffuseMap pointer) to deduplicate across
// composite-material layers that share the same bitmap.
static int32_t extractTextureAnimationFromMaterial(ReferenceTarget* mtlRef,
                                                   ir::IRModel& model)
{
    if (!mtlRef) return -1;
    using PBR = core::ParamBlockReader;

    Control* ctrlU    = getParamController(mtlRef, L"anim_UOffset");
    Control* ctrlV    = getParamController(mtlRef, L"anim_VOffset");
    Control* ctrlWOff = getParamController(mtlRef, L"anim_WOffset");  // Z-component
    Control* ctrlW    = getParamController(mtlRef, L"anim_WAngle");   // rotation
    Control* ctrlUT   = getParamController(mtlRef, L"anim_UTiling");
    Control* ctrlVT   = getParamController(mtlRef, L"anim_VTiling");

    if (!ctrlU && !ctrlV && !ctrlWOff && !ctrlW && !ctrlUT && !ctrlVT) return -1;

    // Cache lookup by diffuseMap pointer
    Texmap* diffuseTexForAnim = nullptr;
    PBR::readTexmapByName(mtlRef, L"diffuseMap", diffuseTexForAnim);
    if (diffuseTexForAnim) {
        auto cached = g_texAnimCache.find(diffuseTexForAnim);
        if (cached != g_texAnimCache.end())
            return cached->second;
    }

    ir::TextureAnimation ta;

    // Translation: anim_UOffset + anim_VOffset + anim_WOffset → Vec3(U, V, W), U negated
    if (ctrlU || ctrlV || ctrlWOff) {
        ir::InterpolationType interp =
            ctrlU    ? detectInterpFromController(ctrlU)
          : ctrlV    ? detectInterpFromController(ctrlV)
                     : detectInterpFromController(ctrlWOff);
        if (interp == ir::InterpolationType::None)
            interp = ir::InterpolationType::Linear;

        ta.translationTrackIndex = extractVec3FromThreeFloats(
            mtlRef,
            L"anim_UOffset", L"anim_VOffset", L"anim_WOffset",
            0.0f, 0.0f, 0.0f,
            interp, model,
            -1.0f);  // negate U: MDX X = -U_Offset (matches importer)

        if (ta.translationTrackIndex >= 0) {
            int32_t gsIdx = detectGlobalSeqAny(model, {ctrlU, ctrlV, ctrlWOff});
            if (gsIdx >= 0)
                model.vec3Tracks[ta.translationTrackIndex].globalSequenceIndex = gsIdx;
        }
    }

    // Rotation: anim_WAngle → Quat (Linear)
    if (ctrlW) {
        ta.rotationTrackIndex = extractQuatFromAngle(mtlRef, model);
        if (ta.rotationTrackIndex >= 0) {
            int32_t gsIdx = detectAndRegisterGlobalSeq(ctrlW, model);
            if (gsIdx >= 0)
                model.quatTracks[ta.rotationTrackIndex].globalSequenceIndex = gsIdx;
        }
    }

    // Scale: anim_UTiling + anim_VTiling → Vec3(U, V, 1)
    if (ctrlUT || ctrlVT) {
        ir::InterpolationType interp =
            ctrlUT ? detectInterpFromController(ctrlUT)
                   : detectInterpFromController(ctrlVT);
        if (interp == ir::InterpolationType::None)
            interp = ir::InterpolationType::Linear;

        ta.scaleTrackIndex = extractVec3FromTwoFloats(
            mtlRef, L"anim_UTiling", L"anim_VTiling",
            1.0f, 1.0f, 1.0f,
            interp, model);

        if (ta.scaleTrackIndex >= 0) {
            int32_t gsIdx = detectGlobalSeqAny(model, {ctrlUT, ctrlVT});
            if (gsIdx >= 0)
                model.vec3Tracks[ta.scaleTrackIndex].globalSequenceIndex = gsIdx;
        }
    }

    if (ta.translationTrackIndex < 0 &&
        ta.rotationTrackIndex < 0 &&
        ta.scaleTrackIndex < 0)
    {
        return -1;
    }

    int32_t taIdx = static_cast<int32_t>(model.textureAnimations.size());
    model.textureAnimations.push_back(std::move(ta));
    if (diffuseTexForAnim)
        g_texAnimCache[diffuseTexForAnim] = taIdx;
    return taIdx;
}

// Material-level properties extracted from a single Wc3Material sub-material.
// These get merged when building a composite material.
struct MaterialLevelProps {
    int priorityPlane = 0;
    uint32_t flags = 0;
    std::string shaderName;
};

// ─── IFL (Image File List) animation extraction ──────────────────────
//
// The importer stores animated textures as a single BitmapTex with a
// .ifl filename. The .ifl is a text file containing one texture path
// per line. When the exporter sees such a BitmapTex, it must reverse
// the importer's encoding to recover a KMTF track with per-texture keys.
//
// Importer encoding (see exporter_handoff_ifl_sequence.md §1):
//   bmpTex->startTime     = first key's Max tick (usually 0)
//   bmpTex->playbackRate  = TicksPerFrame / avgInterval_ticks
//   bmpTex->endCondition  = 0 (LOOP, when MDX used a GlobalSequence)
//                         | 2 (HOLD, when MDX used a named sequence)
//
// Reverse for export:
//   avgInterval_ticks = TicksPerFrame / playbackRate
//   key[i].time (ticks) = startTime + i * avgInterval_ticks
//   The builder then runs ticksToMs on the track, producing the
//   final MDX key times in ms.

// Return true if `fname` ends in ".ifl" (case-insensitive).
bool isIflFilename(const std::string& fname) {
    if (fname.size() < 4) return false;
    const char* s = fname.c_str() + fname.size() - 4;
    return (std::tolower(static_cast<unsigned char>(s[0])) == '.' &&
            std::tolower(static_cast<unsigned char>(s[1])) == 'i' &&
            std::tolower(static_cast<unsigned char>(s[2])) == 'f' &&
            std::tolower(static_cast<unsigned char>(s[3])) == 'l');
}

// Read the IFL file's lines as texture paths. Returns empty vector if
// the file can't be opened or is empty. Trims CR/LF/whitespace from
// each line and skips blank lines.
std::vector<std::string> readIflLines(const std::string& iflPath) {
    std::vector<std::string> out;
    std::ifstream ifs(iflPath);
    if (!ifs.is_open()) return out;

    std::string line;
    while (std::getline(ifs, line)) {
        while (!line.empty() && (line.back() == '\r' ||
                                  line.back() == '\n' ||
                                  line.back() == ' '  ||
                                  line.back() == '\t'))
            line.pop_back();
        // Trim leading whitespace too (uncommon but safe)
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos && start > 0)
            line.erase(0, start);
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

// Extract the filename component (no directory) from a path string.
// Mirrors extractBitmapFileName's stripPath lambda but for std::string.
std::string filenameOnly(const std::string& path) {
    if (path.empty()) return path;
    size_t lastSep = std::string::npos;
    for (size_t i = path.size(); i > 0; --i) {
        char c = path[i - 1];
        if (c == '\\' || c == '/') { lastSep = i - 1; break; }
    }
    return (lastSep != std::string::npos) ? path.substr(lastSep + 1) : path;
}

// Extract an IFL animation track from a BitmapTex whose filename points
// to a .ifl file. Registers each IFL line as a TEXS entry (if not already
// present), builds an IntTrack of texture-indices, optionally binds it to
// a GlobalSequence, and stores it in `model.intTracks`.
//
// Returns the track index to put on `layer.textureIdTrackIndex`, or -1 if
// extraction fails (IFL file missing / < 2 entries / invalid playbackRate).
// On success, `outFirstTexIndex` receives the first IFL texture's index so
// the caller can still set a static `layer.textureRefs[0]` to something
// sensible (most tools ignore it when a KMTF is present, but some validate it).
int32_t extractIflAnimation(BitmapTex* bmpTex, ir::IRModel& model,
                             int32_t defaultReplaceableId,
                             bool defaultWrapU, bool defaultWrapV,
                             int32_t& outFirstTexIndex)
{
    outFirstTexIndex = -1;
    if (!bmpTex) return -1;

    // Read the .ifl path from the BitmapTex
    std::string iflPath;
    {
        const MCHAR* mp = bmpTex->GetMapName();
        if (!mp) return -1;
        iflPath = wstrToUtf8(mp);
    }
    if (!isIflFilename(iflPath)) return -1;

    // Read the .ifl file's contents — one texture path per line
    std::vector<std::string> lines = readIflLines(iflPath);
    if (lines.size() < 2) {
        // Not an animated IFL — fall back to static texture using the
        // first line if any, otherwise caller's static path.
        if (lines.size() == 1) {
            outFirstTexIndex = findOrAddTexture(model,
                filenameOnly(lines[0]),
                defaultReplaceableId, defaultWrapU, defaultWrapV);
        }
        return -1;
    }

    // Read the importer-stored parameters
    TimeValue startTime = bmpTex->GetStartTime();
    float playbackRate  = bmpTex->GetPlaybackRate();
    int   endCondition  = bmpTex->GetEndCondition();

    // Guard against invalid playback rate
    if (playbackRate <= 0.0001f) {
        // Invalid / unset — can't reconstruct timing. Fall back to 1 key.
        outFirstTexIndex = findOrAddTexture(model,
            filenameOnly(lines[0]),
            defaultReplaceableId, defaultWrapU, defaultWrapV);
        return -1;
    }

    // Reverse the importer formula:
    //   playbackRate = TicksPerFrame / avgInterval_ticks
    //   avgInterval_ticks = TicksPerFrame / playbackRate
    TimeValue tpf = GetTicksPerFrame();
    TimeValue avgIntervalTicks = static_cast<TimeValue>(
        static_cast<float>(tpf) / playbackRate + 0.5f);
    if (avgIntervalTicks <= 0) avgIntervalTicks = tpf;  // safety

    // Map each IFL line to a TEXS index (create if missing)
    std::vector<uint32_t> texIndices;
    texIndices.reserve(lines.size());
    for (const std::string& raw : lines) {
        std::string fn = filenameOnly(raw);
        int32_t idx = findOrAddTexture(model, fn,
            defaultReplaceableId, defaultWrapU, defaultWrapV);
        texIndices.push_back(static_cast<uint32_t>(idx));
    }
    outFirstTexIndex = static_cast<int32_t>(texIndices[0]);

    // Build the IntTrack — one key per texture, evenly spaced
    ir::IntTrack track;
    track.interpolation = ir::InterpolationType::Linear;

    // endCondition 0 (LOOP) → GlobalSequence binding
    // endCondition 2 (HOLD) → no global sequence (plays once within
    //                          a named sequence's duration)
    // endCondition 1 (PINGPONG) is not representable in MDX; we
    //                          treat it as LOOP.
    if (endCondition == 0 || endCondition == 1) {
        TimeValue totalDuration = avgIntervalTicks *
            static_cast<TimeValue>(texIndices.size());
        int32_t gsIdx = core::anim::registerGlobalSequence(model, totalDuration);
        if (gsIdx >= 0) track.globalSequenceIndex = gsIdx;
    }

    for (size_t i = 0; i < texIndices.size(); ++i) {
        ir::Keyframe<int32_t> key;
        key.time = startTime +
            static_cast<TimeValue>(i) * avgIntervalTicks;
        key.value = static_cast<int32_t>(texIndices[i]);
        track.keys.push_back(key);
    }

    int32_t trackIdx = static_cast<int32_t>(model.intTracks.size());
    model.intTracks.push_back(std::move(track));
    return trackIdx;
}

ir::MaterialLayer extractWc3Layer(ReferenceTarget* mtlRef, ir::IRModel& model,
                                  MaterialLevelProps& matProps)
{
    using PBR = core::ParamBlockReader;
    TimeValue t = 0;

    ir::MaterialLayer layer;

    // Filter mode (1-based: 1=None..7=Modulate2x)
    int filterMode = 1;
    PBR::readIntByName(mtlRef, L"filterMode", t, filterMode);
    switch (filterMode) {
    case 1: layer.blendMode = ir::BlendMode::None; break;
    case 2: layer.blendMode = ir::BlendMode::Transparent; break;
    case 3: layer.blendMode = ir::BlendMode::Blend; break;
    case 4: layer.blendMode = ir::BlendMode::Additive; break;
    case 5: layer.blendMode = ir::BlendMode::AddAlpha; break;
    case 6: layer.blendMode = ir::BlendMode::Modulate; break;
    case 7: layer.blendMode = ir::BlendMode::Modulate2x; break;
    default: layer.blendMode = ir::BlendMode::None; break;
    }

    // Opacity (0–100 → 0.0–1.0)
    float opacity = 100.0f;
    PBR::readFloatByName(mtlRef, L"opacity", t, opacity);
    layer.alpha = opacity / 100.0f;

    // Flags
    BOOL flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"twoSided", t, flagVal) && flagVal)
        layer.twoSided = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"unshaded", t, flagVal) && flagVal)
        layer.unshaded = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"unfogged", t, flagVal) && flagVal)
        layer.unfogged = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"noDepthTest", t, flagVal) && flagVal)
        layer.noDepthTest = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"noDepthSet", t, flagVal) && flagVal)
        layer.noDepthWrite = true;

    // Priority plane
    int priority = 0;
    PBR::readIntByName(mtlRef, L"priorityPlane", t, priority);
    matProps.priorityPlane = priority;

    // Coord ID (-1 = default → 0)
    int coordId = -1;
    PBR::readIntByName(mtlRef, L"coordId", t, coordId);
    layer.uvSetIndex = (coordId >= 0) ? coordId : 0;

    // Material flags — ConstantColor
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"constantColor", t, flagVal) && flagVal)
        matProps.flags |= 0x01;

    // Material flags — FullResolution
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"fullResolution", t, flagVal) && flagVal)
        matProps.flags |= 0x20;

    // Material flags — SortOrder (1=unused, 2=nearToFar, 3=farToNear)
    int sortOrder = 1;
    PBR::readIntByName(mtlRef, L"sortOrder", t, sortOrder);
    if (sortOrder == 2) matProps.flags |= 0x08;
    else if (sortOrder == 3) matProps.flags |= 0x10;

    // Shader path (Reforged)
    std::wstring shaderPath;
    if (PBR::readStringByName(mtlRef, L"shaderPath", t, shaderPath) && !shaderPath.empty())
        matProps.shaderName = wstrToUtf8(shaderPath.c_str());

    // Material-level replaceableId (new NeoDex scheme — field moved from
    // Wc3Bitmap to Wc3Material). Dropdown stores 1-based indices:
    //   1 = Not Used, 2 = Team Color, 3 = Team Glow, 4 = Cliff,
    //   5 = Lord Cliffington.
    // MDX TEXS expects 0-based: 0 = Normal, 1 = Team Color, 2 = Team Glow,
    // 3 = Cliff, 4 = Lord Cliffington. So subtract 1.
    //
    // We fall back to reading from the bitmap (extractBitmapProperties) if
    // the material doesn't explicitly set replaceableId — that preserves
    // compatibility with older Wc3Bitmap-based materials that still carry
    // the replaceableId there. Zero means "not set / Not Used".
    int matReplaceableId = 0;  // 0 = Normal / Not Set
    bool matReplaceableIdFound = false;
    {
        int dropdownVal = 0;
        if (PBR::readIntByName(mtlRef, L"replaceableId", t, dropdownVal)) {
            matReplaceableIdFound = true;
            // Dropdown is 1-based; convert to MDX 0-based. Clamp negatives.
            matReplaceableId = std::max(0, dropdownVal - 1);
        }

        // Debug log — write to %TEMP%\mdlx_replaceable_debug.log
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
            std::ofstream log(logPath, std::ios::app);
            if (log.is_open()) {
                // Grab the material name via Mtl* interface (MaterialLevelProps
                // doesn't store a name field).
                std::string matName = "<unknown>";
                if (auto* mtl = dynamic_cast<Mtl*>(mtlRef)) {
                    const MCHAR* n = mtl->GetName();
                    if (n) matName = wstrToUtf8(n);
                }
                log << "[MAT] name='" << matName << "'"
                    << " hasReplProp=" << (matReplaceableIdFound ? "yes" : "NO")
                    << " dropdownVal=" << dropdownVal
                    << " -> matReplaceableId=" << matReplaceableId
                    << "\n";
            }
        }
    }

    // --- Diffuse texture ---
    Texmap* texmap = nullptr;
    bool hasTexmap = PBR::readTexmapByName(mtlRef, L"diffuseMap", texmap) && texmap;
    if (hasTexmap) {
        std::string fileName = extractBitmapFileName(texmap);
        BitmapProperties bmpProps = extractBitmapProperties(texmap);

        // Material-level replaceableId wins over bitmap-level (new scheme).
        // Only fall back to bitmap value if material didn't override.
        if (matReplaceableId > 0) {
            bmpProps.replaceableId = matReplaceableId;
        }

        // For Team Color / Team Glow / Cliff the MDX convention is an empty
        // texture path — the game substitutes the texture at runtime based
        // on the replaceableId. Force the path empty if this is a special
        // replaceable type, regardless of what diffuseMap points to.
        if (matReplaceableId >= 1 && matReplaceableId <= 4) {
            fileName.clear();
        }

        // Path prefix: per-slot prefix lives on the Wc3Material
        // (new scheme), but fall back to the bitmap's own prefixPath
        // for legacy materials that still carry it on the bitmap.
        std::string matPrefix = readMaterialPrefix(mtlRef, L"diffusePrefix");
        std::string prefix = !matPrefix.empty() ? matPrefix : bmpProps.prefixPath;
        std::string texPath = (matReplaceableId >= 1 && matReplaceableId <= 4)
                                  ? std::string()  // empty path for replaceables
                                  : buildTexturePath(prefix, fileName);

        // Extended debug log for diffuse texture processing
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
            std::ofstream log(logPath, std::ios::app);
            if (log.is_open()) {
                std::string texClass = "unknown";
                if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) texClass = "BitmapTex";
                else if (texmap->ClassID() == mdx_ids::WC3_BITMAP) texClass = "Wc3Bitmap";
                log << "  [DIFF] texClass=" << texClass
                    << " fileName='" << fileName << "'"
                    << " bmpReplId=" << bmpProps.replaceableId
                    << " matPrefix='" << matPrefix << "'"
                    << " bmpPrefix='" << bmpProps.prefixPath << "'"
                    << " -> texPath='" << texPath << "'"
                    << " finalReplId=" << bmpProps.replaceableId
                    << "\n";
            }
        }

        // SphereEnvMap lives on the bitmap
        if (bmpProps.sphereEnvMap) layer.sphereEnvMap = true;

        // ── IFL detection ─────────────────────────────────────────
        // If the BitmapTex points to a .ifl file, the importer stored
        // an animated texture sequence. Reverse-engineer it into a
        // KMTF track now. The function returns -1 for non-IFL or
        // single-frame IFLs, in which case we fall through to the
        // static-texture path below.
        int32_t iflTrackIdx = -1;
        int32_t iflFirstTex = -1;
        BitmapTex* bmpTexForIfl = nullptr;
        if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
            bmpTexForIfl = static_cast<BitmapTex*>(texmap);
        } else if (texmap->ClassID() == mdx_ids::WC3_BITMAP) {
            // Wc3Bitmap wraps a BitmapTex delegate
            for (int i = 0; i < texmap->NumRefs(); i++) {
                ReferenceTarget* ref = texmap->GetReference(i);
                if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                    bmpTexForIfl = static_cast<BitmapTex*>(ref);
                    break;
                }
            }
        }
        if (bmpTexForIfl) {
            iflTrackIdx = extractIflAnimation(bmpTexForIfl, model,
                bmpProps.replaceableId, bmpProps.wrapU, bmpProps.wrapV,
                iflFirstTex);
        }

        if (iflTrackIdx >= 0) {
            // IFL animation — KMTF track created. Set the static
            // textureRef to the first IFL texture (most tools ignore
            // this when KMTF is present, but some validate it exists).
            layer.textureIdTrackIndex = iflTrackIdx;
            ir::TextureRef texRef;
            texRef.textureIndex = (iflFirstTex >= 0)
                ? iflFirstTex
                : findOrAddTexture(model, texPath,
                    bmpProps.replaceableId, bmpProps.wrapU, bmpProps.wrapV);
            texRef.slot = ir::TextureSlot::Diffuse;
            layer.textureRefs.push_back(texRef);
        } else {
            // Static diffuse — original behaviour
            ir::TextureRef texRef;
            texRef.textureIndex = findOrAddTexture(model, texPath,
                bmpProps.replaceableId, bmpProps.wrapU, bmpProps.wrapV);
            texRef.slot = ir::TextureSlot::Diffuse;
            layer.textureRefs.push_back(texRef);
        }
    } else if (matReplaceableId >= 1 && matReplaceableId <= 4) {
        // Material has no diffuseMap but is a Team Color / Team Glow / Cliff
        // replaceable. MDX convention is to emit a TEXS entry with the
        // replaceableId set and an empty path — the engine substitutes the
        // texture at runtime. Without this fallback, replaceable materials
        // that don't carry a bitmap lose their replaceableId on export.
        ir::TextureRef texRef;
        texRef.textureIndex = findOrAddTexture(model,
            /*path=*/std::string(),
            matReplaceableId,
            /*wrapU=*/false, /*wrapV=*/false);
        texRef.slot = ir::TextureSlot::Diffuse;
        layer.textureRefs.push_back(texRef);
    }

    // ────────────────────────────────────────────────────────────────
    //  Material-level animations
    //  These are shared across all layers of a Wc3Material and come
    //  from ParamBlock2 params on the Wc3Material itself (not on any
    //  specific bitmap).
    // ────────────────────────────────────────────────────────────────

    // KMTA — opacity animation (0..100 → 0..1)
    {
        Control* opacCtrl = getParamController(mtlRef, L"opacity");
        if (opacCtrl) {
            ir::InterpolationType interp = detectInterpFromController(opacCtrl);
            if (interp == ir::InterpolationType::None)
                interp = ir::InterpolationType::Linear;
            int32_t trackIdx = extractOpacityTrack(mtlRef, interp, model);
            if (trackIdx >= 0) {
                layer.alphaTrackIndex = trackIdx;
                // Global sequence detection for the opacity track
                int32_t gsIdx = detectAndRegisterGlobalSeq(opacCtrl, model);
                if (gsIdx >= 0)
                    model.floatTracks[trackIdx].globalSequenceIndex = gsIdx;
            }
        }
    }

    // TXAN — UV animation (translation/rotation/scale) with dedup for
    // composite materials that share controllers across layers.
    {
        int32_t taIdx = extractTextureAnimationFromMaterial(mtlRef, model);
        if (taIdx >= 0)
            layer.textureAnimationIndex = taIdx;
    }

    // --- Reforged PBR maps ---
    //
    // IMPORTANT: Sub-texture ORDER must match ORIG Blizzard HD layout:
    //   [0] Diffuse, [1] Normal, [2] ORM, [3] Emissive,
    //   [4] TeamColor, [5] Environment
    // This order is used by the HD shader to bind textures to GPU samplers.
    // Writing them in a different order causes the shader to bind wrong
    // textures to wrong samplers → visual artifacts (wrong colors, broken
    // lighting). DO NOT reorder these blocks.
    Texmap* normalMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"normalMap", normalMap) && normalMap) {
        std::string prefix = readMaterialPrefix(mtlRef, L"normalPrefix");
        std::string path = buildTexturePath(prefix, extractBitmapFileName(normalMap));
        BitmapProperties bp = extractBitmapProperties(normalMap);
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, path, bp.replaceableId,
                                            bp.wrapU, bp.wrapV);
        ref.slot = ir::TextureSlot::Normal;
        layer.textureRefs.push_back(ref);
    }

    Texmap* ormMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"ormMap", ormMap) && ormMap) {
        std::string prefix = readMaterialPrefix(mtlRef, L"ormPrefix");
        std::string path = buildTexturePath(prefix, extractBitmapFileName(ormMap));
        BitmapProperties bp = extractBitmapProperties(ormMap);
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, path, bp.replaceableId,
                                            bp.wrapU, bp.wrapV);
        ref.slot = ir::TextureSlot::ORM;
        layer.textureRefs.push_back(ref);
    }

    Texmap* emissiveMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"emissiveMap", emissiveMap) && emissiveMap) {
        std::string prefix = readMaterialPrefix(mtlRef, L"emissivePrefix");
        std::string path = buildTexturePath(prefix, extractBitmapFileName(emissiveMap));
        BitmapProperties bp = extractBitmapProperties(emissiveMap);
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, path, bp.replaceableId,
                                            bp.wrapU, bp.wrapV);
        ref.slot = ir::TextureSlot::Emissive;
        layer.textureRefs.push_back(ref);
    }

    // HD TeamColor Mask (slot position 4 — must come AFTER Emissive, BEFORE Env).
    // teamColorMap is the authoritative carrier for HD Team Color masks.
    // When populated, we emit a TextureRef with slot=TeamColor, replaceableId=1,
    // and an empty path — matching ORIG Blizzard convention where HD Reforged
    // materials have a sub-texture at slot 4 referencing a TEXS entry with
    // replId=1 and empty path.
    //
    // Material-level replaceableId dropdown stays on "Not Used" for these HD
    // materials — the TC-Mask lives exclusively in the teamColorMap slot.
    {
        Texmap* tcMap = nullptr;
        if (PBR::readTexmapByName(mtlRef, L"teamColorMap", tcMap) && tcMap) {
            ir::TextureRef tcRef;
            tcRef.textureIndex = findOrAddTexture(model,
                /*path=*/std::string(),
                /*replaceableId=*/1,  // Team Color
                /*wrapU=*/false, /*wrapV=*/false);
            tcRef.slot = ir::TextureSlot::TeamColor;
            layer.textureRefs.push_back(tcRef);

            // Debug log — trace when HD TC sub-texture is emitted
            {
                char tempPath[MAX_PATH];
                GetTempPathA(MAX_PATH, tempPath);
                std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
                std::ofstream log(logPath, std::ios::app);
                if (log.is_open()) {
                    log << "  [TC-HD] teamColorMap slot populated"
                        << " -> emitted TextureRef(slot=TeamColor, replId=1, path='')"
                        << " textureIndex=" << tcRef.textureIndex
                        << "\n";
                }
            }
        }
    }

    Texmap* envMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"environmentMap", envMap) && envMap) {
        std::string prefix = readMaterialPrefix(mtlRef, L"environmentPrefix");
        std::string path = buildTexturePath(prefix, extractBitmapFileName(envMap));
        BitmapProperties bp = extractBitmapProperties(envMap);
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, path, bp.replaceableId,
                                            bp.wrapU, bp.wrapV);
        ref.slot = ir::TextureSlot::Environment;
        layer.textureRefs.push_back(ref);
    }

    // Fresnel properties
    PBR::readFloatByName(mtlRef, L"emissiveGain", t, layer.emissiveGain);
    PBR::readFloatByName(mtlRef, L"fresnelOpacity", t, layer.fresnelOpacity);
    PBR::readFloatByName(mtlRef, L"fresnelTeamCol", t, layer.fresnelTeamColor);

    // Fresnel color
    float fR = 1.0f, fG = 1.0f, fB = 1.0f;
    PBR::readFloatByName(mtlRef, L"fresnelR", t, fR);
    PBR::readFloatByName(mtlRef, L"fresnelG", t, fG);
    PBR::readFloatByName(mtlRef, L"fresnelB", t, fB);
    layer.fresnelColor = Point3(fR, fG, fB);

    return layer;
}

void extractWc3Material(ReferenceTarget* mtlRef, ir::IRModel& model,
                        core::ExportErrorReporter& /*reporter*/)
{
    MaterialLevelProps matProps;
    ir::MaterialLayer layer = extractWc3Layer(mtlRef, model, matProps);

    ir::Material mat;
    mat.priorityPlane = matProps.priorityPlane;
    mat.flags = matProps.flags;
    mat.shaderName = std::move(matProps.shaderName);
    mat.layers.push_back(std::move(layer));
    model.materials.push_back(std::move(mat));
}

bool isWc3Composite(Mtl* mtl) {
    if (!mtl) return false;
    int n = mtl->NumSubMtls();
    if (n == 0) return false;
    bool hasWc3 = false;
    for (int i = 0; i < n; i++) {
        Mtl* sub = mtl->GetSubMtl(i);
        if (!sub) continue;
        if (sub->ClassID() != mdx_ids::WC3_MATERIAL)
            return false;
        hasWc3 = true;
    }
    return hasWc3;
}

void extractCompositeMaterial(Mtl* mtl, ir::IRModel& model,
                              core::ExportErrorReporter& /*reporter*/)
{
    ir::Material mat;
    MaterialLevelProps matProps;

    int n = mtl->NumSubMtls();
    for (int i = 0; i < n; i++) {
        Mtl* sub = mtl->GetSubMtl(i);
        if (!sub || sub->ClassID() != mdx_ids::WC3_MATERIAL) continue;
        auto* ref = dynamic_cast<ReferenceTarget*>(sub);
        if (!ref) continue;

        MaterialLevelProps layerProps;
        ir::MaterialLayer layer = extractWc3Layer(ref, model, layerProps);
        mat.layers.push_back(std::move(layer));

        // Use properties from the first layer for the material
        if (mat.layers.size() == 1) {
            matProps = std::move(layerProps);
        }
    }

    mat.priorityPlane = matProps.priorityPlane;
    mat.flags = matProps.flags;
    mat.shaderName = std::move(matProps.shaderName);
    model.materials.push_back(std::move(mat));
}

void extractStdMaterial(Mtl* mtl, ir::IRModel& model) {
    if (!mtl) return;
    auto* stdMat = dynamic_cast<StdMat2*>(mtl);
    if (!stdMat) return;

    ir::Material mat;
    ir::MaterialLayer layer;
    layer.blendMode = ir::BlendMode::None;
    layer.alpha = stdMat->GetOpacity(0);
    layer.twoSided = stdMat->GetTwoSided() != 0;

    Texmap* diffTex = stdMat->GetSubTexmap(ID_DI);
    if (diffTex) {
        std::string path = extractBitmapPath(diffTex);
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, path, 0, true, true);
        ref.slot = ir::TextureSlot::Diffuse;
        layer.textureRefs.push_back(ref);
    }

    mat.layers.push_back(std::move(layer));
    model.materials.push_back(std::move(mat));
}

} // anonymous namespace

MaterialMap extractMaterials(const std::vector<core::SceneNode>& nodes,
                             ir::IRModel& model,
                             core::ExportErrorReporter& reporter)
{
    // Reset the per-export dedup cache. The cache deduplicates TextureAnimation
    // entries across composite-material layers that share controllers.
    g_texAnimCache.clear();

    // Banner the replaceable-debug log (trunc so each export starts fresh)
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string logPath = std::string(tempPath) + "mdlx_replaceable_debug.log";
        std::ofstream log(logPath, std::ios::trunc);
        if (log.is_open()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            log << "=== MDLX replaceableId Debug Log ===\n"
                << "Export started " << st.wYear << "-" << st.wMonth << "-" << st.wDay
                << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "\n"
                << "----------------------------------------\n";
        }
    }

    std::unordered_map<Mtl*, int32_t> mtlToIndex;

    // ── Helper: run the appropriate extractor for a Max material ─────
    // Dispatches by ClassID (Wc3Material → Wc3 extractor; Wc3Composite →
    // composite extractor; anything else → stdMat fallback). Called from
    // both the mesh-material pass below and the ribbon-material pass
    // after it, so the logic stays in sync.
    auto addMaterial = [&](Mtl* mtl) -> int32_t {
        if (!mtl) return -1;
        auto existing = mtlToIndex.find(mtl);
        if (existing != mtlToIndex.end()) return existing->second;

        int32_t idx = static_cast<int32_t>(model.materials.size());
        auto* ref = dynamic_cast<ReferenceTarget*>(mtl);
        if (ref && mtl->ClassID() == mdx_ids::WC3_MATERIAL) {
            extractWc3Material(ref, model, reporter);
        } else if (isWc3Composite(mtl)) {
            extractCompositeMaterial(mtl, model, reporter);
        } else {
            extractStdMaterial(mtl, model);
        }
        mtlToIndex[mtl] = idx;
        return idx;
    };

    // ── Pass 1: mesh materials (unchanged behavior) ───────────────────
    for (auto& sn : nodes) {
        if (sn.category != core::NodeCategory::Mesh) continue;
        if (!sn.maxNode) continue;

        Mtl* mtl = sn.maxNode->GetMtl();
        if (!mtl) continue;
        addMaterial(mtl);
    }

    // ── Pass 2: ribbon-emitter materials ──────────────────────────────
    //
    // Wc3Ribbon stores its material reference in the paramblock
    // (pb_material = 7), NOT on the INode — so INode::GetMtl() returns
    // nullptr for a ribbon node and the mesh-pass above skips them.
    // Without this pass, the ribbon extractor's mtlToIndex.find() fails
    // and every ribbon ends up with materialIndex=-1, which the builder
    // writes as materialId=0 (usually the body-mesh material) — the
    // ribbon then renders with the wrong texture/blend mode ingame.
    //
    // Known symptom: Saurus warrior's Tail_Ribbon / Club_Ribbon exports
    // with materialId=0 instead of the intended materialId=4, losing
    // RibbonBlur1.blp and the additive blend mode.
    constexpr ParamID kRibbonMaterialParamID = 7;  // pb_material in Ribbon.h
    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Ribbon") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        IParamBlock2* pb = core::ParamBlockReader::findParamBlock(ref, 0);
        if (!pb) continue;

        Mtl* mtl = pb->GetMtl(kRibbonMaterialParamID, 0);
        if (!mtl) continue;
        addMaterial(mtl);
    }

    // Assign material indices to meshes
    for (auto& mesh : model.meshes) {
        if (mesh.nodeIndex < 0 || mesh.nodeIndex >= static_cast<int32_t>(model.nodes.size()))
            continue;
        auto* maxNode = model.nodes[mesh.nodeIndex].maxNode;
        if (!maxNode) continue;
        Mtl* mtl = maxNode->GetMtl();
        if (!mtl) continue;

        auto it = mtlToIndex.find(mtl);
        if (it != mtlToIndex.end())
            mesh.materialIndex = it->second;
    }

    return mtlToIndex;
}

} // namespace mdx_extract
