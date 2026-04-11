// MDLXExporter — MDX material mapper implementation
// CHANGES: Added debug logging for material/layer conversion
#include "mdx_material_mapper.h"

#include <max.h>
#ifndef MDX_DEBUG_PRINT
#define MDX_DEBUG_PRINT 1
#endif
#if MDX_DEBUG_PRINT
  #define MDX_LOG(...) DebugPrint(__VA_ARGS__)
#else
  #define MDX_LOG(...) ((void)0)
#endif

namespace wdx = whiteout::mdx;
using wdx::Layer;
using wdx::Track;
using wdx::InterpolationType;

wdx::Material MdxMaterialMapper::map(const ir::Material& irMat,
                                      const ir::IRModel& model,
                                      uint32_t version)
{
    wdx::Material mat;
    mat.priorityPlane = irMat.priorityPlane;
    mat.flags = irMat.flags;
    mat.shader = irMat.shaderName;
    if (mat.shader.empty() && version > 800 && version < 1100)
        mat.shader = "Shader_HD_DefaultUnit";

    MDX_LOG(_T("  MaterialMapper: priority=%d flags=0x%X shader=\"%S\" layers=%d\n"),
            mat.priorityPlane, mat.flags, mat.shader.c_str(), (int)irMat.layers.size());

    for (size_t i = 0; i < irMat.layers.size(); i++) {
        auto layer = mapLayer(irMat.layers[i], model, version);
        MDX_LOG(_T("    layer[%d] filter=%d alpha=%.2f texId=%u coordId=%u hd=%s\n"),
                (int)i, (int)layer.filterMode, layer.alpha,
                layer.textureId, layer.coordId,
                layer.is_hd ? _T("yes") : _T("no"));
        mat.layers.push_back(std::move(layer));
    }

    return mat;
}

Layer MdxMaterialMapper::mapLayer(const ir::MaterialLayer& irLayer,
                                   const ir::IRModel& model,
                                   uint32_t version)
{
    Layer layer;

    switch (irLayer.blendMode) {
    case ir::BlendMode::None:        layer.filterMode = Layer::FilterMode::None; break;
    case ir::BlendMode::Transparent: layer.filterMode = Layer::FilterMode::Transparent; break;
    case ir::BlendMode::Blend:       layer.filterMode = Layer::FilterMode::Blend; break;
    case ir::BlendMode::Additive:   layer.filterMode = Layer::FilterMode::Additive; break;
    case ir::BlendMode::AddAlpha:   layer.filterMode = Layer::FilterMode::AddAlpha; break;
    case ir::BlendMode::Modulate:   layer.filterMode = Layer::FilterMode::Modulate; break;
    case ir::BlendMode::Modulate2x: layer.filterMode = Layer::FilterMode::Modulate2x; break;
    }

    Layer::ShadingFlag flags = Layer::ShadingFlag::None;
    if (irLayer.unshaded)     flags = flags | Layer::ShadingFlag::Unshaded;
    if (irLayer.sphereEnvMap) flags = flags | Layer::ShadingFlag::SphereEnvMap;
    if (irLayer.twoSided)     flags = flags | Layer::ShadingFlag::TwoSided;
    if (irLayer.unfogged)     flags = flags | Layer::ShadingFlag::Unfogged;
    if (irLayer.noDepthTest)  flags = flags | Layer::ShadingFlag::NoDepthTest;
    if (irLayer.noDepthWrite) flags = flags | Layer::ShadingFlag::NoDepthSet;
    layer.shadingFlags = flags;

    layer.alpha = irLayer.alpha;
    layer.coordId = static_cast<uint32_t>(irLayer.uvSetIndex);
    layer.textureAnimationId = (irLayer.textureAnimationIndex >= 0)
        ? static_cast<uint32_t>(irLayer.textureAnimationIndex) : 0xFFFFFFFF;

    for (auto& ref : irLayer.textureRefs) {
        if (ref.slot == ir::TextureSlot::Diffuse && ref.textureIndex >= 0) {
            layer.textureId = static_cast<uint32_t>(ref.textureIndex);
            break;
        }
    }

    if (version >= 1200) {
        layer.emissiveGain = irLayer.emissiveGain;
        layer.fresnelOpacity = irLayer.fresnelOpacity;
        layer.fresnelTeamColor = irLayer.fresnelTeamColor;
        layer.fresnelColor = {irLayer.fresnelColor.x, irLayer.fresnelColor.y,
                              irLayer.fresnelColor.z};

        for (auto& ref : irLayer.textureRefs) {
            if (ref.textureIndex < 0) continue;
            Layer::SubTexture sub;
            sub.textureId = static_cast<uint32_t>(ref.textureIndex);
            switch (ref.slot) {
            case ir::TextureSlot::Diffuse:    sub.slot = Layer::SlotType::DiffuseMap; break;
            case ir::TextureSlot::Normal:     sub.slot = Layer::SlotType::NormalMap; break;
            case ir::TextureSlot::ORM:        sub.slot = Layer::SlotType::ORMMap; break;
            case ir::TextureSlot::Emissive:   sub.slot = Layer::SlotType::EmissiveMap; break;
            case ir::TextureSlot::TeamColor:  sub.slot = Layer::SlotType::TeamColor; break;
            case ir::TextureSlot::Environment: sub.slot = Layer::SlotType::EnvironmentMap; break;
            default: sub.slot = Layer::SlotType::DiffuseMap; break;
            }
            layer.subTextures.push_back(sub);
        }

        if (!layer.subTextures.empty())
            layer.is_hd = true;
    }

    // Animated alpha track
    if (irLayer.alphaTrackIndex >= 0 &&
        irLayer.alphaTrackIndex < static_cast<int32_t>(model.floatTracks.size()))
    {
        auto& irTrack = model.floatTracks[irLayer.alphaTrackIndex];
        if (!irTrack.empty()) {
            layer.alphaTracks.isUsed = true;
            layer.alphaTracks.interpolationType = static_cast<InterpolationType>(
                static_cast<int>(irTrack.interpolation));
            layer.alphaTracks.globalSequenceId = (irTrack.globalSequenceIndex >= 0)
                ? static_cast<uint32_t>(irTrack.globalSequenceIndex)
                : 0xFFFFFFFF;
            layer.alphaTracks.keyCount = irTrack.keys.size();

            bool hasTangents = (irTrack.interpolation == ir::InterpolationType::Hermite ||
                                irTrack.interpolation == ir::InterpolationType::Bezier);

            if (hasTangents) {
                using TK = Track<whiteout::f32>::TangentKey;
                layer.alphaTracks.keys_data.resize(irTrack.keys.size() * sizeof(TK));
                auto* tangentKeys = reinterpret_cast<TK*>(layer.alphaTracks.keys_data.data());
                for (size_t k = 0; k < irTrack.keys.size(); k++) {
                    tangentKeys[k].frame = mdx_transform::ticksToMs(irTrack.keys[k].time);
                    tangentKeys[k].value = irTrack.keys[k].value;
                    tangentKeys[k].inTan = irTrack.keys[k].inTangent;
                    tangentKeys[k].outTan = irTrack.keys[k].outTangent;
                }
            } else {
                using K = Track<whiteout::f32>::Key;
                layer.alphaTracks.keys_data.resize(irTrack.keys.size() * sizeof(K));
                auto* keys = reinterpret_cast<K*>(layer.alphaTracks.keys_data.data());
                for (size_t k = 0; k < irTrack.keys.size(); k++) {
                    keys[k].frame = mdx_transform::ticksToMs(irTrack.keys[k].time);
                    keys[k].value = irTrack.keys[k].value;
                }
            }

            MDX_LOG(_T("      alpha animated: %d keys\n"), (int)irTrack.keys.size());
        }
    }

    // Animated texture ID track
    if (irLayer.textureIdTrackIndex >= 0 &&
        irLayer.textureIdTrackIndex < static_cast<int32_t>(model.intTracks.size()))
    {
        auto& irTrack = model.intTracks[irLayer.textureIdTrackIndex];
        if (!irTrack.empty()) {
            layer.textureIdTracks.isUsed = true;
            layer.textureIdTracks.interpolationType = static_cast<InterpolationType>(
                static_cast<int>(irTrack.interpolation));
            layer.textureIdTracks.globalSequenceId = (irTrack.globalSequenceIndex >= 0)
                ? static_cast<uint32_t>(irTrack.globalSequenceIndex)
                : 0xFFFFFFFF;
            layer.textureIdTracks.keyCount = irTrack.keys.size();

            using K = Track<whiteout::u32>::Key;
            layer.textureIdTracks.keys_data.resize(irTrack.keys.size() * sizeof(K));
            auto* keys = reinterpret_cast<K*>(layer.textureIdTracks.keys_data.data());
            for (size_t k = 0; k < irTrack.keys.size(); k++) {
                keys[k].frame = mdx_transform::ticksToMs(irTrack.keys[k].time);
                keys[k].value = static_cast<whiteout::u32>(irTrack.keys[k].value);
            }

            MDX_LOG(_T("      texId animated: %d keys\n"), (int)irTrack.keys.size());
        }
    }

    return layer;
}
