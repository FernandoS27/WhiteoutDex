// ============================================================================
// WhiteoutDex Renderer — RenderModel (impl)
//
// Per-frame state-application methods. Moved out of RenderService in Phase 5
// v2 once the data they touch (gpuGeosets / gpuMaterials / texAnimPalette /
// matTexAnim / activeLights / ribbons / pe1) was already living here.
// ============================================================================

#include "render_model.h"

#include <algorithm>

namespace WhiteoutDex {

void RenderModel::ApplyGeosetStates(const FrameState& state) {
    for (int i = 0; i < (int)state.geosetTransforms.size() && i < (int)gpuGeosets.size(); i++)
        gpuGeosets[i].worldMatrix = state.geosetTransforms[i];

    for (int i = 0; i < (int)state.geosetAlphas.size() && i < (int)gpuGeosets.size(); i++)
        gpuGeosets[i].geosetAlpha = state.geosetAlphas[i];

    for (int i = 0; i < (int)state.geosetColors.size() && i < (int)gpuGeosets.size(); i++)
        gpuGeosets[i].geosetColor = state.geosetColors[i];
}

void RenderModel::ApplyLayerStates(const FrameState& state) {
    // Texture animations (per-layer) — clear stale entries from previous frame
    matTexAnim.clear();
    for (auto& ta : state.texAnims) {
        int key = ta.materialId * 1000 + ta.layerIndex;
        matTexAnim[key] = {ta.uOff, ta.vOff, ta.uTile, ta.vTile, ta.rotation};
    }

    // BLS-path palette: dense by textureAnimationId. Size it to the max id
    // we see this frame and fill unused slots with identity.
    int maxTexAnimId = -1;
    for (auto& tam : state.texAnimMatrices) {
        if (tam.textureAnimId > maxTexAnimId) maxTexAnimId = tam.textureAnimId;
    }
    texAnimPalette.assign(std::max(0, maxTexAnimId + 1),
                          TexAnimPaletteEntry{
                              {1.0f, 0.0f, 0.0f, 0.0f},
                              {0.0f, 1.0f, 0.0f, 0.0f}});
    for (auto& tam : state.texAnimMatrices) {
        if (tam.textureAnimId < 0 || tam.textureAnimId > maxTexAnimId) continue;
        auto& e = texAnimPalette[tam.textureAnimId];
        for (int k = 0; k < 4; ++k) { e.row0[k] = tam.row0[k]; e.row1[k] = tam.row1[k]; }
    }

    // Per-layer alpha animation (KMTA tracks)
    for (auto& la : state.layerAlphas) {
        if (la.materialId >= 0 && la.materialId < (int)gpuMaterials.size()) {
            auto& layers = gpuMaterials[la.materialId].cpu.layers;
            if (la.layerIndex >= 0 && la.layerIndex < (int)layers.size())
                layers[la.layerIndex].alpha = la.alpha;
        }
    }

    // Per-layer texture ID animation (KMTF tracks). Reforged HD layers
    // carry an independent KMTF track per subtexture slot, so route the
    // animated id into the matching *MapId field by slot. Pre-Reforged
    // layers always emit slot=Diffuse → updates `textureId` exactly as
    // before.
    for (auto& lt : state.layerTextureIds) {
        if (lt.materialId < 0 || lt.materialId >= (int)gpuMaterials.size()) continue;
        auto& layers = gpuMaterials[lt.materialId].cpu.layers;
        if (lt.layerIndex < 0 || lt.layerIndex >= (int)layers.size()) continue;
        auto& L = layers[lt.layerIndex];
        switch (lt.slot) {
            case FrameState::LayerTexSlot::Diffuse:   L.textureId       = lt.textureId; break;
            case FrameState::LayerTexSlot::Normal:    L.normalMapId     = lt.textureId; break;
            case FrameState::LayerTexSlot::ORM:       L.ormMapId        = lt.textureId; break;
            case FrameState::LayerTexSlot::Emissive:  L.emissiveMapId   = lt.textureId; break;
            case FrameState::LayerTexSlot::TeamColor:
                // Preserve the kHdTeamColorActive sentinel (-2): the layer
                // is asking for the live UI swatch, not a per-frame texture
                // id swap. KMTF on a TeamColor slot only makes sense when
                // the artist authored real masks; in that case the static
                // teamColorMapId was already set to the diffuse id by the
                // adapter, and we overwrite with the animated value here.
                if (L.teamColorMapId != kHdTeamColorActive)
                    L.teamColorMapId = lt.textureId;
                break;
        }
    }

    // Per-layer fresnel / emissive animation — matches Previewd's
    // RenderGeosetLayers (0x14030a210), which stamps these four values
    // into layerMaterial.m_pixelParams every draw from the per-layer
    // evaluated track arrays (modelptr->m_fresnelColor etc.).
    for (auto& lf : state.layerFresnels) {
        if (lf.materialId >= 0 && lf.materialId < (int)gpuMaterials.size()) {
            auto& layers = gpuMaterials[lf.materialId].cpu.layers;
            if (lf.layerIndex >= 0 && lf.layerIndex < (int)layers.size()) {
                auto& L = layers[lf.layerIndex];
                L.fresnelColor     = lf.fresnelColor;
                L.fresnelOpacity   = lf.fresnelOpacity;
                L.fresnelTeamColor = lf.fresnelTeamColor;
                L.emissiveGain     = lf.emissiveGain;
            }
        }
    }

    // Cache evaluated MDX scene lights. We keep BOTH enabled and disabled
    // entries so the debug "Lights" overlay can show every authored light
    // at its world position (disabled ones drawn dim). Consumers that want
    // only the enabled set must check `L.enabled` themselves.
    activeLights = state.lights;
}

void RenderModel::ApplyRibbonFrameStates(const FrameState& state) {
    for (auto& rs : state.ribbonStates) {
        RibbonEmitterState st;
        st.transform   = rs.transform;
        st.above       = rs.above;
        st.below       = rs.below;
        st.alpha       = rs.alpha;
        st.color       = rs.color;
        st.visibility  = rs.visibility;
        st.slot        = rs.slot;
        ribbons.UpdateEmitterState(rs.emitterId, st);
    }
}

void RenderModel::ApplyPE1FrameStates(const FrameState& state) {
    for (auto& ps : state.pe1States) {
        PE1EmitterState st;
        st.transform    = ps.transform;
        st.emissionRate = ps.emissionRate;
        st.speed        = ps.speed;
        st.latitude     = ps.latitude;
        st.longitude    = ps.longitude;
        st.gravity      = ps.gravity;
        st.visibility   = ps.visibility;
        pe1.UpdateEmitterState(ps.emitterId, st);
    }
}

} // namespace WhiteoutDex
