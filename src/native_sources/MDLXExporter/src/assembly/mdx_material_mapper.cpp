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
    mat.flags = static_cast<wdx::Material::Flag>(irMat.flags);
    mat.shader = irMat.shaderName;
    if (mat.shader.empty() && version > 800 && version < 1100)
        mat.shader = "Shader_HD_DefaultUnit";

    MDX_LOG(_T("  MaterialMapper: priority=%d flags=0x%X shader=\"%S\" layers=%d\n"),
            mat.priorityPlane, static_cast<uint32_t>(mat.flags), mat.shader.c_str(), (int)irMat.layers.size());

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
    // Not version-gated: the 3.0.0 client reads shadingFlags as one u32 at
    // every version, and older clients never test these bits.
    if (irLayer.backFacesForShadows) flags = flags | Layer::ShadingFlag::BackFacesForShadows;
    if (irLayer.ambientOcclusion)    flags = flags | Layer::ShadingFlag::AmbientOcclusion;
    layer.shadingFlags = flags;

    layer.alpha = irLayer.alpha;
    layer.coordId = static_cast<uint32_t>(irLayer.uvSetIndex);
    layer.textureAnimationId = (irLayer.textureAnimationIndex >= 0)
        ? static_cast<uint32_t>(irLayer.textureAnimationIndex) : 0xFFFFFFFF;

    // From v1100 the textures live in the sub-texture list and Blizzard's
    // files leave the old textureId at 0.
    if (version < 1100) {
        for (auto& ref : irLayer.textureRefs) {
            if (ref.slot == ir::TextureSlot::Diffuse && ref.textureIndex >= 0) {
                layer.textureId = static_cast<uint32_t>(ref.textureIndex);
                break;
            }
        }
    }

    // The writer gates each of these on the version that introduced it.
    layer.emissiveGain = irLayer.emissiveGain;
    layer.fresnelOpacity = irLayer.fresnelOpacity;
    layer.fresnelTeamColor = irLayer.fresnelTeamColor;
    layer.fresnelColor = {irLayer.fresnelColor.x, irLayer.fresnelColor.y,
                          irLayer.fresnelColor.z};

    if (version >= 1100) {
        bool hasPbrSlot = false;
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
            if (sub.slot != Layer::SlotType::DiffuseMap &&
                sub.slot != Layer::SlotType::TeamColor)
                hasPbrSlot = true;
            layer.subTextures.push_back(sub);
        }

        // The shader the material names wins. Without one (a Standard
        // material, a NeoDex material) a layer is HD only if it carries a
        // PBR map: every layer has a diffuse sub-texture, so "has
        // sub-textures" would turn every SD layer into HD.
        using ShaderType = Layer::ShaderType;
        layer.shader = (irLayer.shaderType >= 0)
            ? static_cast<ShaderType>(irLayer.shaderType)
            : (hasPbrSlot ? ShaderType::HD : ShaderType::SD);
        layer.is_hd = (layer.shader == ShaderType::HD ||
                       layer.shader == ShaderType::Crystal);
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
                : Track<whiteout::f32>::kNoGlobalSequence;
            layer.alphaTracks.keyCount = irTrack.keys.size();

            bool hasTangents = (irTrack.interpolation == ir::InterpolationType::Hermite ||
                                irTrack.interpolation == ir::InterpolationType::Bezier);

            layer.alphaTracks.timestamps.resize(irTrack.keys.size());
            if (hasTangents) {
                using TK = Track<whiteout::f32>::TangentKey;
                layer.alphaTracks.keys_data.resize(irTrack.keys.size() * sizeof(TK) / sizeof(whiteout::f32));
                auto tangentKeys = layer.alphaTracks.tangentKeys();
                for (size_t k = 0; k < irTrack.keys.size(); k++) {
                    layer.alphaTracks.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
                    tangentKeys[k].value = irTrack.keys[k].value;
                    tangentKeys[k].inTan = irTrack.keys[k].inTangent;
                    tangentKeys[k].outTan = irTrack.keys[k].outTangent;
                }
            } else {
                layer.alphaTracks.keys_data.resize(irTrack.keys.size());
                for (size_t k = 0; k < irTrack.keys.size(); k++) {
                    layer.alphaTracks.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
                    layer.alphaTracks.keys_data[k] = irTrack.keys[k].value;
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
            Track<whiteout::u32> track;
            track.isUsed = true;
            track.interpolationType = static_cast<InterpolationType>(
                static_cast<int>(irTrack.interpolation));
            track.globalSequenceId = (irTrack.globalSequenceIndex >= 0)
                ? static_cast<uint32_t>(irTrack.globalSequenceIndex)
                : Track<whiteout::u32>::kNoGlobalSequence;
            track.keyCount = irTrack.keys.size();

            track.timestamps.resize(irTrack.keys.size());
            track.keys_data.resize(irTrack.keys.size());
            for (size_t k = 0; k < irTrack.keys.size(); k++) {
                track.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
                track.keys_data[k] = static_cast<whiteout::u32>(irTrack.keys[k].value);
            }

            // From v1100 the writer emits KMTF only from a sub-texture, so a
            // flipbook on the layer itself would be dropped. The flipbook
            // swaps the diffuse texture.
            Layer::SubTexture* diffuseSub = nullptr;
            for (auto& sub : layer.subTextures) {
                if (sub.slot == Layer::SlotType::DiffuseMap) { diffuseSub = &sub; break; }
            }
            if (version >= 1100 && diffuseSub)
                diffuseSub->tracks = std::move(track);
            else
                layer.textureIdTracks = std::move(track);

            MDX_LOG(_T("      texId animated: %d keys\n"), (int)irTrack.keys.size());
        }
    }

    // KMTE / KFC3 / KFCA / KFTC. The writer only emits them from the version
    // that has the matching static field.
    layer.emissiveGainTracks = floatTrack(model, irLayer.emissiveGainTrackIndex);
    layer.fresnelAlphaTracks = floatTrack(model, irLayer.fresnelAlphaTrackIndex);
    layer.fresnelTeamColorTracks = floatTrack(model, irLayer.fresnelTeamColorTrackIndex);
    if (irLayer.fresnelColorTrackIndex >= 0 &&
        irLayer.fresnelColorTrackIndex < static_cast<int32_t>(model.colorTracks.size()))
    {
        const auto& irTrack = model.colorTracks[irLayer.fresnelColorTrackIndex];
        if (!irTrack.empty()) {
            auto& out = layer.fresnelColorTracks;
            out.isUsed = true;
            out.interpolationType = static_cast<InterpolationType>(
                static_cast<int>(irTrack.interpolation));
            out.globalSequenceId = (irTrack.globalSequenceIndex >= 0)
                ? static_cast<uint32_t>(irTrack.globalSequenceIndex)
                : Track<whiteout::Vector3f>::kNoGlobalSequence;
            out.keyCount = irTrack.keys.size();
            out.timestamps.resize(irTrack.keys.size());
            const bool hasTangents = (irTrack.interpolation == ir::InterpolationType::Hermite ||
                                      irTrack.interpolation == ir::InterpolationType::Bezier);
            auto rgb = [](const Color& c) { return whiteout::Vector3f{c.r, c.g, c.b}; };
            if (hasTangents) {
                using TK = Track<whiteout::Vector3f>::TangentKey;
                out.keys_data.resize(irTrack.keys.size() * sizeof(TK) / sizeof(whiteout::Vector3f));
                auto keys = out.tangentKeys();
                for (size_t k = 0; k < irTrack.keys.size(); k++) {
                    out.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
                    keys[k].value = rgb(irTrack.keys[k].value);
                    keys[k].inTan = rgb(irTrack.keys[k].inTangent);
                    keys[k].outTan = rgb(irTrack.keys[k].outTangent);
                }
            } else {
                out.keys_data.resize(irTrack.keys.size());
                for (size_t k = 0; k < irTrack.keys.size(); k++) {
                    out.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
                    out.keys_data[k] = rgb(irTrack.keys[k].value);
                }
            }
        }
    }

    return layer;
}

Track<whiteout::f32> MdxMaterialMapper::floatTrack(const ir::IRModel& model, int32_t index)
{
    Track<whiteout::f32> out;
    if (index < 0 || index >= static_cast<int32_t>(model.floatTracks.size()))
        return out;
    const auto& irTrack = model.floatTracks[index];
    if (irTrack.empty())
        return out;

    out.isUsed = true;
    out.interpolationType = static_cast<InterpolationType>(
        static_cast<int>(irTrack.interpolation));
    out.globalSequenceId = (irTrack.globalSequenceIndex >= 0)
        ? static_cast<uint32_t>(irTrack.globalSequenceIndex)
        : Track<whiteout::f32>::kNoGlobalSequence;
    out.keyCount = irTrack.keys.size();
    out.timestamps.resize(irTrack.keys.size());

    const bool hasTangents = (irTrack.interpolation == ir::InterpolationType::Hermite ||
                              irTrack.interpolation == ir::InterpolationType::Bezier);
    if (hasTangents) {
        using TK = Track<whiteout::f32>::TangentKey;
        out.keys_data.resize(irTrack.keys.size() * sizeof(TK) / sizeof(whiteout::f32));
        auto keys = out.tangentKeys();
        for (size_t k = 0; k < irTrack.keys.size(); k++) {
            out.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
            keys[k].value = irTrack.keys[k].value;
            keys[k].inTan = irTrack.keys[k].inTangent;
            keys[k].outTan = irTrack.keys[k].outTangent;
        }
    } else {
        out.keys_data.resize(irTrack.keys.size());
        for (size_t k = 0; k < irTrack.keys.size(); k++) {
            out.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
            out.keys_data[k] = irTrack.keys[k].value;
        }
    }
    return out;
}
