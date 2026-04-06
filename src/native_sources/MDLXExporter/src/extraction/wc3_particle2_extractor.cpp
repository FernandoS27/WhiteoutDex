// MDLXExporter — Wc3Particles2 extractor implementation
#include "wc3_particle2_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

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

        pe.speed = PBR::readFloat(pb, PB_SPEED, t);
        pe.variation = PBR::readFloat(pb, PB_VARIATION, t);
        pe.emissionRate = PBR::readFloat(pb, PB_INITVEL, t);
        pe.lifespan = PBR::readFloat(pb, PB_LIFE, t);
        pe.gravity = PBR::readFloat(pb, PB_GRAVITY, t);
        pe.latitude = static_cast<float>(PBR::readInt(pb, PB_LATITUDE, t));
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

        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
