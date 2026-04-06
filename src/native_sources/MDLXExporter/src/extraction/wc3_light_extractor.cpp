// MDLXExporter — Wc3Light extractor implementation
#include "wc3_light_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

namespace mdx_extract {

void extractLights(const std::vector<core::SceneNode>& nodes,
                   ir::IRModel& model,
                   core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Light") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        ir::Light light;
        light.nodeIndex = sn.nodeIndex;

        TimeValue t = 0;
        int lightType = 0;
        PBR::readIntByName(ref, L"LightType", t, lightType);
        switch (lightType) {
        case 1: light.type = ir::Light::Type::Omni; break;
        case 2: light.type = ir::Light::Type::Directional; break;
        case 3: light.type = ir::Light::Type::Ambient; break;
        default: light.type = ir::Light::Type::Omni; break;
        }

        PBR::readFloatByName(ref, L"DecayStart", t, light.attenuationStart);
        PBR::readFloatByName(ref, L"DecayEnd", t, light.attenuationEnd);

        Color mainColor(1.0f, 1.0f, 1.0f);
        PBR::readColorByName(ref, L"ShadowColor", t, mainColor);
        light.color = mainColor;

        float mainValue = 1.0f;
        PBR::readFloatByName(ref, L"ShadowValue", t, mainValue);
        light.intensity = mainValue;

        Color ambColor(0, 0, 0);
        PBR::readColorByName(ref, L"AmbColor", t, ambColor);
        light.ambientColor = ambColor;

        float ambValue = 0.0f;
        PBR::readFloatByName(ref, L"AmbValue", t, ambValue);
        light.ambientIntensity = ambValue;

        model.lights.push_back(std::move(light));
    }
}

} // namespace mdx_extract
