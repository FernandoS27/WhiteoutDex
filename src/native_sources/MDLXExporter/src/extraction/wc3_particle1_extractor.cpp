// MDLXExporter — Wc3Particles1 extractor implementation
// CHANGES: NeoDex BlizzPart1 compatibility via ClassID-based branching.
//   - WhiteoutDex: ORIGINAL index-based (unchanged)
//   - NeoDex:      name-based with BlizzPart1.ms param names
//
// CRITICAL: Latitude/Longitude → RADIANS in MDX (both plugins store degrees)

#include "wc3_particle1_extractor.h"
#include "../mdx_class_ids.h"
#include "controller_track_helper.h"
#include "visibility_track_helper.h"
#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <control.h>
#include <modstack.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

static std::ofstream& pre1ExtLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_pre1_extract.log";
        log.open(path, std::ios::trunc);
        log << "=== PRE1 Extraction Log (dual-path) ===\n\n";
    }
    return log;
}
#define P1ELOG pre1ExtLog()
#define P1EFLUSH pre1ExtLog().flush()

constexpr ULONG WC3P1_MODEL_PATH_IID   = 0x7B3C8D01;
constexpr ULONG WC3P1_MODEL_PREFIX_IID = 0x7B3C8D02;

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

enum P1Params : ParamID {
    P1_PB_COUNT=0, P1_PB_SPEED=1, P1_PB_EMISSION_RATE=2, P1_PB_LIFE=3,
    P1_PB_ACCELERATION=4, P1_PB_LATITUDE=5, P1_PB_LONGITUDE=6, P1_PB_SCALE=7,
};

std::string wcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// Index-based track with scale (WDX)
int32_t extractTrackByIndex(IParamBlock2* pb, ParamID pid,
                             ir::IRModel& model, const char* tag,
                             float scale = 1.0f) {
    if (!pb) return -1;
    int32_t idx = mdx_extract::extractFloatControllerTrack(
        pb->GetControllerByID(pid, 0), model, scale);
    if (idx >= 0)
        P1ELOG << "  [" << tag << "] " << model.floatTracks[idx].keys.size() << " keys\n";
    return idx;
}

// Name-based track with scale (NeoDex)
int32_t extractTrackByName(ReferenceTarget* ref, const wchar_t* name,
                            ir::IRModel& model, const char* tag,
                            float scale = 1.0f) {
    int32_t idx = mdx_extract::extractFloatControllerTrack(
        core::anim::getParamControllerDirect(ref, name), model, scale);
    if (idx >= 0)
        P1ELOG << "  [" << tag << "] " << model.floatTracks[idx].keys.size() << " keys\n";
    return idx;
}

} // anon

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

        Object* baseObj = obj;
        while (baseObj && baseObj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            baseObj = static_cast<IDerivedObject*>(baseObj)->GetObjRef();
        Class_ID cid = baseObj ? baseObj->ClassID() : Class_ID(0,0);
        const bool isNeoDex = (cid == mdx_ids::NEODEX_PARTICLES1);
        // By-name reads look at the object's own param blocks, which a
        // modifier on the node would hide behind its derived object.
        if (isNeoDex) ref = baseObj;

        TimeValue t = 0;
        ir::ParticleEmitter pe;
        pe.nodeIndex = sn.nodeIndex;
        pe.variant = 1;

        std::string nameA = wcharToUtf8(sn.maxNode->GetName());
        P1ELOG << "── PE1 " << (isNeoDex ? "NeoDex" : "WDX")
               << " '" << nameA << "' ──\n";

        if (!isNeoDex) {
            // ═══ WHITEOUTDEX — original index-based, UNCHANGED ═══
            IParamBlock2* pb = PBR::findParamBlock(ref, 0);
            if (!pb) continue;

            pe.speed        = PBR::readFloat(pb, P1_PB_SPEED, t);
            pe.emissionRate = PBR::readFloat(pb, P1_PB_EMISSION_RATE, t);
            pe.lifespan     = PBR::readFloat(pb, P1_PB_LIFE, t);
            pe.gravity      = PBR::readFloat(pb, P1_PB_ACCELERATION, t);
            pe.latitude     = PBR::readFloat(pb, P1_PB_LATITUDE, t)  * kDegToRad;
            pe.longitude    = PBR::readFloat(pb, P1_PB_LONGITUDE, t) * kDegToRad;

            // Model path via IID
            std::string modelName, modelPrefix;
            auto* pp = static_cast<const MSTR*>(obj->GetInterface(WC3P1_MODEL_PATH_IID));
            if (pp && pp->Length() > 0) modelName = wcharToUtf8(pp->data());
            auto* xp = static_cast<const MSTR*>(obj->GetInterface(WC3P1_MODEL_PREFIX_IID));
            if (xp && xp->Length() > 0) modelPrefix = wcharToUtf8(xp->data());
            if (!modelName.empty()) {
                size_t ls = modelName.find_last_of("\\/");
                if (ls != std::string::npos) modelName = modelName.substr(ls + 1);
                pe.modelPath = modelPrefix + modelName;
            }

            pe.flags = 0;

            pe.speedTrackIndex        = extractTrackByIndex(pb, P1_PB_SPEED,         model, "KPES");
            pe.emissionRateTrackIndex = extractTrackByIndex(pb, P1_PB_EMISSION_RATE, model, "KPEE");
            pe.gravityTrackIndex      = extractTrackByIndex(pb, P1_PB_ACCELERATION,  model, "KPEG");
            pe.latitudeTrackIndex     = extractTrackByIndex(pb, P1_PB_LATITUDE,      model, "KPLT", kDegToRad);
            pe.longitudeTrackIndex    = extractTrackByIndex(pb, P1_PB_LONGITUDE,     model, "KPLN", kDegToRad);
            pe.lifespanTrackIndex     = extractTrackByIndex(pb, P1_PB_LIFE,          model, "KPEL");

        } else {
            // ═══ NEODEX — name-based with BlizzPart1.ms names ═══
            // The speed is "radius" (UI label "Speed:"); latitude/longitude
            // hold the MDX values as they are — NeoDex's importer and
            // exporter copy them without a unit conversion
            // (NeoDexSceneRebuilder / NeoDexSceneParser, PE1).
            PBR::readFloatByName(ref, L"radius",     t, pe.speed);
            PBR::readFloatByName(ref, L"PartEmit",   t, pe.emissionRate);
            PBR::readFloatByName(ref, L"Life",       t, pe.lifespan);
            PBR::readFloatByName(ref, L"Gravity",    t, pe.gravity);
            PBR::readFloatByName(ref, L"latitude",   t, pe.latitude);
            PBR::readFloatByName(ref, L"longitude",  t, pe.longitude);

            // Model path, as NeoDex builds it: "path" (prefix, default
            // war3mapImported\) + "Part" (file name, stored without extension
            // by the importer), any .mdl dropped, then ".MDL" appended; an
            // empty name becomes war3mapImported\dummy.MDL.
            std::string modelName, modelPrefix = "war3mapImported\\";
            { std::wstring w; PBR::readStringByName(ref, L"Part", t, w);
              if (!w.empty()) modelName = wcharToUtf8(w.c_str()); }
            { std::wstring w;
              if (PBR::readStringByName(ref, L"path", t, w)) modelPrefix = wcharToUtf8(w.c_str()); }
            {
                std::string combined = modelPrefix + modelName;
                if (combined.size() > 4) {
                    std::string ext = combined.substr(combined.size() - 4);
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (ext == ".mdl" || ext == ".mdx") combined.resize(combined.size() - 4);
                }
                if (combined.empty() || combined == "war3mapImported\\")
                    combined = "war3mapImported\\dummy";
                pe.modelPath = combined + ".MDL";
            }

            pe.flags = 0;

            pe.speedTrackIndex        = extractTrackByName(ref, L"radius",    model, "KPES");
            pe.emissionRateTrackIndex = extractTrackByName(ref, L"PartEmit",  model, "KPEE");
            pe.gravityTrackIndex      = extractTrackByName(ref, L"Gravity",   model, "KPEG");
            pe.latitudeTrackIndex     = extractTrackByName(ref, L"latitude",  model, "KPLT");
            pe.longitudeTrackIndex    = extractTrackByName(ref, L"longitude", model, "KPLN");
            pe.lifespanTrackIndex     = extractTrackByName(ref, L"Life",      model, "KPEL");
        }

        pe.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);
        P1ELOG << "  Speed=" << pe.speed << " Emit=" << pe.emissionRate
               << " Lat=" << pe.latitude << "rad\n";
        P1EFLUSH;
        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
