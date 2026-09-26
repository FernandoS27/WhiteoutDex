// MDLXExporter — Wc3Particles2 extractor implementation
// CHANGES: NeoDex BlizzPart2 compatibility via ClassID-based branching.
//
// Strategy: detect plugin type by ClassID, then:
//   - WhiteoutDex (WC3_PARTICLES2): ORIGINAL index-based PB access (zero regression risk)
//   - NeoDex (NEODEX_PARTICLES2):   name-based PB access with NeoDex param names
//
#include "wc3_particle2_extractor.h"
#include "../mdx_class_ids.h"
#include "visibility_track_helper.h"
#include "wc3_material_extractor.h"
#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <control.h>
#include <modstack.h>
#include <fstream>

static std::ofstream& pre2ExtLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_pre2_extract.log";
        log.open(path, std::ios::trunc);
        log << "=== PRE2 Extraction Log (dual-path) ===\n\n";
    }
    return log;
}
#define P2ELOG pre2ExtLog()
#define P2EFLUSH pre2ExtLog().flush()

constexpr ULONG WC3P2_TEXTURE_PATH_IID   = 0x7B3C8D10;
constexpr ULONG WC3P2_TEXTURE_PREFIX_IID = 0x7B3C8D11;

namespace {

// WhiteoutDex ParamIDs (UNCHANGED from original)
enum P2Params : ParamID {
    PB_COUNT=0, PB_SPEED=1, PB_VARIATION=2, PB_LIFE=3, PB_WIDTH=4,
    PB_HEIGHT=5, PB_INITVEL=6, PB_ANGLE_Y=7, PB_MIDTIME=8,
    PB_COLOR_START=9, PB_COLOR_MID=10, PB_COLOR_END=11,
    PB_ALPHA_START=12, PB_ALPHA_MID=13, PB_ALPHA_END=14,
    PB_SCALE_START=15, PB_SCALE_MID=16, PB_SCALE_END=17,
    PB_HEAD_LIFE_START=18, PB_HEAD_LIFE_REPEAT=19, PB_HEAD_LIFE_END=20,
    PB_HEAD_DECAY_START=21, PB_HEAD_DECAY_REPEAT=22, PB_HEAD_DECAY_END=23,
    PB_TAIL_LEN=24, PB_TYPE=25, PB_ROWS=26, PB_COLS=27,
    PB_TAIL_LIFE_START=28, PB_TAIL_LIFE_REPEAT=29, PB_TAIL_LIFE_END=30,
    PB_TAIL_DECAY_START=31, PB_TAIL_DECAY_REPEAT=32, PB_TAIL_DECAY_END=33,
    PB_SQUIRT=34, PB_BLEND=35, PB_GRAVITY=36, PB_SORT=37,
    PB_LINE_EMIT=38, PB_UNSHADED=39, PB_LATITUDE=40, PB_PRIORITY=41,
    PB_UNFOGGED=42, PB_MODELSPACE=43, PB_XYQUAD=44,
    PB_REPLACEABLE_ID=45, PB_LONGITUDE=46,
};

std::string wcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// Index-based track (WhiteoutDex — original code)
int32_t extractTrackByIndex(IParamBlock2* pb, ParamID pid,
                             ir::IRModel& model, const char* tag) {
    if (!pb) return -1;
    Control* ctrl = pb->GetControllerByID(pid, 0);
    if (!ctrl) return -1;
    int nk = ctrl->NumKeys();
    if (nk <= 0) return -1;

    ir::Track<float> track;
    ULONG cidA = ctrl->ClassID().PartA();
    ULONG cidB = ctrl->ClassID().PartB();
    P2ELOG << "    [" << tag << "] ctrl classID=(0x" << std::hex << cidA
           << ",0x" << cidB << std::dec << ") nkeys=" << nk << "\n";

    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
        IKeyControl* ikc = GetKeyControlInterface(ctrl);
        bool allStep = (ikc && mdx_extract::detail::bezierKeysAllStep(ikc));
        P2ELOG << "    [" << tag << "] → BEZIER detected, allStep="
               << (allStep ? "YES→None" : "NO→Bezier") << "\n";
        if (allStep)
            track.interpolation = ir::InterpolationType::None;
        else
            track.interpolation = ir::InterpolationType::Bezier;
    } else if (cidA == LININTERP_FLOAT_CLASS_ID) {
        track.interpolation = ir::InterpolationType::Linear;
    } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
        track.interpolation = ir::InterpolationType::Hermite;
    } else {
        track.interpolation = ir::InterpolationType::Linear;
        P2ELOG << "    [" << tag << "] → UNKNOWN ClassID, fallback Linear\n";
    }

    std::vector<TimeValue> times; std::vector<float> vals, inT, outT;
    bool hasTan = false;
    core::anim::readFloatKeys(ctrl, times, vals, inT, outT, hasTan);
    if (times.empty()) {
        for (int i = 0; i < nk; ++i) {
            TimeValue t = ctrl->GetKeyTime(i);
            float v = 0.0f; Interval iv = FOREVER;
            ctrl->GetValue(t, &v, iv);
            times.push_back(t); vals.push_back(v); inT.push_back(0); outT.push_back(0);
        }
    }
    for (size_t i = 0; i < times.size(); i++) {
        ir::Keyframe<float> k;
        k.time = times[i]; k.value = vals[i];
        k.inTangent = inT[i]; k.outTangent = outT[i]; k.hasTangents = hasTan;
        track.keys.push_back(k);
    }
    int32_t gs = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
    if (gs >= 0) track.globalSequenceIndex = gs;

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

// Name-based track (NeoDex)
int32_t extractTrackByName(ReferenceTarget* ref, const wchar_t* name,
                            ir::IRModel& model, const char* tag) {
    Control* ctrl = core::anim::getParamControllerDirect(ref, name);
    if (!ctrl) return -1;
    int nk = ctrl->NumKeys();
    if (nk <= 0) return -1;

    ir::Track<float> track;
    ULONG cidA = ctrl->ClassID().PartA();
    ULONG cidB = ctrl->ClassID().PartB();
    P2ELOG << "    [" << tag << "] ctrl classID=(0x" << std::hex << cidA
           << ",0x" << cidB << std::dec << ") nkeys=" << nk
           << " HYBRIDINTERP_FLOAT=0x" << std::hex << HYBRIDINTERP_FLOAT_CLASS_ID
           << " LININTERP_FLOAT=0x" << LININTERP_FLOAT_CLASS_ID << std::dec << "\n";

    if (cidA == HYBRIDINTERP_FLOAT_CLASS_ID) {
        IKeyControl* ikc = GetKeyControlInterface(ctrl);
        bool allStep = (ikc && mdx_extract::detail::bezierKeysAllStep(ikc));
        P2ELOG << "    [" << tag << "] → BEZIER detected, ikc=" << (ikc ? "yes" : "null")
               << " allStep=" << (allStep ? "YES→None" : "NO→Bezier") << "\n";
        if (allStep)
            track.interpolation = ir::InterpolationType::None;
        else
            track.interpolation = ir::InterpolationType::Bezier;
    } else if (cidA == LININTERP_FLOAT_CLASS_ID) {
        track.interpolation = ir::InterpolationType::Linear;
        P2ELOG << "    [" << tag << "] → LINEAR\n";
    } else if (cidA == TCBINTERP_FLOAT_CLASS_ID) {
        track.interpolation = ir::InterpolationType::Hermite;
        P2ELOG << "    [" << tag << "] → HERMITE\n";
    } else {
        track.interpolation = ir::InterpolationType::Linear;
        P2ELOG << "    [" << tag << "] → UNKNOWN ClassID, fallback to Linear\n";
    }

    std::vector<TimeValue> times; std::vector<float> vals, inT, outT;
    bool hasTan = false;
    core::anim::readFloatKeys(ctrl, times, vals, inT, outT, hasTan);
    if (times.empty()) {
        for (int i = 0; i < nk; ++i) {
            TimeValue t = ctrl->GetKeyTime(i);
            float v = 0.0f; Interval iv = FOREVER;
            ctrl->GetValue(t, &v, iv);
            times.push_back(t); vals.push_back(v); inT.push_back(0); outT.push_back(0);
        }
    }
    for (size_t i = 0; i < times.size(); i++) {
        ir::Keyframe<float> k;
        k.time = times[i]; k.value = vals[i];
        k.inTangent = inT[i]; k.outTangent = outT[i]; k.hasTangents = hasTan;
        track.keys.push_back(k);
    }
    int32_t gs = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
    if (gs >= 0) track.globalSequenceIndex = gs;

    int32_t idx = static_cast<int32_t>(model.floatTracks.size());
    model.floatTracks.push_back(std::move(track));
    return idx;
}

} // anon

namespace mdx_extract {

void extractParticles2(const std::vector<core::SceneNode>& nodes,
                       ir::IRModel& model,
                       core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Particles2") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        // Detect plugin type
        Object* baseObj = obj;
        while (baseObj && baseObj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            baseObj = static_cast<IDerivedObject*>(baseObj)->GetObjRef();
        Class_ID cid = baseObj ? baseObj->ClassID() : Class_ID(0,0);
        const bool isNeoDex = (cid == mdx_ids::NEODEX_PARTICLES2);
        // By-name reads look at the object's own param blocks, which a
        // modifier on the node would hide behind its derived object.
        if (isNeoDex) ref = baseObj;

        P2ELOG << "── PE2 " << (isNeoDex ? "NeoDex" : "WhiteoutDex")
               << " nodeIdx=" << sn.nodeIndex << " ──\n";

        TimeValue t = 0;
        ir::ParticleEmitter pe;
        pe.nodeIndex = sn.nodeIndex;
        pe.variant = 2;

        if (!isNeoDex) {
            // ═══════════════════════════════════════════════════
            // WHITEOUTDEX — original index-based code, UNCHANGED
            // ═══════════════════════════════════════════════════
            IParamBlock2* pb = PBR::findParamBlock(ref, 0);
            if (!pb) continue;

            pe.speed = PBR::readFloat(pb, PB_SPEED, t);
            pe.variation = PBR::readFloat(pb, PB_VARIATION, t);
            pe.emissionRate = PBR::readFloat(pb, PB_INITVEL, t);
            pe.lifespan = PBR::readFloat(pb, PB_LIFE, t);
            pe.gravity = PBR::readFloat(pb, PB_GRAVITY, t);
            pe.latitude = PBR::readFloat(pb, PB_ANGLE_Y, t);
            pe.width = PBR::readFloat(pb, PB_WIDTH, t);
            pe.length = PBR::readFloat(pb, PB_HEIGHT, t);
            pe.tailLength = PBR::readFloat(pb, PB_TAIL_LEN, t);
            pe.midTime = PBR::readFloat(pb, PB_MIDTIME, t, 0.5f);
            pe.filterMode = PBR::readInt(pb, PB_BLEND, t);
            pe.rows = PBR::readInt(pb, PB_ROWS, t, 1);
            pe.columns = PBR::readInt(pb, PB_COLS, t, 1);
            pe.headOrTail = PBR::readInt(pb, PB_TYPE, t);
            pe.replaceableId = PBR::readInt(pb, PB_REPLACEABLE_ID, t);
            pe.priorityPlane = PBR::readInt(pb, PB_PRIORITY, t);

            Point3 cS = PBR::readPoint3(pb, PB_COLOR_START, t);
            Point3 cM = PBR::readPoint3(pb, PB_COLOR_MID, t);
            Point3 cE = PBR::readPoint3(pb, PB_COLOR_END, t);
            pe.segmentColors[0] = Color(cS.x, cS.y, cS.z);
            pe.segmentColors[1] = Color(cM.x, cM.y, cM.z);
            pe.segmentColors[2] = Color(cE.x, cE.y, cE.z);
            pe.segmentAlpha[0] = float(PBR::readInt(pb, PB_ALPHA_START, t)) / 255.0f;
            pe.segmentAlpha[1] = float(PBR::readInt(pb, PB_ALPHA_MID, t)) / 255.0f;
            pe.segmentAlpha[2] = float(PBR::readInt(pb, PB_ALPHA_END, t)) / 255.0f;
            pe.segmentScale[0] = PBR::readFloat(pb, PB_SCALE_START, t);
            pe.segmentScale[1] = PBR::readFloat(pb, PB_SCALE_MID, t);
            pe.segmentScale[2] = PBR::readFloat(pb, PB_SCALE_END, t);

            pe.headInterval[0]      = PBR::readInt(pb, PB_HEAD_LIFE_START, t);
            pe.headInterval[1]      = PBR::readInt(pb, PB_HEAD_LIFE_REPEAT, t);
            pe.headInterval[2]      = PBR::readInt(pb, PB_HEAD_LIFE_END, t);
            pe.headDecayInterval[0] = PBR::readInt(pb, PB_HEAD_DECAY_START, t);
            pe.headDecayInterval[1] = PBR::readInt(pb, PB_HEAD_DECAY_REPEAT, t);
            pe.headDecayInterval[2] = PBR::readInt(pb, PB_HEAD_DECAY_END, t);
            pe.tailInterval[0]      = PBR::readInt(pb, PB_TAIL_LIFE_START, t);
            pe.tailInterval[1]      = PBR::readInt(pb, PB_TAIL_LIFE_REPEAT, t);
            pe.tailInterval[2]      = PBR::readInt(pb, PB_TAIL_LIFE_END, t);
            pe.tailDecayInterval[0] = PBR::readInt(pb, PB_TAIL_DECAY_START, t);
            pe.tailDecayInterval[1] = PBR::readInt(pb, PB_TAIL_DECAY_REPEAT, t);
            pe.tailDecayInterval[2] = PBR::readInt(pb, PB_TAIL_DECAY_END, t);

            uint32_t flags = 0;
            if (PBR::readInt(pb, PB_SORT, t))       flags |= 0x10000;
            if (PBR::readInt(pb, PB_UNSHADED, t))    flags |= 0x8000;
            if (PBR::readInt(pb, PB_LINE_EMIT, t))   flags |= 0x20000;
            if (PBR::readInt(pb, PB_UNFOGGED, t))    flags |= 0x40000;
            if (PBR::readInt(pb, PB_MODELSPACE, t))  flags |= 0x80000;
            if (PBR::readInt(pb, PB_XYQUAD, t))      flags |= 0x100000;
            if (PBR::readInt(pb, PB_SQUIRT, t))       flags |= 1;
            pe.flags = flags;

            pe.speedTrackIndex        = extractTrackByIndex(pb, PB_SPEED,    model, "KP2S");
            pe.variationTrackIndex    = extractTrackByIndex(pb, PB_VARIATION, model, "KP2R");
            pe.latitudeTrackIndex     = extractTrackByIndex(pb, PB_ANGLE_Y,  model, "KP2L");
            pe.gravityTrackIndex      = extractTrackByIndex(pb, PB_GRAVITY,  model, "KP2G");
            pe.emissionRateTrackIndex = extractTrackByIndex(pb, PB_INITVEL,  model, "KP2E");
            pe.lengthTrackIndex       = extractTrackByIndex(pb, PB_HEIGHT,   model, "KP2N");
            pe.widthTrackIndex        = extractTrackByIndex(pb, PB_WIDTH,    model, "KP2W");

            // Texture via IID
            std::string texName, texPrefix;
            auto* pp = static_cast<const MSTR*>(obj->GetInterface(WC3P2_TEXTURE_PATH_IID));
            if (pp && pp->Length() > 0) texName = wcharToUtf8(pp->data());
            auto* xp = static_cast<const MSTR*>(obj->GetInterface(WC3P2_TEXTURE_PREFIX_IID));
            if (xp && xp->Length() > 0) texPrefix = wcharToUtf8(xp->data());
            if (!texName.empty()) {
                size_t ls = texName.find_last_of("\\/");
                if (ls != std::string::npos) texName = texName.substr(ls + 1);
                std::string fp = texPrefix + texName;
                pe.textureIndex = findOrAddTexture(model, fp, pe.replaceableId, false, false);
            }

        } else {
            // ═══════════════════════════════════════════════════
            // NEODEX — name-based using BlizzPart2.ms param names
            // ═══════════════════════════════════════════════════
            PBR::readFloatByName(ref, L"speed",       t, pe.speed);
            PBR::readFloatByName(ref, L"variation",   t, pe.variation);
            PBR::readFloatByName(ref, L"PartsEmit",   t, pe.emissionRate);
            PBR::readFloatByName(ref, L"Life",        t, pe.lifespan);
            PBR::readFloatByName(ref, L"gravity",     t, pe.gravity);
            PBR::readFloatByName(ref, L"coneangle",   t, pe.latitude);
            PBR::readFloatByName(ref, L"width",       t, pe.width);
            PBR::readFloatByName(ref, L"length",      t, pe.length);
            PBR::readFloatByName(ref, L"taillength",  t, pe.tailLength);
            { float mt = 0.5f; PBR::readFloatByName(ref, L"midtime", t, mt); pe.midTime = mt; }

            // NeoDex dropdowns are 1-based; MDX expects 0-based.
            // NeoDex writer does the same subtraction (NeoDexSceneParser.ms).
            { int v=0; PBR::readIntByName(ref, L"filtermode",    t, v); pe.filterMode = (v > 0) ? v - 1 : 0; }
            { int v=1; PBR::readIntByName(ref, L"rows",          t, v); pe.rows = v; }
            { int v=1; PBR::readIntByName(ref, L"cols",          t, v); pe.columns = v; }
            { int v=0; PBR::readIntByName(ref, L"particletype",  t, v); pe.headOrTail = (v > 0) ? v - 1 : 0; }
            { int v=0; PBR::readIntByName(ref, L"rtexture",      t, v); pe.replaceableId = (v > 0) ? v - 1 : 0; }
            { int v=0; PBR::readIntByName(ref, L"priorityplane", t, v); pe.priorityPlane = v; }

            Color cS(1,1,1), cM(.5f,.5f,.5f), cE(0,0,0);
            PBR::readColorByName(ref, L"startcolor",  t, cS); pe.segmentColors[0] = cS;
            PBR::readColorByName(ref, L"middlecolor", t, cM); pe.segmentColors[1] = cM;
            PBR::readColorByName(ref, L"endcolor",    t, cE); pe.segmentColors[2] = cE;

            { float a=255; PBR::readFloatByName(ref, L"startalpha",  t, a); pe.segmentAlpha[0] = a / 255.0f; }
            { float a=128; PBR::readFloatByName(ref, L"middlealpha", t, a); pe.segmentAlpha[1] = a / 255.0f; }
            { float a=0;   PBR::readFloatByName(ref, L"endalpha",    t, a); pe.segmentAlpha[2] = a / 255.0f; }

            { float s=1; PBR::readFloatByName(ref, L"startscale",  t, s); pe.segmentScale[0] = s; }
            { float s=1; PBR::readFloatByName(ref, L"middlescale", t, s); pe.segmentScale[1] = s; }
            { float s=1; PBR::readFloatByName(ref, L"endscale",    t, s); pe.segmentScale[2] = s; }

            // MDX interval order is start, end, repeat — NeoDex writes
            // [startX, endX, repeatX] (NeoDexSceneParser, PE2 UV anims).
            { auto readInterval = [&](const wchar_t* start, const wchar_t* end,
                                      const wchar_t* repeat, std::array<int32_t, 3>& out) {
                  int v = 0; PBR::readIntByName(ref, start,  t, v); out[0] = v;
                  v = 0;     PBR::readIntByName(ref, end,    t, v); out[1] = v;
                  v = 0;     PBR::readIntByName(ref, repeat, t, v); out[2] = v;
              };
              readInterval(L"startLifespanHead", L"endLifespanHead", L"repeatLifespanHead", pe.headInterval);
              readInterval(L"startDecayHead",    L"endDecayHead",    L"repeatDecayHead",    pe.headDecayInterval);
              readInterval(L"startLifespanTail", L"endLifespanTail", L"repeatLifespanTail", pe.tailInterval);
              readInterval(L"startDecayTail",    L"endDecayTail",    L"repeatDecayTail",    pe.tailDecayInterval);
            }

            uint32_t flags = 0;
            BOOL b=FALSE;
            PBR::readBoolByName(ref, L"SortZ",         t, b); if(b) flags |= 0x10000; b=FALSE;
            PBR::readBoolByName(ref, L"Unshaded",      t, b); if(b) flags |= 0x8000;  b=FALSE;
            { int li=0; PBR::readIntByName(ref, L"LineEmitter", t, li); if(li>1) flags |= 0x20000; }
            PBR::readBoolByName(ref, L"Unfogged",      t, b); if(b) flags |= 0x40000; b=FALSE;
            PBR::readBoolByName(ref, L"ParticleSpace", t, b); if(b) flags |= 0x80000; b=FALSE;
            PBR::readBoolByName(ref, L"XYQuads",       t, b); if(b) flags |= 0x100000; b=FALSE;
            PBR::readBoolByName(ref, L"Squirt",        t, b); if(b) flags |= 1;
            pe.flags = flags;

            pe.speedTrackIndex     = extractTrackByName(ref, L"speed",     model, "KP2S");
            pe.variationTrackIndex = extractTrackByName(ref, L"variation", model, "KP2R");
            pe.latitudeTrackIndex  = extractTrackByName(ref, L"coneangle", model, "KP2L");
            pe.gravityTrackIndex   = extractTrackByName(ref, L"gravity",   model, "KP2G");
            pe.emissionRateTrackIndex = extractTrackByName(ref, L"PartsEmit", model, "KP2E");
            pe.lengthTrackIndex    = extractTrackByName(ref, L"length",    model, "KP2N");
            pe.widthTrackIndex     = extractTrackByName(ref, L"width",     model, "KP2W");

            // NeoDex texture: PB params
            std::string texName, texPrefix;
            { std::wstring w; PBR::readStringByName(ref, L"texture", t, w);
              if (!w.empty()) texName = wcharToUtf8(w.c_str()); }
            { std::wstring w; PBR::readStringByName(ref, L"path", t, w);
              if (!w.empty()) texPrefix = wcharToUtf8(w.c_str()); }
            // "texture" holds the bitmap file the importer loaded; it is also
            // the conversion source. Without a texture NeoDex writes an empty
            // path for Team Color / Glow and Textures\white.blp otherwise
            // (NeoDexSceneParser, PE2 textures).
            if (!texName.empty()) {
                const std::string disk = texName;
                size_t ls = texName.find_last_of("\\/");
                if (ls != std::string::npos) texName = texName.substr(ls + 1);
                std::string fp = texPrefix + texName;
                pe.textureIndex = findOrAddTexture(model, fp, pe.replaceableId, false, false, disk);
            } else if (pe.replaceableId > 0) {
                pe.textureIndex = findOrAddTexture(model, std::string(), pe.replaceableId, false, false);
            } else {
                pe.textureIndex = findOrAddTexture(model, "Textures\\white.blp", 0, false, false);
            }
        }

        pe.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);
        P2ELOG << "  Speed=" << pe.speed << " Emit=" << pe.emissionRate
               << " texIdx=" << pe.textureIndex << "\n";
        P2EFLUSH;
        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
