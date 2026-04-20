// MDLXExporter — Wc3Particles2 extractor implementation
#include "wc3_particle2_extractor.h"
#include "../mdx_class_ids.h"
#include "visibility_track_helper.h"
#include "wc3_material_extractor.h"  // findOrAddTexture
#include <scene/paramblock_reader.h>
#include <animation/global_sequence_helper.h>
#include <control.h>
#include <fstream>

// ── Dedicated PRE2 extraction log — separate from anim log ──
static std::ofstream& pre2ExtLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_pre2_extract.log";
        log.open(path, std::ios::trunc);
        log << "=== PRE2 Extraction Log ===\n";
        log << "Shows the Max→IR node mapping for each particle emitter.\n";
        log << "If parent references get mixed up later, this log tells us\n";
        log << "what the correct mapping was at extraction time.\n\n";
    }
    return log;
}
#define P2ELOG pre2ExtLog()
#define P2EFLUSH pre2ExtLog().flush()

// Interface IDs matching Wc3Particles2's GetInterface(ULONG) handler.
// Defined in Particles.h:
//   constexpr ULONG WC3P2_TEXTURE_PATH_IID   = 0x7B3C8D10;  // MSTR* m_particlePath
//   constexpr ULONG WC3P2_TEXTURE_PREFIX_IID = 0x7B3C8D11;  // MSTR* m_texturePrefix
constexpr ULONG WC3P2_TEXTURE_PATH_IID   = 0x7B3C8D10;
constexpr ULONG WC3P2_TEXTURE_PREFIX_IID = 0x7B3C8D11;

// ParamIDs from Wc3Particles2/Particles.h
namespace {
    enum P2Params : ParamID {
        PB_COUNT = 0,
        PB_SPEED = 1,
        PB_VARIATION = 2,
        PB_LIFE = 3,
        PB_WIDTH = 4,
        PB_HEIGHT = 5,
        PB_INITVEL = 6,    // emission rate
        PB_ANGLE_Y = 7,    // latitude
        PB_MIDTIME = 8,
        PB_COLOR_START = 9,
        PB_COLOR_MID = 10,
        PB_COLOR_END = 11,
        PB_ALPHA_START = 12,
        PB_ALPHA_MID = 13,
        PB_ALPHA_END = 14,
        PB_SCALE_START = 15,
        PB_SCALE_MID = 16,
        PB_SCALE_END = 17,
        PB_HEAD_LIFE_START = 18,
        PB_HEAD_LIFE_REPEAT = 19,
        PB_HEAD_LIFE_END = 20,
        PB_HEAD_DECAY_START = 21,
        PB_HEAD_DECAY_REPEAT = 22,
        PB_HEAD_DECAY_END = 23,
        PB_TAIL_LEN = 24,
        PB_TYPE = 25,
        PB_ROWS = 26,
        PB_COLS = 27,
        PB_TAIL_LIFE_START = 28,
        PB_TAIL_LIFE_REPEAT = 29,
        PB_TAIL_LIFE_END = 30,
        PB_TAIL_DECAY_START = 31,
        PB_TAIL_DECAY_REPEAT = 32,
        PB_TAIL_DECAY_END = 33,
        PB_SQUIRT = 34,
        PB_BLEND = 35,
        PB_GRAVITY = 36,
        PB_SORT = 37,
        PB_LINE_EMIT = 38,
        PB_UNSHADED = 39,
        PB_LATITUDE = 40,
        PB_PRIORITY = 41,
        PB_UNFOGGED = 42,
        PB_MODELSPACE = 43,
        PB_XYQUAD = 44,
        PB_REPLACEABLE_ID = 45,
        PB_LONGITUDE = 46,
    };

    // ── PE2 float-track extractor ─────────────────────────────
    //
    // Reads a float paramblock parameter's controller as an ir::FloatTrack
    // and registers it in model.floatTracks. Returns the track index, or
    // -1 if the parameter has no controller or no keys (caller then leaves
    // the corresponding *TrackIndex at -1 so the builder omits the chunk).
    //
    // Uses IParamBlock2::GetControllerByID() directly — faster and more
    // reliable than the SubAnim chain (same reason wc3_light_extractor
    // uses getParamControllerDirect via name lookup).
    //
    // Tangent-aware: Bezier/Hermite controllers get real in/out tangents
    // via readFloatKeys(); Linear controllers get zero tangents. Global
    // Sequence (ORT_CYCLE/ORT_LOOP) is detected and registered.
    //
    // Value pass-through: no unit conversion. For PE2 this is correct for
    // ALL tracked params (speed, variation, gravity, emissionRate, width,
    // length are scalars; latitude is degrees per the critical rule in
    // exporter_handoff_pe2.md — do NOT convert to radians).
    int32_t extractPE2FloatTrack(IParamBlock2* pb, ParamID pid,
                                  ir::IRModel& model, const char* tagName)
    {
        if (!pb) return -1;
        Control* ctrl = pb->GetControllerByID(pid, 0);
        if (!ctrl) {
            P2ELOG << "    [" << tagName << "] no controller (pid=" << pid << ")\n";
            return -1;
        }

        int numKeys = ctrl->NumKeys();
        if (numKeys <= 0) {
            P2ELOG << "    [" << tagName << "] controller has 0 keys — static only\n";
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

        // readFloatKeys handles Linear/Bezier/TCB. If it returned empty
        // (no IKeyControl interface), fall back to procedural sampling.
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

        track.keys.reserve(times.size());
        for (size_t i = 0; i < times.size(); i++) {
            ir::Keyframe<float> key;
            key.time = times[i];
            key.value = values[i];
            key.inTangent = inTans[i];
            key.outTangent = outTans[i];
            key.hasTangents = hasTangents;
            track.keys.push_back(key);
            P2ELOG << "      key[" << i << "] t=" << times[i]
                   << " v=" << values[i];
            if (hasTangents)
                P2ELOG << " in=" << inTans[i] << " out=" << outTans[i];
            P2ELOG << "\n";
        }

        int32_t gsIdx = core::anim::detectAndRegisterGlobalSeq(ctrl, model);
        if (gsIdx >= 0) {
            track.globalSequenceIndex = gsIdx;
            P2ELOG << "    [" << tagName << "] → GlobalSequence idx=" << gsIdx << "\n";
        }

        int32_t idx = static_cast<int32_t>(model.floatTracks.size());
        model.floatTracks.push_back(std::move(track));
        P2ELOG << "    [" << tagName << "] " << numKeys
               << " keys → floatTrack[" << idx << "] interp="
               << static_cast<int>(track.interpolation) << "\n";
        return idx;
    }
}

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

        IParamBlock2* pb = PBR::findParamBlock(ref, 0);
        if (!pb) continue;

        TimeValue t = 0;
        ir::ParticleEmitter pe;
        pe.nodeIndex = sn.nodeIndex;
        pe.variant = 2;

        // ── Extraction log: record what Max thinks the parent is ──
        {
            MSTR nname = sn.maxNode->GetName();
            std::string nnameA;
            int len = WideCharToMultiByte(CP_UTF8, 0, nname.data(), -1,
                                           nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                nnameA.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, nname.data(), -1,
                                     nnameA.data(), len, nullptr, nullptr);
            }
            INode* par = sn.maxNode->GetParentNode();
            INode* root = GetCOREInterface()->GetRootNode();
            bool hasMaxParent = (par && par != root && !par->IsRootNode());
            std::string parNameA = "<NONE>";
            if (hasMaxParent) {
                MSTR pname = par->GetName();
                int plen = WideCharToMultiByte(CP_UTF8, 0, pname.data(), -1,
                                                nullptr, 0, nullptr, nullptr);
                if (plen > 0) {
                    parNameA.resize(plen - 1);
                    WideCharToMultiByte(CP_UTF8, 0, pname.data(), -1,
                                         parNameA.data(), plen, nullptr, nullptr);
                }
            }
            // Store as ASCII so non-UTF8 name bytes are visible
            P2ELOG << "─────────────────────────────────────\n";
            P2ELOG << "PRE2 extracted: sn.nodeIndex=" << sn.nodeIndex
                   << " parentSceneNodeIdx=" << sn.parentNodeIndex << "\n";
            P2ELOG << "  maxNode name (UTF8): '" << nnameA << "'\n";
            P2ELOG << "  maxNode name bytes:";
            for (size_t i = 0; i < nnameA.size() && i < 64; ++i)
                P2ELOG << " " << std::hex << (int)(uint8_t)nnameA[i];
            P2ELOG << std::dec << "\n";
            P2ELOG << "  maxNode parent (UTF8): '" << parNameA << "'\n";
            P2ELOG << "  maxNode ptr: " << (void*)sn.maxNode
                   << " parent ptr: " << (void*)par << "\n";
            P2EFLUSH;
        }

        pe.speed = PBR::readFloat(pb, PB_SPEED, t);
        pe.variation = PBR::readFloat(pb, PB_VARIATION, t);
        pe.emissionRate = PBR::readFloat(pb, PB_INITVEL, t);
        pe.lifespan = PBR::readFloat(pb, PB_LIFE, t);
        pe.gravity = PBR::readFloat(pb, PB_GRAVITY, t);
        // PE2 latitude is DEGREES and comes from PB_ANGLE_Y (ConeAngle),
        // NOT from the deprecated PB_LATITUDE slot. Pass-through — do
        // NOT convert to radians. See exporter_handoff_pe2.md "Critical
        // Rule: PE2 Latitude is in DEGREES" for the full rationale.
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

        // Segment colors
        Point3 cStart = PBR::readPoint3(pb, PB_COLOR_START, t);
        Point3 cMid = PBR::readPoint3(pb, PB_COLOR_MID, t);
        Point3 cEnd = PBR::readPoint3(pb, PB_COLOR_END, t);
        pe.segmentColors[0] = Color(cStart.x, cStart.y, cStart.z);
        pe.segmentColors[1] = Color(cMid.x, cMid.y, cMid.z);
        pe.segmentColors[2] = Color(cEnd.x, cEnd.y, cEnd.z);

        // Segment alpha (0-255 int → 0-1 float for IR, but stored as float in segmentAlpha)
        pe.segmentAlpha[0] = static_cast<float>(PBR::readInt(pb, PB_ALPHA_START, t)) / 255.0f;
        pe.segmentAlpha[1] = static_cast<float>(PBR::readInt(pb, PB_ALPHA_MID, t)) / 255.0f;
        pe.segmentAlpha[2] = static_cast<float>(PBR::readInt(pb, PB_ALPHA_END, t)) / 255.0f;

        // Segment scale
        pe.segmentScale[0] = PBR::readFloat(pb, PB_SCALE_START, t);
        pe.segmentScale[1] = PBR::readFloat(pb, PB_SCALE_MID, t);
        pe.segmentScale[2] = PBR::readFloat(pb, PB_SCALE_END, t);

        // Head/tail UV anim intervals
        pe.headInterval[0] = PBR::readInt(pb, PB_HEAD_LIFE_START, t);
        pe.headInterval[1] = PBR::readInt(pb, PB_HEAD_LIFE_REPEAT, t);
        pe.headInterval[2] = PBR::readInt(pb, PB_HEAD_LIFE_END, t);
        pe.headDecayInterval[0] = PBR::readInt(pb, PB_HEAD_DECAY_START, t);
        pe.headDecayInterval[1] = PBR::readInt(pb, PB_HEAD_DECAY_REPEAT, t);
        pe.headDecayInterval[2] = PBR::readInt(pb, PB_HEAD_DECAY_END, t);
        pe.tailInterval[0] = PBR::readInt(pb, PB_TAIL_LIFE_START, t);
        pe.tailInterval[1] = PBR::readInt(pb, PB_TAIL_LIFE_REPEAT, t);
        pe.tailInterval[2] = PBR::readInt(pb, PB_TAIL_LIFE_END, t);
        pe.tailDecayInterval[0] = PBR::readInt(pb, PB_TAIL_DECAY_START, t);
        pe.tailDecayInterval[1] = PBR::readInt(pb, PB_TAIL_DECAY_REPEAT, t);
        pe.tailDecayInterval[2] = PBR::readInt(pb, PB_TAIL_DECAY_END, t);

        // Node flags from bools
        uint32_t flags = 0;
        if (PBR::readInt(pb, PB_SORT, t)) flags |= 0x10000;
        if (PBR::readInt(pb, PB_UNSHADED, t)) flags |= 0x8000;
        if (PBR::readInt(pb, PB_LINE_EMIT, t)) flags |= 0x20000;
        if (PBR::readInt(pb, PB_UNFOGGED, t)) flags |= 0x40000;
        if (PBR::readInt(pb, PB_MODELSPACE, t)) flags |= 0x80000;
        if (PBR::readInt(pb, PB_XYQUAD, t)) flags |= 0x100000;
        if (PBR::readInt(pb, PB_SQUIRT, t)) flags |= 1;
        pe.flags = flags;

        // ── Static-value dump ──
        //
        // Formatted to match the output of `dump_pe2_values_3_.ms` so that
        // post-export log + post-import MaxScript dump can be compared
        // line-by-line to verify byte-identical roundtrip.
        //
        // Alphas are re-read as int (0..255) here because that's the form
        // the builder ultimately writes to MDX (see mdx_model_builder.cpp:656
        // `u8 = float*255+0.5f`). Segment/Scale/Color values are shown in
        // the same format as the IR holds them.
        {
            Matrix3 ntm = sn.maxNode->GetNodeTM(t);
            Point3 np = ntm.GetTrans();
            int a0 = PBR::readInt(pb, PB_ALPHA_START, t);
            int a1 = PBR::readInt(pb, PB_ALPHA_MID, t);
            int a2 = PBR::readInt(pb, PB_ALPHA_END, t);

            P2ELOG << "  === PE2 Static Values ===\n";
            P2ELOG << "  node.pos     = (" << np.x << ", " << np.y << ", " << np.z << ")\n";
            P2ELOG << "  -- Animatable --\n";
            P2ELOG << "    Speed        = " << pe.speed << "\n";
            P2ELOG << "    Variation    = " << pe.variation << "\n";
            P2ELOG << "    EmissionRate = " << pe.emissionRate << "\n";
            P2ELOG << "    ConeAngle    = " << pe.latitude
                   << "  (latitude, degrees pass-through)\n";
            P2ELOG << "    Gravity      = " << pe.gravity << "\n";
            P2ELOG << "    Width        = " << pe.width << "\n";
            P2ELOG << "    Height       = " << pe.length
                   << "  (IR.length = MDX length = plugin Height)\n";
            P2ELOG << "  -- Static scalars --\n";
            P2ELOG << "    Life         = " << pe.lifespan << "\n";
            P2ELOG << "    TailLength   = " << pe.tailLength << "\n";
            P2ELOG << "    MidTime      = " << pe.midTime << "\n";
            P2ELOG << "  -- Enumerations --\n";
            P2ELOG << "    BlendMode     = " << pe.filterMode
                   << "  (0=Blend 1=Add 2=Mod 3=Mod2X 4=AlphaKey)\n";
            P2ELOG << "    ParticleType  = " << pe.headOrTail
                   << "  (0=Head 1=Tail 2=Both)\n";
            P2ELOG << "    TextureRows   = " << pe.rows << "\n";
            P2ELOG << "    TextureCols   = " << pe.columns << "\n";
            P2ELOG << "    ReplaceableId = " << pe.replaceableId << "\n";
            P2ELOG << "    PriorityPlane = " << pe.priorityPlane << "\n";
            P2ELOG << "  -- Segments --\n";
            P2ELOG << "    ColorStart = (" << pe.segmentColors[0].r << ", "
                   << pe.segmentColors[0].g << ", " << pe.segmentColors[0].b << ")\n";
            P2ELOG << "    ColorMid   = (" << pe.segmentColors[1].r << ", "
                   << pe.segmentColors[1].g << ", " << pe.segmentColors[1].b << ")\n";
            P2ELOG << "    ColorEnd   = (" << pe.segmentColors[2].r << ", "
                   << pe.segmentColors[2].g << ", " << pe.segmentColors[2].b << ")\n";
            P2ELOG << "    AlphaStart = " << a0 << "  (IR=" << pe.segmentAlpha[0] << ")\n";
            P2ELOG << "    AlphaMid   = " << a1 << "  (IR=" << pe.segmentAlpha[1] << ")\n";
            P2ELOG << "    AlphaEnd   = " << a2 << "  (IR=" << pe.segmentAlpha[2] << ")\n";
            P2ELOG << "    ScaleStart = " << pe.segmentScale[0] << "\n";
            P2ELOG << "    ScaleMid   = " << pe.segmentScale[1] << "\n";
            P2ELOG << "    ScaleEnd   = " << pe.segmentScale[2] << "\n";
            P2ELOG << "  -- UV intervals --\n";
            P2ELOG << "    HeadLife   = [" << pe.headInterval[0] << ", "
                   << pe.headInterval[1] << ", " << pe.headInterval[2] << "]\n";
            P2ELOG << "    HeadDecay  = [" << pe.headDecayInterval[0] << ", "
                   << pe.headDecayInterval[1] << ", " << pe.headDecayInterval[2] << "]\n";
            P2ELOG << "    TailLife   = [" << pe.tailInterval[0] << ", "
                   << pe.tailInterval[1] << ", " << pe.tailInterval[2] << "]\n";
            P2ELOG << "    TailDecay  = [" << pe.tailDecayInterval[0] << ", "
                   << pe.tailDecayInterval[1] << ", " << pe.tailDecayInterval[2] << "]\n";
            // Flags are encoded into pe.flags following the builder's expected
            // convention: bit 0 (squirt) goes to PE2-body, bits 15-20 go to
            // node.flags via mask 0x1F8000 in mdx_model_builder.cpp:629/645.
            P2ELOG << "  -- Flags (pe.flags=0x" << std::hex << flags << std::dec
                   << ") --\n";
            P2ELOG << "    Squirt="          << ((flags & 1)        ? "true" : "false")
                   << "  Sort="              << ((flags & 0x10000)  ? "true" : "false")
                   << "  LineEmitter="       << ((flags & 0x20000)  ? "true" : "false") << "\n";
            P2ELOG << "    Unshaded="        << ((flags & 0x8000)   ? "true" : "false")
                   << "  Unfogged="          << ((flags & 0x40000)  ? "true" : "false")
                   << "  ModelSpace="        << ((flags & 0x80000)  ? "true" : "false")
                   << "  XYQuad="            << ((flags & 0x100000) ? "true" : "false") << "\n";
            P2EFLUSH;
        }

        // Visibility animation from the INode's visibility controller
        // (handles on_off_float correctly via Animatable::NumKeys fallback).
        pe.visibilityTrackIndex = extractVisibilityTrack(sn.maxNode, model);

        // ── Animation tracks for animatable PE2 params ────────────
        //
        // Each corresponds to one of these MDX chunks (see handoff_pe2.md):
        //   KP2S ← Speed controller (scalar)
        //   KP2R ← Variation controller (scalar)
        //   KP2L ← ConeAngle (= PB_ANGLE_Y) controller — DEGREES, pass-through
        //   KP2G ← Gravity controller (scalar)
        //   KP2E ← EmissionRate (= PB_INITVEL) controller (particles/sec)
        //   KP2N ← Height (= PB_HEIGHT) controller — MDX field is "length"
        //   KP2W ← Width controller (world units)
        //
        // extractPE2FloatTrack returns -1 for static params; the builder
        // omits the chunk when the index is -1. No clamping applied —
        // spinner ranges on PB_ANGLE_Y (0..180) and PB_GRAVITY (0..100)
        // affect UI but not the stored/animated values.
        P2ELOG << "  PE2 Tracks:\n";
        pe.speedTrackIndex        = extractPE2FloatTrack(pb, PB_SPEED,    model, "KP2S");
        pe.variationTrackIndex    = extractPE2FloatTrack(pb, PB_VARIATION, model, "KP2R");
        pe.latitudeTrackIndex     = extractPE2FloatTrack(pb, PB_ANGLE_Y,  model, "KP2L");
        pe.gravityTrackIndex      = extractPE2FloatTrack(pb, PB_GRAVITY,  model, "KP2G");
        pe.emissionRateTrackIndex = extractPE2FloatTrack(pb, PB_INITVEL,  model, "KP2E");
        pe.lengthTrackIndex       = extractPE2FloatTrack(pb, PB_HEIGHT,   model, "KP2N");
        pe.widthTrackIndex        = extractPE2FloatTrack(pb, PB_WIDTH,    model, "KP2W");
        P2EFLUSH;

        // --- Particle texture path ---
        // Texture filename and path prefix are stored as MSTR members on
        // the Wc3Particles2 object (not in the ParamBlock), exposed via
        // GetInterface(ULONG) with IIDs defined in Particles.h.
        // The filename is stored bare (e.g. "SFX_Particle 1.blp") and the
        // prefix is stored separately (e.g. "Textures\"). We concatenate
        // them into the MDX texture path here.
        std::string texName;
        std::string texPrefix;
        {
            auto* pathPtr = static_cast<const MSTR*>(
                obj->GetInterface(WC3P2_TEXTURE_PATH_IID));
            if (pathPtr && pathPtr->Length() > 0) {
                int len = WideCharToMultiByte(CP_UTF8, 0, pathPtr->data(), -1,
                                              nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    texName.resize(static_cast<size_t>(len - 1));
                    WideCharToMultiByte(CP_UTF8, 0, pathPtr->data(), -1,
                                        texName.data(), len, nullptr, nullptr);
                }
            }
            auto* prefixPtr = static_cast<const MSTR*>(
                obj->GetInterface(WC3P2_TEXTURE_PREFIX_IID));
            if (prefixPtr && prefixPtr->Length() > 0) {
                int len = WideCharToMultiByte(CP_UTF8, 0, prefixPtr->data(), -1,
                                              nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    texPrefix.resize(static_cast<size_t>(len - 1));
                    WideCharToMultiByte(CP_UTF8, 0, prefixPtr->data(), -1,
                                        texPrefix.data(), len, nullptr, nullptr);
                }
            }
        }

        // Strip any path separator from texName (defensive — Particles.cpp's
        // import code already does this, but an older .max file or a user
        // who pasted a full path directly into the edit could bypass that).
        if (!texName.empty()) {
            size_t lastSep = std::string::npos;
            for (size_t i = texName.size(); i > 0; --i) {
                char c = texName[i - 1];
                if (c == '\\' || c == '/') { lastSep = i - 1; break; }
            }
            if (lastSep != std::string::npos)
                texName = texName.substr(lastSep + 1);
        }

        // Only add a TEXS entry if a filename was set.
        if (!texName.empty()) {
            std::string fullPath;
            // If the name already contains a separator (shouldn't after the
            // strip above, but be safe), use it verbatim. Otherwise prepend
            // the prefix.
            if (texName.find('\\') != std::string::npos ||
                texName.find('/')  != std::string::npos)
            {
                fullPath = texName;
            } else {
                fullPath = texPrefix + texName;
            }
            // PB_REPLACEABLE_ID is already read into pe.replaceableId above.
            // The TEXS entry needs the same replaceableId so the game looks
            // up the right replaceable slot (TeamColor=1, TeamGlow=2, etc.).
            // Particle textures never tile — they map to single quads — so
            // wrapU/wrapV are always false (matching NeoDex / WC3 exports).
            pe.textureIndex = findOrAddTexture(model, fullPath,
                                               pe.replaceableId,
                                               /*wrapU=*/false,
                                               /*wrapV=*/false);
        }

        // Log the resolved texture info. Useful when comparing against
        // the source MDX — the fullPath here must equal the original
        // mdx::Texture::filePath for byte-identical roundtrip.
        P2ELOG << "  -- Texture --\n";
        P2ELOG << "    prefix       = '" << texPrefix << "'\n";
        P2ELOG << "    filename     = '" << texName << "'\n";
        if (!texName.empty()) {
            std::string fullPath = (texName.find('\\') != std::string::npos ||
                                    texName.find('/')  != std::string::npos)
                                       ? texName
                                       : (texPrefix + texName);
            P2ELOG << "    fullPath     = '" << fullPath << "'\n";
            P2ELOG << "    textureIndex = " << pe.textureIndex << "\n";
        } else {
            P2ELOG << "    (no texture — pe.textureIndex=-1, TEXS entry skipped)\n";
        }
        P2EFLUSH;

        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
