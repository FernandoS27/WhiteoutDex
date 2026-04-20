// MDLXExporter — Wc3Particles1 extractor implementation
//
// Extracts PE1 (PREM chunk) emitter data from Wc3Particles1 scripted plugins.
// PE1 differs from PE2 in several important ways — see exporter_handoff_pe1.md
// for the full specification.
//
// ── CRITICAL: Latitude/Longitude are RADIANS in MDX ──────────
// PE1's `latitude` and `longitude` fields in the PREM chunk are stored as
// radians. The plugin's ParamBlock2 stores them as degrees (UI spinner range
// 0..360). We must apply `degrees → radians` conversion on export, for both
// static values AND animation track keys/tangents.
//
// This is the OPPOSITE of PE2, where `latitude` is degrees in MDX (because
// the engine converts internally). Don't accidentally unify the two.
//
// ── Model path: two interface-exposed members, not paramblock ──
// Wc3Particles1 splits the model path into two std::wstring members:
//     m_modelPath   ("Bones1.MDL")
//     m_modelPrefix ("SharedModels\")
// Accessed via GetInterface() using IIDs WC3P1_MODEL_PATH_IID and
// WC3P1_MODEL_PREFIX_IID. We concatenate prefix + filename → MDX modelPath.
//
// ── Node flag bits: from UserProps ──
// PE1 plugin doesn't have its own Unshaded/Sort/LineEmit/etc checkboxes
// (unlike PE2). The six PE-level flag bits (15-20) come from Max node
// UserProps. If the importer didn't set these (check UserPropExists), they
// will default to false and the export will drop Unshaded etc.

#include "wc3_particle1_extractor.h"
#include "../mdx_class_ids.h"
#include "visibility_track_helper.h"
#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <control.h>
#include <cmath>
#include <fstream>

// ── Dedicated PRE1 extraction log ─────────────────────────────
static std::ofstream& pre1ExtLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_pre1_extract.log";
        log.open(path, std::ios::trunc);
        log << "=== PRE1 Extraction Log ===\n\n";
    }
    return log;
}
#define P1ELOG pre1ExtLog()
#define P1EFLUSH pre1ExtLog().flush()

// Interface IDs matching Wc3Particles1's GetInterface(ULONG) handler
// (Defined in Particles1/Particles.h — kept in sync here manually.)
constexpr ULONG WC3P1_MODEL_PATH_IID   = 0x7B3C8D01;  // MSTR* m_modelPath
constexpr ULONG WC3P1_MODEL_PREFIX_IID = 0x7B3C8D02;  // MSTR* m_modelPrefix

// ParamIDs from Wc3Particles1/Particles.h
namespace {
    enum P1Params : ParamID {
        P1_PB_COUNT         = 0,
        P1_PB_SPEED         = 1,
        P1_PB_EMISSION_RATE = 2,
        P1_PB_LIFE          = 3,
        P1_PB_ACCELERATION  = 4,  // == gravity in MDX
        P1_PB_LATITUDE      = 5,  // degrees in plugin, radians in MDX
        P1_PB_LONGITUDE     = 6,  // degrees in plugin, radians in MDX
        P1_PB_SCALE         = 7,  // viewport-only, not written to MDX
    };

    // UTF-16 → UTF-8 conversion (copy of PE2 helper; kept local for clarity)
    std::string wcharToUtf8(const wchar_t* wstr) {
        if (!wstr || !wstr[0]) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1,
                                       nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string result(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1,
                             result.data(), len, nullptr, nullptr);
        return result;
    }

    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    // ── PE1 float-track extractor ─────────────────────────────
    //
    // Reads a float paramblock parameter's controller as an ir::FloatTrack
    // and registers it in model.floatTracks. Returns the track index, or
    // -1 if the parameter has no controller or no keys.
    //
    // `valueScale` lets us apply a uniform factor to keys + tangents — used
    // for latitude/longitude to convert degrees → radians. Speed/Gravity/
    // EmissionRate are pass-through (scale=1.0).
    //
    // Pattern mirrors wc3_particle2_extractor's extractPE2FloatTrack exactly;
    // the only difference is the added `valueScale` parameter.
    int32_t extractPE1FloatTrack(IParamBlock2* pb, ParamID pid,
                                  ir::IRModel& model, const char* tagName,
                                  float valueScale = 1.0f)
    {
        if (!pb) return -1;
        Control* ctrl = pb->GetControllerByID(pid, 0);
        if (!ctrl) {
            P1ELOG << "    [" << tagName << "] no controller (pid=" << pid << ")\n";
            return -1;
        }
        int numKeys = ctrl->NumKeys();
        if (numKeys <= 0) {
            P1ELOG << "    [" << tagName << "] controller has 0 keys — static only\n";
            return -1;
        }

        ir::Track<float> track;
        track.interpolation = core::anim::detectInterpFromController(ctrl);
        if (track.interpolation == ir::InterpolationType::None)
            track.interpolation = ir::InterpolationType::Linear;

        std::vector<TimeValue> times;
        std::vector<float> values, inTans, outTans;
        bool hasTangents = false;
        core::anim::readFloatKeys(ctrl, times, values, inTans, outTans, hasTangents);

        // Fallback for non-IKeyControl controllers
        if (times.empty()) {
            for (int i = 0; i < numKeys; ++i) {
                TimeValue t = ctrl->GetKeyTime(i);
                float v = 0.0f;
                Interval iv = FOREVER;
                ctrl->GetValue(t, &v, iv);
                times.push_back(t);
                values.push_back(v);
                inTans.push_back(0.0f);
                outTans.push_back(0.0f);
            }
        }

        // Apply value scale (deg→rad for latitude/longitude tracks)
        track.keys.reserve(times.size());
        for (size_t i = 0; i < times.size(); i++) {
            ir::Keyframe<float> key;
            key.time = times[i];
            key.value      = values[i]  * valueScale;
            key.inTangent  = inTans[i]  * valueScale;
            key.outTangent = outTans[i] * valueScale;
            key.hasTangents = hasTangents;
            track.keys.push_back(key);
            P1ELOG << "      key[" << i << "] t=" << times[i]
                   << " v=" << key.value;
            if (hasTangents)
                P1ELOG << " in=" << key.inTangent << " out=" << key.outTangent;
            P1ELOG << "\n";
        }

        int32_t gsIdx = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
        if (gsIdx >= 0) {
            track.globalSequenceIndex = gsIdx;
            P1ELOG << "    [" << tagName << "] → GlobalSequence idx=" << gsIdx << "\n";
        }

        int32_t idx = static_cast<int32_t>(model.floatTracks.size());
        model.floatTracks.push_back(std::move(track));
        P1ELOG << "    [" << tagName << "] " << numKeys
               << " keys → floatTrack[" << idx << "] interp="
               << static_cast<int>(track.interpolation)
               << " scale=" << valueScale << "\n";
        return idx;
    }
}

namespace mdx_extract {

void extractParticles1(const std::vector<core::SceneNode>& nodes,
                       ir::IRModel& model,
                       core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Particles1") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        IParamBlock2* pb = PBR::findParamBlock(ref, 0);
        if (!pb) continue;

        TimeValue t = 0;
        ir::ParticleEmitter pe;
        pe.nodeIndex = sn.nodeIndex;
        pe.variant = 1;

        std::string nameA = wcharToUtf8(sn.maxNode->GetName());
        P1ELOG << "─────────────────────────────────────\n";
        P1ELOG << "PRE1 extracted: sn.nodeIndex=" << sn.nodeIndex
               << " name='" << nameA << "'\n";

        // ── Static values ─────────────────────────────────────
        // Plain pass-through for scalar fields.
        pe.speed        = PBR::readFloat(pb, P1_PB_SPEED,         t);
        pe.emissionRate = PBR::readFloat(pb, P1_PB_EMISSION_RATE, t);
        pe.lifespan     = PBR::readFloat(pb, P1_PB_LIFE,          t);
        pe.gravity      = PBR::readFloat(pb, P1_PB_ACCELERATION,  t);

        // Latitude/Longitude: plugin stores DEGREES (UI range 0..360),
        // MDX stores RADIANS. Apply conversion here — must match the
        // identical conversion applied to track keys/tangents below.
        pe.latitude  = PBR::readFloat(pb, P1_PB_LATITUDE,  t) * kDegToRad;
        pe.longitude = PBR::readFloat(pb, P1_PB_LONGITUDE, t) * kDegToRad;

        // ── Model path: two IIDs, concatenate prefix + filename ───
        // The plugin separates directory and filename for UI purposes
        // (two edit boxes in the Model Options rollout). The MDX format
        // expects them concatenated as a single 260-byte field.
        std::string modelName, modelPrefix;
        auto* pathPtr = static_cast<const MSTR*>(
            obj->GetInterface(WC3P1_MODEL_PATH_IID));
        if (pathPtr && pathPtr->Length() > 0)
            modelName = wcharToUtf8(pathPtr->data());

        auto* prefixPtr = static_cast<const MSTR*>(
            obj->GetInterface(WC3P1_MODEL_PREFIX_IID));
        if (prefixPtr && prefixPtr->Length() > 0)
            modelPrefix = wcharToUtf8(prefixPtr->data());

        // Strip any path separator from the filename half — defensive
        // against a user pasting a full path into the filename field.
        // The prefix is expected to already end in '\' if non-empty;
        // NeoDex convention does not inject a separator on concat.
        if (!modelName.empty()) {
            size_t lastSep = std::string::npos;
            for (size_t i = modelName.size(); i > 0; --i) {
                char c = modelName[i - 1];
                if (c == '\\' || c == '/') { lastSep = i - 1; break; }
            }
            if (lastSep != std::string::npos)
                modelName = modelName.substr(lastSep + 1);
        }

        if (!modelName.empty()) {
            // Verbatim if the user pasted a full path (filename contains
            // a separator after the strip above — shouldn't happen, but
            // be safe). Otherwise concat.
            if (modelName.find('\\') != std::string::npos ||
                modelName.find('/')  != std::string::npos)
            {
                pe.modelPath = modelName;
            } else {
                pe.modelPath = modelPrefix + modelName;
            }
        }

        // ── Node flags ────────────────────────────────────────
        // PE1 has no Unshaded/Sort/LineEmitter/Unfogged/ModelSpace/XYQuad
        // plugin parameters — those are PE2-only features. The Wc3Particles1
        // plugin mirrors Max's SuperSpray, which has none of these checkboxes.
        //
        // The MDX PREM chunk does *technically* inherit bits 15-20 from the
        // generic Node header (every node type has this u32 flags field),
        // but there's no UI surface to author them for PE1 emitters, and
        // they probably have no runtime effect on the WC3 engine for PE1
        // nodes (wowdev spec documents them only for PE2).
        //
        // Blizzard-authored models like RedDragonWelp.mdx do have bit 15
        // (Unshaded) set on the PREM node — likely a carry-over from their
        // authoring tool. If exact byte-for-byte roundtrip of those bits
        // is needed later, the fix belongs on the importer side (stash
        // the MDX flag bits on ir::IRModel::Node::nodeFlags or an INode
        // UserProp) plus a buildNode() change to merge them in.
        //
        // For now: don't fabricate any bits here. Let buildNode() apply
        // only Node::NodeFlag::ParticleEmitter (0x1000).
        pe.flags = 0;

        // ── Static-value log ──────────────────────────────────
        P1ELOG << "  === PE1 Static Values ===\n";
        P1ELOG << "  -- Animatable --\n";
        P1ELOG << "    Speed        = " << pe.speed << "\n";
        P1ELOG << "    EmissionRate = " << pe.emissionRate << "\n";
        P1ELOG << "    Gravity      = " << pe.gravity
               << "  (plugin PB_ACCELERATION)\n";
        P1ELOG << "    Latitude     = " << pe.latitude
               << " rad  (from plugin degrees × π/180)\n";
        P1ELOG << "    Longitude    = " << pe.longitude
               << " rad  (from plugin degrees × π/180)\n";
        P1ELOG << "  -- Static --\n";
        P1ELOG << "    Life         = " << pe.lifespan << "\n";
        P1ELOG << "  -- Model --\n";
        P1ELOG << "    prefix       = '" << modelPrefix << "'\n";
        P1ELOG << "    filename     = '" << modelName << "'\n";
        P1ELOG << "    fullPath     = '" << pe.modelPath << "'\n";
        P1EFLUSH;

        // ── Animation tracks ──────────────────────────────────
        //
        // MDX chunk mapping (see handoff_pe1.md):
        //   KPES ← Speed controller           (scalar, pass-through)
        //   KPEE ← EmissionRate controller    (scalar, pass-through)
        //   KPEG ← Gravity controller         (scalar, pass-through)
        //   KPEL ← Latitude controller        (DEG→RAD conversion)
        //   KPLN ← Longitude controller       (DEG→RAD conversion)
        //   KPEV ← node visibility controller (set below)
        //
        // PE1 `Life` is NOT animatable — plugin's PB_LIFE has no
        // P_ANIMATABLE flag, and MDX PREM has no track chunk for it.
        P1ELOG << "  PE1 Tracks:\n";
        pe.speedTrackIndex = extractPE1FloatTrack(
            pb, P1_PB_SPEED,         model, "KPES");
        pe.emissionRateTrackIndex = extractPE1FloatTrack(
            pb, P1_PB_EMISSION_RATE, model, "KPEE");
        pe.gravityTrackIndex = extractPE1FloatTrack(
            pb, P1_PB_ACCELERATION,  model, "KPEG");
        pe.latitudeTrackIndex = extractPE1FloatTrack(
            pb, P1_PB_LATITUDE,      model, "KPEL", kDegToRad);
        pe.longitudeTrackIndex = extractPE1FloatTrack(
            pb, P1_PB_LONGITUDE,     model, "KPLN", kDegToRad);

        // Node-level visibility — handles on_off_float etc.
        pe.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);
        P1ELOG << "    [KPEV visibility] trackIndex="
               << pe.visibilityTrackIndex << "\n";
        P1EFLUSH;

        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
