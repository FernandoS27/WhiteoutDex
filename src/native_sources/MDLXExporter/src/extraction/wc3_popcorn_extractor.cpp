// MDLXExporter — Wc3Popcorn (CornEmitter) extractor implementation
#include "wc3_popcorn_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

namespace mdx_extract {

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

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        TimeValue t = 0;
        ir::ParticleEmitter pe;
        pe.nodeIndex = sn.nodeIndex;
        pe.variant = 3; // CornEmitter marker

        PBR::readFloatByName(ref, L"LifeSpan", t, pe.lifespan);
        PBR::readFloatByName(ref, L"EmissionRate", t, pe.emissionRate);
        PBR::readFloatByName(ref, L"Speed", t, pe.speed);
        PBR::readIntByName(ref, L"ReplaceableId", t, pe.replaceableId);

        // Effect file path
        std::wstring pathW;
        if (PBR::readStringByName(ref, L"popcornPath", t, pathW) && !pathW.empty()) {
            int len = WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string u8(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), -1, u8.data(), len, nullptr, nullptr);
                pe.modelPath = std::move(u8);
            }
        }

        // Base color (stored in segmentColors[0] + segmentAlpha[0] for variant==3)
        Color baseColor(1, 1, 1);
        PBR::readColorByName(ref, L"baseColor", t, baseColor);
        pe.segmentColors[0] = baseColor;
        float alpha = 1.0f;
        PBR::readFloatByName(ref, L"alpha", t, alpha);
        pe.segmentAlpha[0] = alpha;

        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
