// ============================================================================
// DncAsset — implementation
// ============================================================================

#include "dnc_asset.h"

#include "io/mdx_animation.h"   // EvaluateTrackF32, EvaluateTrackVec3, MdxHierarchy

#include <whiteout/vector_types.h>   // transform_normal

#include <cmath>
#include <cstdio>

namespace WhiteoutDex::dnc {

namespace {
using whiteout::Vector3f;
using whiteout::mdx::Light;
using whiteout::mdx::Model;
} // namespace

DncSample Sample(const DncAsset& asset,
                 float           todHours,
                 float           hoursPerDay,
                 float           ambModifier) {
    DncSample s;
    if (!asset.HasLight() || hoursPerDay <= 0.0f) {
        return s;   // valid=false, zeroed
    }

    // -------- 1. TOD → MDL anim cursor (preview.exe TOD2AnimTime @0x7ff609a83b10) --------
    //
    //     animMs = (todHours * mdlAnimLengthMs) / hoursPerDay
    //
    // Wrap defensively: a TOD outside [0, hoursPerDay) would land outside
    // the sequence range and the track evaluator would return defaults.
    float wrapped = std::fmod(todHours, hoursPerDay);
    if (wrapped < 0.0f) wrapped += hoursPerDay;

    const int animLen   = asset.AnimLengthMs();
    const int animMsRel = (animLen > 0)
                              ? static_cast<int>((wrapped * static_cast<float>(animLen)) / hoursPerDay)
                              : 0;
    const int animMs    = asset.seqStartMs + animMsRel;

    const auto& L = asset.model.lights[0];   // shipping DNC has a single light; ignore extras

    // -------- 2. Hierarchy walk → light node world matrix --------
    std::vector<Matrix44f> boneWorld, allNodes;
    asset.hierarchy.Evaluate(animMs, asset.seqStartMs, asset.seqEndMs,
                             asset.model.globalSequences, boneWorld, allNodes,
                             /*cameraPos*/ nullptr, /*globalTimeMs*/ animMs);
    Matrix44f lightWorld = (asset.lightNodeIdx >= 0 && asset.lightNodeIdx < (int)allNodes.size())
                               ? allNodes[asset.lightNodeIdx]
                               : Matrix44f::identity();

    // -------- 3. Diffuse: dirColor * dirIntensity --------
    //
    // BGR→RGB swap mirrors MdxModelAdapter::Evaluate (the engine's
    // ReadBinLight stores RGB in file order, but the colour vector
    // ends up needing a swap when the renderer treats it as RGB —
    // see mdx_model_adapter.cpp note on BGR→RGB).
    Vector3f color = L.color;
    if (L.colorTracks.isUsed) {
        Vector3f animated = EvaluateTrackVec3(L.colorTracks, animMs,
                                              asset.seqStartMs, asset.seqEndMs, L.color);
        color = {animated.z, animated.y, animated.x};
    }
    const float intensity =
        std::max(0.0f, EvaluateTrackF32(L.intensityTracks, animMs,
                                        asset.seqStartMs, asset.seqEndMs, L.intensity));
    s.diffuse = { color.x * intensity, color.y * intensity, color.z * intensity };

    // -------- 4. Ambient: ambColor * ambModifier + ambIntensity (broadcast) --------
    //
    // Mirrors CGxLightToShaderLight (preview.exe @0x7ff609b3f1d0):
    //
    //     ambient.rgb = ambColor.rgb * ambModifier + ambIntensity
    //
    // Default ambModifier=0 matches the HD path (the engine discards
    // the MDL's authored ambColor and uses only the broadcast scalar).
    Vector3f ambColor = L.ambientColor;
    if (L.ambientColorTracks.isUsed) {
        Vector3f animated = EvaluateTrackVec3(L.ambientColorTracks, animMs,
                                              asset.seqStartMs, asset.seqEndMs, L.ambientColor);
        ambColor = {animated.z, animated.y, animated.x};
    }
    const float ambI =
        std::max(0.0f, EvaluateTrackF32(L.ambientIntensityTracks, animMs,
                                        asset.seqStartMs, asset.seqEndMs, L.ambientIntensity));
    s.ambient = { ambColor.x * ambModifier + ambI,
                  ambColor.y * ambModifier + ambI,
                  ambColor.z * ambModifier + ambI };

    // -------- 5. World-space direction --------
    //
    // MdxModelAdapter convention (verified at mdx_model_adapter.cpp:1586):
    //     worldDir = transform_normal({0, 0, -1}, nodeWorldMat)
    // — the light's local -Z transformed into world space, which is
    // "the direction toward the shaded surface" for a directional
    // sun-like light. BuildLightPalette later negates this and pushes
    // it through the view matrix; we hand back the un-negated form so
    // the caller stays free to pick its target space.
    s.worldDir = whiteout::transform_normal(Vector3f{0.0f, 0.0f, -1.0f}, lightWorld);
    const float n2 = s.worldDir.x * s.worldDir.x +
                     s.worldDir.y * s.worldDir.y +
                     s.worldDir.z * s.worldDir.z;
    if (n2 > 1.0e-12f) {
        const float invLen = 1.0f / std::sqrt(n2);
        s.worldDir = { s.worldDir.x * invLen, s.worldDir.y * invLen, s.worldDir.z * invLen };
    } else {
        // Degenerate node transform — fall back to "down" (light over
        // the scene) so consumers always get a finite, usable vector.
        s.worldDir = { 0.0f, 0.0f, -1.0f };
    }

    s.valid = true;
    return s;
}

} // namespace WhiteoutDex::dnc
