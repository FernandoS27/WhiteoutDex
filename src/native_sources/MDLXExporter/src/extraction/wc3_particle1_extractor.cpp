// MDLXExporter — Wc3Particles1 extractor implementation
#include "wc3_particle1_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

// Interface ID matching Wc3Particles1's GetInterface(ULONG) handler
constexpr ULONG WC3P1_MODEL_PATH_IID = 0x7B3C8D01;

// ParamIDs from Wc3Particles1/Particles.h
namespace {
    enum P1Params : ParamID {
        P1_PB_COUNT = 0,
        P1_PB_SPEED = 1,
        P1_PB_EMISSION_RATE = 2,
        P1_PB_LIFE = 3,
        P1_PB_ACCELERATION = 4,
        P1_PB_LATITUDE = 5,
        P1_PB_LONGITUDE = 6,
        P1_PB_SCALE = 7,
    };
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

        pe.speed = PBR::readFloat(pb, P1_PB_SPEED, t);
        pe.emissionRate = PBR::readFloat(pb, P1_PB_EMISSION_RATE, t);
        pe.lifespan = PBR::readFloat(pb, P1_PB_LIFE, t);
        pe.gravity = PBR::readFloat(pb, P1_PB_ACCELERATION, t);
        pe.latitude = PBR::readFloat(pb, P1_PB_LATITUDE, t);
        pe.longitude = PBR::readFloat(pb, P1_PB_LONGITUDE, t);

        // Model path: stored as member on the Wc3Particles1 object, accessed via GetInterface()
        auto* pathPtr = static_cast<const MSTR*>(obj->GetInterface(WC3P1_MODEL_PATH_IID));
        if (pathPtr && pathPtr->Length() > 0)
        {
            int len = WideCharToMultiByte(CP_UTF8, 0, pathPtr->data(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string u8(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, pathPtr->data(), -1, u8.data(), len, nullptr, nullptr);
                pe.modelPath = std::move(u8);
            }
        }

        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
