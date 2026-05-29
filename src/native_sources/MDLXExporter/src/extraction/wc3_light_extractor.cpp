// MDLXExporter — Wc3Light extractor implementation
//
// Reads Wc3 Light objects (LITE chunk) from the Max scene. Wc3Light
// is a simpleManipulator scripted plugin; we detect it via classifier's
// customTag "Wc3Light" (set up in mdx_node_registration.h).
//
// Per exporter_handoff_light.md, the MDX LITE chunk contains:
//   - Node common block (via buildNode in builder)
//   - type (0=Omni, 1=Directional, 2=Ambient)
//   - attenuationStart, attenuationEnd (floats)
//   - color (Vec3, BGR order!), intensity (float)
//   - ambientColor (Vec3, BGR!), ambientIntensity (float)
//   - Optional animation tracks: KLAS/KLAE/KLAC/KLAI/KLBC/KLBI/KLAV
//
// CRITICAL: All colors stored as BGR in MDX, not RGB. We swap here in
// the extractor (like geoset_anim_extractor Patch B does for KGAC)
// because the builder/writer pipeline writes Color.{r,g,b} directly to
// Vec3.{x,y,z} without any transformation. So what we store in IR as
// "color.r" will become MDX byte "color.x", etc.
//
// The fields are named confusingly in the plugin:
//   Plugin "ShadowColor" (UI: "Shadow Light")  →  MDX "color" (primary)
//   Plugin "ShadowValue"                        →  MDX "intensity"
//   Plugin "AmbColor"    (UI: "Ambient Light") →  MDX "ambientColor"
//   Plugin "AmbValue"                           →  MDX "ambientIntensity"

#include "wc3_light_extractor.h"
#include "../mdx_class_ids.h"
#include "visibility_track_helper.h"
#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <iparamb2.h>
#include <control.h>
#include <decomp.h>
#include <fstream>
#include <windows.h>

namespace mdx_extract {

namespace {

// ── Light extraction debug log ──
std::ofstream& liteLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_light_debug.log";
        log.open(path, std::ios::trunc);
        log << "=== MDX Light Extraction Log ===\n\n";
    }
    return log;
}
#define LLOG liteLog()
#define LFLUSH liteLog().flush()

// ── Float track extraction ──
//
// For a named float parameter on the Wc3Light ref, produce an IR
// float track and register it in model.floatTracks. Returns the
// track index, or -1 if the parameter has no animation.
//
// We use getParamControllerDirect (Autodesk-recommended GetControllerByID
// path) — same pattern as GEOA Patch B, which is validated to work on
// scripted simpleManipulator plugins.
int32_t extractLightFloatTrack(ReferenceTarget* ref, const wchar_t* paramName,
                                ir::IRModel& model, const char* tagName)
{
    Control* ctrl = core::anim::getParamControllerDirect(ref, paramName);
    if (!ctrl) {
        LLOG << "    [" << tagName << "] no controller for '"
             << (paramName ? "<name>" : "?") << "'\n";
        return -1;
    }

    int numKeys = ctrl->NumKeys();
    if (numKeys <= 0) {
        LLOG << "    [" << tagName << "] controller has 0 keys — static only\n";
        return -1;
    }

    ir::Track<float> track;
    // Default interpolation: Linear. Bezier/Hermite would require
    // tangent extraction which is error-prone on scripted plugins.
    // Light animations in MDX are almost always Linear in the wild.
    track.interpolation = ir::InterpolationType::Linear;

    for (int i = 0; i < numKeys; ++i) {
        TimeValue t = ctrl->GetKeyTime(i);
        float v = 0.0f;
        Interval iv = FOREVER;
        ctrl->GetValue(t, &v, iv);

        ir::Keyframe<float> key;
        key.time = t;
        key.value = v;
        track.keys.push_back(key);

        LLOG << "      key[" << i << "] t=" << t
             << " v=" << v << "\n";
    }

    // Global Sequence detection via ORT
    int afterORT = ctrl->GetORT(ORT_AFTER);
    if (afterORT == ORT_CYCLE || afterORT == ORT_LOOP) {
        if (!track.keys.empty()) {
            TimeValue duration = track.keys.back().time;
            int32_t gsIdx = core::anim::registerGlobalSequence(model, duration);
            if (gsIdx >= 0) {
                track.globalSequenceIndex = gsIdx;
                LLOG << "    [" << tagName << "] → GlobalSequence idx="
                     << gsIdx << " dur=" << duration << "\n";
            }
        }
    }

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    LLOG << "    [" << tagName << "] " << numKeys
         << " keys → floatTrack[" << idx << "]\n";
    return idx;
}

// ── Color track extraction ──
//
// Same procedural-sampling pattern as the float version but reads a
// Point3 value and applies BGR swap before storing. This mirrors
// geoset_anim_extractor's Patch B (KGAC).
int32_t extractLightColorTrack(ReferenceTarget* ref, const wchar_t* paramName,
                                ir::IRModel& model, const char* tagName)
{
    Control* ctrl = core::anim::getParamControllerDirect(ref, paramName);
    if (!ctrl) {
        LLOG << "    [" << tagName << "] no controller\n";
        return -1;
    }

    int numKeys = ctrl->NumKeys();
    if (numKeys <= 0) {
        LLOG << "    [" << tagName << "] controller has 0 keys — static only\n";
        return -1;
    }

    ir::Track<Color> track;
    track.interpolation = ir::InterpolationType::Linear;

    for (int i = 0; i < numKeys; ++i) {
        TimeValue t = ctrl->GetKeyTime(i);
        Point3 val(1.0f, 1.0f, 1.0f);
        Interval iv = FOREVER;
        ctrl->GetValue(t, &val, iv);

        ir::Keyframe<Color> key;
        key.time = t;
        // BGR swap: Max gives us RGB in val.{x,y,z}; MDX stores BGR
        // in color.{r,g,b} (those fields become byte-stream order).
        key.value = Color(val.z, val.y, val.x);
        track.keys.push_back(key);

        LLOG << "      key[" << i << "] t=" << t
             << " RGB=(" << val.x << "," << val.y << "," << val.z << ")"
             << " → BGR stored\n";
    }

    int afterORT = ctrl->GetORT(ORT_AFTER);
    if (afterORT == ORT_CYCLE || afterORT == ORT_LOOP) {
        if (!track.keys.empty()) {
            TimeValue duration = track.keys.back().time;
            int32_t gsIdx = core::anim::registerGlobalSequence(model, duration);
            if (gsIdx >= 0) {
                track.globalSequenceIndex = gsIdx;
                LLOG << "    [" << tagName << "] → GlobalSequence idx="
                     << gsIdx << " dur=" << duration << "\n";
            }
        }
    }

    int32_t idx = static_cast<int32_t>(model.colorTracks.size());
    model.colorTracks.push_back(std::move(track));
    LLOG << "    [" << tagName << "] " << numKeys
         << " keys → colorTrack[" << idx << "]\n";
    return idx;
}

} // anonymous namespace

void extractLights(const std::vector<core::SceneNode>& nodes,
                   ir::IRModel& model,
                   core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    LLOG << "── extractLights ──\n\n";

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Light") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) {
            LLOG << "  [SKIP] node has no ReferenceTarget\n";
            continue;
        }

        ir::Light light;
        light.nodeIndex = sn.nodeIndex;

        // ── Static values at t=0 ──
        // Reading at TimeValue=0 ensures we get the base value even if
        // the property is animated (Max would otherwise return the
        // interpolated value at the current slider time).
        TimeValue t = 0;

        // LightType: plugin dropdown is 1-based (1=Omni, 2=Dir, 3=Amb)
        // MDX is 0-based (0=Omni, 1=Dir, 2=Amb).
        int lightType = 1;
        PBR::readIntByName(ref, L"LightType", t, lightType);
        switch (lightType) {
        case 1: light.type = ir::Light::Type::Omni;        break;
        case 2: light.type = ir::Light::Type::Directional; break;
        case 3: light.type = ir::Light::Type::Ambient;     break;
        default: light.type = ir::Light::Type::Omni;       break;
        }

        PBR::readFloatByName(ref, L"DecayStart", t, light.attenuationStart);
        PBR::readFloatByName(ref, L"DecayEnd",   t, light.attenuationEnd);

        // Field mapping (Plugin → MDX):
        //   ShadowColor → color          (primary light color)
        //   ShadowValue → intensity
        //   AmbColor    → ambientColor
        //   AmbValue    → ambientIntensity
        // ─── MDX Light color convention is ASYMMETRIC ───
        // Static color (this path)        : stored as RGB in MDX
        // Animated color keys (KLBC track): stored as BGR in MDX
        // Same quirk as GeosetAnim VertexColor — confirmed empirically
        // against game/Magos rendering. NeoDex IOFixColor splits
        // identically (static = RGB, animated = BGR).
        Color primaryRGB(1.0f, 1.0f, 1.0f);
        PBR::readColorByName(ref, L"ShadowColor", t, primaryRGB);
        light.color = primaryRGB;

        PBR::readFloatByName(ref, L"ShadowValue", t, light.intensity);

        Color ambRGB(0.0f, 0.0f, 0.0f);
        PBR::readColorByName(ref, L"AmbColor", t, ambRGB);
        light.ambientColor = ambRGB;

        PBR::readFloatByName(ref, L"AmbValue", t, light.ambientIntensity);

        LLOG << "  Light node[" << sn.nodeIndex << "] (maxNode='"
             << (sn.maxNode->GetName() ? "set" : "null") << "'):\n";
        LLOG << "    type=" << static_cast<int>(light.type)
             << "  attStart=" << light.attenuationStart
             << "  attEnd="   << light.attenuationEnd << "\n";
        LLOG << "    primary  RGB=(" << primaryRGB.r << "," << primaryRGB.g << "," << primaryRGB.b << ")"
             << " stored as RGB  intensity=" << light.intensity << "\n";
        LLOG << "    ambient  RGB=(" << ambRGB.r << "," << ambRGB.g << "," << ambRGB.b << ")"
             << " stored as RGB  ambIntensity=" << light.ambientIntensity << "\n";

        // ── Animation tracks ──
        //
        // Each track extraction checks if the corresponding plugin
        // parameter is animated; returns -1 if static. Builder will
        // then omit the track from the MDX output.
        LLOG << "  Tracks:\n";
        light.attStartTrackIndex =
            extractLightFloatTrack(ref, L"DecayStart",  model, "KLAS");
        light.attEndTrackIndex =
            extractLightFloatTrack(ref, L"DecayEnd",    model, "KLAE");
        light.colorTrackIndex =
            extractLightColorTrack(ref, L"ShadowColor", model, "KLAC");
        light.intensityTrackIndex =
            extractLightFloatTrack(ref, L"ShadowValue", model, "KLAI");
        light.ambColorTrackIndex =
            extractLightColorTrack(ref, L"AmbColor",    model, "KLBC");
        light.ambIntensityTrackIndex =
            extractLightFloatTrack(ref, L"AmbValue",    model, "KLBI");

        // ── Visibility track (KLAV) ──
        // Uses the node's visibility controller (set by Wc3 Light scripted
        // plugin via A_Visibility animatable parameter, typically on_off_float).
        light.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);
        LLOG << "    [KLAV] visibilityTrackIndex="
             << light.visibilityTrackIndex << "\n";

        LFLUSH;
        model.lights.push_back(std::move(light));
    }

    LLOG << "\n  Total lights extracted: " << model.lights.size() << "\n";
    LFLUSH;
}

} // namespace mdx_extract
