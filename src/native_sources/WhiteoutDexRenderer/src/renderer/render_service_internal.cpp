#include "render_service_internal.h"
#include "render_service.h"  // RenderService::GetRenderOrder / GeosetPassesLod
#include "constants.h"

#include <algorithm>

namespace WhiteoutDex::render_detail {

std::vector<GeosetRef> CollectSortedGeosetRefs(
    const std::unordered_map<uint32_t, std::unique_ptr<ModelInstance>>& models,
    int selectedLod) {
    std::vector<GeosetRef> refs;
    refs.reserve(64);

    for (const auto& [h, miPtr] : models) {
        ModelInstance* mi = miPtr.get();
        if (mi->parentVisibility <= 0.02f) continue;
        const int modelLod = mi->hasLods ? selectedLod : 0;
        const int geosetCount = static_cast<int>(mi->gpuGeosets.size());
        for (int i = 0; i < geosetCount; ++i) {
            const auto& geo = mi->gpuGeosets[i];
            if (!RenderService::GeosetPassesLod(geo.lod, modelLod)) continue;
            int ro = 1;
            const int matId = geo.materialId;
            if (matId >= 0 && matId < static_cast<int>(mi->gpuMaterials.size())
                && !mi->gpuMaterials[matId].cpu.layers.empty()) {
                ro = RenderService::GetRenderOrder(
                    mi->gpuMaterials[matId].cpu.layers[0].filterMode);
            }
            refs.push_back({mi, i, ro, geo.priorityPlane, geo.geosetId});
        }
    }

    std::sort(refs.begin(), refs.end(),
        [](const GeosetRef& a, const GeosetRef& b) {
            if (a.renderOrder   != b.renderOrder)   return a.renderOrder   < b.renderOrder;
            if (a.priorityPlane != b.priorityPlane) return a.priorityPlane < b.priorityPlane;
            return a.geosetId < b.geosetId;
        });

    return refs;
}

void BindSdMeshGeometry(gfx::IGFXCommandList* cmd, const GPUGeoset& geo) {
    cmd->BindVertexBuffer(0, geo.vb, sizeof(Vertex));
    cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);
}

void BindLayerAlbedo(gfx::IGFXCommandList*    cmd,
                     ModelInstance&           mi,
                     int                      textureId,
                     gfx::TextureHandle       defaultTex,
                     const gfx::SamplerHandle (&samplerWrap)[4],
                     uint32_t                 slot) {
    uint32_t wrapFlags = kWrapFlagsMask;
    bool     hasTex    = false;
    if (textureId >= 0) {
        auto it = mi.gpuTextures.find(textureId);
        if (it != mi.gpuTextures.end() && it->second.tex != gfx::TextureHandle::Invalid) {
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, it->second.tex);
            wrapFlags = it->second.wrapFlags & kWrapFlagsMask;
            hasTex    = true;
        }
    }
    if (!hasTex) cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, defaultTex);
    cmd->BindSampler(gfx::ShaderStage::Pixel, slot, samplerWrap[wrapFlags]);
}

UnpackedLayer UnpackLayer(const GPUMaterial* mat, int layerIndex) {
    UnpackedLayer out;
    if (!mat || layerIndex < 0 || layerIndex >= static_cast<int>(mat->cpu.layers.size())) {
        return out;
    }
    const auto& L = mat->cpu.layers[layerIndex];
    out.filterMode          = L.filterMode;
    out.flags               = L.flags;
    out.alpha               = L.alpha;
    out.textureId           = L.textureId;
    out.textureAnimationId  = L.textureAnimationId;
    out.shaderId            = L.shaderId;
    out.normalMapId         = L.normalMapId;
    out.ormMapId            = L.ormMapId;
    out.emissiveMapId       = L.emissiveMapId;
    out.teamColorMapId      = L.teamColorMapId;
    out.emissiveGain        = L.emissiveGain;
    out.fresnelOpacity      = L.fresnelOpacity;
    out.fresnelTeamColor    = L.fresnelTeamColor;
    out.fresnelColor        = L.fresnelColor;
    return out;
}

} // namespace WhiteoutDex::render_detail
