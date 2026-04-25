#include "render_service_internal.h"
#include "render_service.h"  // RenderService::GetRenderOrder / GeosetPassesLod
#include "constants.h"
#include "sampler_asset_manager.h"
#include "bls/bls_frame.h"

#include <algorithm>
#include <cmath>

namespace WhiteoutDex::render_detail {

CollectedRenderables CollectSortedRenderables(
    const std::unordered_map<uint32_t, std::unique_ptr<Actor>>& models,
    int selectedLod) {
    CollectedRenderables out;
    // Reserve up-front so emplace_back never reallocates and the raw
    // pointers we hand to GeosetRef remain stable for the frame.
    out.views.reserve(models.size());
    out.refs.reserve(models.size() * 4);

    for (const auto& [h, miPtr] : models) {
        Actor* mi = miPtr.get();
        if (mi->parentVisibility <= 0.02f) continue;

        // Build the per-actor view. Pointers borrow from the actor's
        // RenderModel; valid only for the duration of this frame's render.
        RenderableView& view = out.views.emplace_back();
        view.geosets          = &mi->gpuGeosets;
        view.materials        = &mi->gpuMaterials;
        view.textures         = mi->textures.get();
        view.skinning         = &mi->skinning;
        view.activeLights     = &mi->activeLights;
        view.texAnimPalette   = &mi->texAnimPalette;
        view.worldTransform   = mi->worldTransform;
        view.parentVisibility = mi->parentVisibility;
        view.hasLods          = mi->hasLods;

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
            out.refs.push_back({&view, i, ro, geo.priorityPlane, geo.geosetId});
        }
    }

    std::sort(out.refs.begin(), out.refs.end(),
        [](const GeosetRef& a, const GeosetRef& b) {
            if (a.renderOrder   != b.renderOrder)   return a.renderOrder   < b.renderOrder;
            if (a.priorityPlane != b.priorityPlane) return a.priorityPlane < b.priorityPlane;
            return a.geosetId < b.geosetId;
        });

    return out;
}

bool BindSdMeshGeometry(gfx::IGFXCommandList* cmd,
                        const GPUGeoset&      geo) {
    // Slot 0 always carries the rest-pose Vertex stream -- the SD VS
    // blends against the bone palette when the FourBoneSkinning permute
    // is active, or passes through unchanged when numWeights=0.
    cmd->BindVertexBuffer(0, geo.unskinnedVb, sizeof(Vertex));
    cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);

    const bool hasBones = (geo.boneVb != gfx::BufferHandle::Invalid)
                       && (geo.bonePaletteCb != gfx::BufferHandle::Invalid);
    if (hasBones) {
        cmd->BindVertexBuffer(1, geo.boneVb, sizeof(BoneVertex));
        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 3, geo.bonePaletteCb);
    }
    return hasBones;
}

void BindLayerAlbedo(gfx::IGFXCommandList*            cmd,
                     TextureAssetManager::ModelScope* scope,
                     int                              textureId,
                     gfx::TextureHandle               defaultTex,
                     SamplerAssetManager&             samplers,
                     uint32_t                         slot) {
    // Default to "wrap on both axes" when no per-texture flags are
    // available — matches the previous kWrapFlagsMask sentinel.
    uint32_t wrapFlags = kSamplerWrapBitsMask;
    bool     hasTex    = false;
    if (textureId >= 0 && scope) {
        const gfx::TextureHandle h = scope->Get(textureId);
        if (h != gfx::TextureHandle::Invalid) {
            cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, h);
            wrapFlags = scope->WrapFlags(textureId);   // SamplerAssetManager masks
            hasTex    = true;
        }
    }
    if (!hasTex) cmd->BindShaderResource(gfx::ShaderStage::Pixel, slot, defaultTex);
    cmd->BindSampler(gfx::ShaderStage::Pixel, slot, samplers.WrapVariant(wrapFlags));
}

void WriteCbPerFrame(gfx::IGFXDevice*      gfx,
                     gfx::BufferHandle     cb,
                     const CbPerFrameDesc& d) {
    if (!gfx || cb == gfx::BufferHandle::Invalid) return;
    auto* p = static_cast<CBPerFrame*>(gfx->MapBuffer(cb));
    if (!p) return;
    p->world         = d.world.transpose();
    p->view          = d.view.transpose();
    p->projection    = d.projection.transpose();
    p->lightDir      = d.lightDir;
    p->lightColor    = d.lightColor;
    p->ambientColor  = d.ambientColor;
    p->extraParams   = d.extraParams;
    p->texAnimParams = d.texAnimParams;
    p->materialFlags = d.materialFlags;
    gfx->UnmapBuffer(cb);
}

Vector4f NormalizedLightDir4(const Vector4f& dir) {
    const float n = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (n <= 1e-6f) return {0.0f, 0.0f, 0.0f, 0.0f};
    return {dir.x / n, dir.y / n, dir.z / n, 0.0f};
}

void ApplyTexAnimPaletteToFrame(bls::FrameInputs&    frame,
                                const std::vector<RenderModel::TexAnimPaletteEntry>* palette,
                                int                  textureAnimationId) {
    if (palette &&
        textureAnimationId >= 0 &&
        textureAnimationId < static_cast<int>(palette->size())) {
        const auto& e = (*palette)[textureAnimationId];
        frame.texMtx0.rows[0] = { e.row0[0], e.row0[1], e.row0[2], e.row0[3] };
        frame.texMtx0.rows[1] = { e.row1[0], e.row1[1], e.row1[2], e.row1[3] };
    } else {
        frame.texMtx0 = bls::IdentityTexMtx();
    }
    frame.texMtx1 = bls::IdentityTexMtx();
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
