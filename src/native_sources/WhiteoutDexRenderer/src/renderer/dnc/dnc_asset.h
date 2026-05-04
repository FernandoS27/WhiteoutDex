#pragma once
// ============================================================================
// DncAsset — one Day/Night-Cycle MDL parsed into a sampleable form.
//
// Mirrors the engine's per-DNC-asset state in preview.exe:
//   * MDL is parsed once at acquire time (text via convertMdlToModel,
//     binary via Parser::parse).
//   * Sequence 0 is the day cycle; its (endMs - startMs) is the
//     animation cursor's range.
//   * MdxHierarchy is built once so per-frame sampling is cheap (pure
//     track interpolation + a hierarchy walk for the light's world
//     transform).
//
// Lifetime: cache-owned + refcounted (see DncCache). The asset is
// passed to Sample() by const-ref; `Sample` does no allocation and is
// safe to call from any thread once the asset is fully built.
// ============================================================================

#include "io/mdx_animation.h"
#include "renderer/types.h"

#include <whiteout/models/mdx/structures.h>

#include <cstdint>
#include <string>

namespace WhiteoutDex::dnc {

// One DNC MDL parsed into a usable form.
struct DncAsset {
    // Lower-cased, slash-normalised key the cache hashes by (e.g.
    // "environment/dnc/dnclordaeron/dnclordaeronunit/dnclordaeronunit.mdl").
    std::string                 key;
    whiteout::mdx::Model        model;
    MdxHierarchy                hierarchy;
    int                         seqStartMs   = 0;
    int                         seqEndMs     = 0;
    int                         lightNodeIdx = -1;   // index into hierarchy.Nodes(); -1 if model has no light
    uint32_t                    refs         = 0;

    int AnimLengthMs() const { return seqEndMs - seqStartMs; }
    bool HasLight() const    { return lightNodeIdx >= 0; }
};

// Sampled values at a given TOD hour. View-space conversion happens in
// the consumer (BaselineLights builder); the sample stays pipeline-
// agnostic so the same call can feed an SD pass and an HD pass.
struct DncSample {
    Vector3f ambient   = {0, 0, 0};   // ambColor*ambModifier + ambIntensity (broadcast)
    Vector3f diffuse   = {0, 0, 0};   // dirColor * dirIntensity
    Vector3f worldDir  = {0, 0, -1};  // unit, world-space; light pointing toward the shaded surface
    bool     valid     = false;
};

// Sample the asset at TOD `todHours` ∈ [0, hoursPerDay).
//
// Mirrors preview.exe TOD2AnimTime + per-track evaluation:
//
//     animMs   = (todHours * mdlAnimLengthMs) / hoursPerDay
//     diffuse  = dirColor(animMs) * dirIntensity(animMs)
//     ambient  = ambColor(animMs) * ambModifier + ambIntensity(animMs)   broadcast to RGB
//     worldDir = transform_normal({0, 0, -1}, lightNodeWorldMat(animMs))
//
// `ambModifier` defaults to 0.0 — matches the HD path
// (CGxLightToShaderLight @ preview.exe 0x7ff609b3f1d0 with the modifier
// argument ≈ 0). Set to a small positive value to revive the SD-on-HD
// compensation contribution from `ambColor`.
//
// Returns `valid=false` (zeroed sample) if the asset has no light node
// or `hoursPerDay <= 0`.
DncSample Sample(const DncAsset& asset,
                 float           todHours,
                 float           hoursPerDay,
                 float           ambModifier = 0.0f);

} // namespace WhiteoutDex::dnc
