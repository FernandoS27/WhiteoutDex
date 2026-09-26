// MDLXExporter — Wc3Popcorn (CornEmitter) extractor implementation
#include "wc3_popcorn_extractor.h"
#include "../mdx_class_ids.h"
#include "visibility_track_helper.h"
#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <control.h>
#include <modstack.h>
#include <algorithm>
#include <utility>

namespace mdx_extract {

namespace {

// ── Animation track extraction ──
//
// Wc3Popcorn is a simpleManipulator scripted plugin, so its params must be
// reached through getParamControllerDirect (the GetControllerByID path) —
// the same route wc3_light_extractor uses, and the mirror image of the
// importer's animateFloatNamedScript / animateColorNamedScript, which write
// these controllers by MaxScript property name.
//
// MDX chunk mapping (see the CORN writer in WhiteoutLib):
//   emissionRate → KPPE   speed → KPPS   lifeSpan → KPPL
//   alpha        → KPPA   baseColor → KPPC
// KPPV is the node's own visibility controller, handled separately.

int32_t extractCornFloatTrack(ReferenceTarget* ref, const wchar_t* paramName,
                              ir::IRModel& model)
{
    Control* ctrl = core::anim::getParamControllerDirect(ref, paramName);
    if (!ctrl) return -1;
    const int numKeys = ctrl->NumKeys();
    if (numKeys <= 0) return -1;

    ir::Track<float> track;
    // Linear, matching the light extractor: tangent recovery off a scripted
    // plugin's controller is error-prone, and every CORN track Blizzard ships
    // is interpolationType 0 or 1 anyway.
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
    }

    const int afterORT = ctrl->GetORT(ORT_AFTER);
    if ((afterORT == ORT_CYCLE || afterORT == ORT_LOOP) && !track.keys.empty()) {
        int32_t gsIdx = core::anim::registerGlobalSequence(model, track.keys.back().time);
        if (gsIdx >= 0) track.globalSequenceIndex = gsIdx;
    }

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

int32_t extractCornColorTrack(ReferenceTarget* ref, const wchar_t* paramName,
                              ir::IRModel& model)
{
    Control* ctrl = core::anim::getParamControllerDirect(ref, paramName);
    if (!ctrl) return -1;
    const int numKeys = ctrl->NumKeys();
    if (numKeys <= 0) return -1;

    ir::Track<Color> track;
    track.interpolation = ir::InterpolationType::Linear;

    for (int i = 0; i < numKeys; ++i) {
        TimeValue t = ctrl->GetKeyTime(i);
        Point3 val(1.0f, 1.0f, 1.0f);
        Interval iv = FOREVER;
        ctrl->GetValue(t, &val, iv);
        ir::Keyframe<Color> key;
        key.time = t;
        // No BGR swap. Unlike LITE, the CORN colour path is straight RGB on
        // both sides — the disassembler's mapColorTrack and the static
        // corn.color field below both pass x,y,z through untouched.
        key.value = Color(val.x, val.y, val.z);
        track.keys.push_back(key);
    }

    const int afterORT = ctrl->GetORT(ORT_AFTER);
    if ((afterORT == ORT_CYCLE || afterORT == ORT_LOOP) && !track.keys.empty()) {
        int32_t gsIdx = core::anim::registerGlobalSequence(model, track.keys.back().time);
        if (gsIdx >= 0) track.globalSequenceIndex = gsIdx;
    }

    int32_t idx = static_cast<int32_t>(model.colorTracks.size());
    model.colorTracks.push_back(std::move(track));
    return idx;
}

} // anonymous namespace

// Popcorn data is stored in ir::ParticleEmitter with variant=3.
// The model builder converts variant=3 to mdx::CornEmitter.

void extractPopcorn(const std::vector<core::SceneNode>& nodes,
                    ir::IRModel& model,
                    core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Popcorn") continue;
        if (!sn.maxNode) continue;

        Object* obj = sn.maxNode->GetObjectRef();
        while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;
        const bool neoDex = obj->ClassID() == mdx_ids::NEODEX_POPCORN;

        TimeValue t = 0;
        ir::ParticleEmitter pe;
        pe.nodeIndex = sn.nodeIndex;
        pe.variant = 3; // CornEmitter marker

        PBR::readFloatByName(ref, L"LifeSpan", t, pe.lifespan);
        PBR::readFloatByName(ref, L"EmissionRate", t, pe.emissionRate);
        PBR::readFloatByName(ref, L"Speed", t, pe.speed);
        // ReplaceableId / team color used to be on the Popcorn plugin but
        // the engine ignores both — the plugin no longer exposes them, and
        // we don't read them on export. NeoDex's plug-in still has the
        // dropdown and writes it as value - 1, so do the same for it.
        if (neoDex) {
            int repl = 0;
            if (PBR::readIntByName(ref, L"replaceableId", t, repl))
                pe.replaceableId = std::max(0, repl - 1);
        }

        // Render flags: pack the Wc3Popcorn bool params into pe.flags using
        // the same bit positions the disassembler uses. The model builder
        // OR-merges these into corn.node.flags on the way back out to MDX.
        //   0x8000  Unshaded
        //   0x20000 PopcornUnfogged
        //   0x40000 PopcornScaling
        BOOL b = FALSE;
        if (PBR::readBoolByName(ref, L"flagUnshaded", t, b) && b) pe.flags |= 0x8000u;
        if (PBR::readBoolByName(ref, L"flagUnfogged", t, b) && b) pe.flags |= 0x20000u;
        if (PBR::readBoolByName(ref, L"flagScaling",  t, b) && b) pe.flags |= 0x40000u;

        // Helper: wstring → UTF-8.
        auto wideToUtf8 = [](const std::wstring& w) -> std::string {
            if (w.empty()) return {};
            int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
            if (len <= 1) return {};
            std::string u8(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u8.data(), len,
                                nullptr, nullptr);
            return u8;
        };

        // Effect file path
        std::wstring pathW;
        if (PBR::readStringByName(ref, L"popcornPath", t, pathW) && !pathW.empty())
            pe.modelPath = wideToUtf8(pathW);

        // PopcornFX anim-visibility gate. The scripted plugin's `rawFlags`
        // carries the raw comma-separated guide string (e.g. "Stand=on,
        // Death=off"); round-trip it verbatim so the renderer can drive the
        // emitter the same way the engine does.
        std::wstring guideW;
        if (PBR::readStringByName(ref, L"rawFlags", t, guideW) && !guideW.empty())
            pe.animVisibilityGuide = wideToUtf8(guideW);
        // NeoDex rebuilds an empty rawFlags from its flag checkboxes
        // (NeoDexSceneParser LoadCornEmitter).
        if (neoDex && pe.animVisibilityGuide.empty()) {
            static const std::pair<const wchar_t*, const char*> kFlags[] = {
                { L"flagAlways", "Always" }, { L"flagBirth", "Birth" },
                { L"flagDeath", "Death" }, { L"flagDissipate", "Dissipate" },
                { L"flagPortrait", "Portrait" },
            };
            for (const auto& [param, word] : kFlags) {
                BOOL on = FALSE;
                if (PBR::readBoolByName(ref, param, t, on) && on) {
                    if (!pe.animVisibilityGuide.empty()) pe.animVisibilityGuide += ',';
                    pe.animVisibilityGuide += word;
                }
            }
        }

        // Base color (stored in segmentColors[0] + segmentAlpha[0] for variant==3)
        Color baseColor(1, 1, 1);
        PBR::readColorByName(ref, L"baseColor", t, baseColor);
        pe.segmentColors[0] = baseColor;
        float alpha = 1.0f;
        PBR::readFloatByName(ref, L"alpha", t, alpha);
        // Older NeoDex imports stored alpha as 0..100; NeoDex's exporter
        // scales such values back.
        if (neoDex && alpha > 1.0f) alpha /= 100.0f;
        pe.segmentAlpha[0] = alpha;

        // Animation tracks. The static values above are the t=0 samples of
        // these same params; the builder writes both, exactly as Blizzard's
        // own CORN chunks carry a base value next to each track.
        pe.emissionRateTrackIndex = extractCornFloatTrack(ref, L"emissionRate", model);
        pe.speedTrackIndex        = extractCornFloatTrack(ref, L"speed", model);
        pe.lifespanTrackIndex     = extractCornFloatTrack(ref, L"lifeSpan", model);
        pe.alphaTrackIndex        = extractCornFloatTrack(ref, L"alpha", model);
        pe.colorTrackIndex        = extractCornColorTrack(ref, L"baseColor", model);
        // KPPV — the importer materialises it as the node's own visibility
        // track, so this is where it has to come back from.
        pe.visibilityTrackIndex   = extractVisibilityTrack(sn.maxNode, model);

        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
