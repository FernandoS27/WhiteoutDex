#pragma once
// ============================================================================
// BLS MatParams — C++ mirror of the subset of CGxMatParams that drives
// render-state selection in Previewd. See docs/BLS_ParticleAndMesh.md §1.4 /
// §1.6 for the RE, and docs/BLS_ShaderABI.md for the CB layouts the material
// feeds into.
// ============================================================================

#include "bls_permuter.h"
#include "model_types.h"
#include "particle/particle_material.h"
#include "types.h"

#include <cstdint>

namespace WhiteoutDex::bls {

// EGxMatAlphaOp — the engine's alpha mode. Maps 1:1 to GetBlendMode output in
// ProcessTexLayers (0x14048c630).
enum class GxMatAlpha : uint8_t {
    Opaque     = 0,   // TEXOP_LOAD        -> no blend, no alpha test
    AlphaKey   = 1,   // TEXOP_TRANSPARENT -> discard fragments below threshold
    Blend      = 2,   // TEXOP_BLEND       -> src.a, 1-src.a
    Add        = 3,   // TEXOP_ADD / ADD_ALPHA
    Modulate   = 4,   // TEXOP_MODULATE
    Modulate2X = 5,   // TEXOP_MODULATE2X
};

// Bits of CGxMatParams::m_disables — verified against EnableState() calls in
// GetShaderIndices.
inline constexpr uint32_t kDisableLighting   = 0x01;
inline constexpr uint32_t kDisableFog        = 0x02;
inline constexpr uint32_t kDisableDepthTest  = 0x04;
inline constexpr uint32_t kDisableDepthWrite = 0x08;
inline constexpr uint32_t kDisableCull       = 0x10;
inline constexpr uint32_t kDisableBit5       = 0x20;  // cleared by ProcessTexLayers
inline constexpr uint32_t kDisableBit8       = 0x100; // set by DEPTHFILL_DEPTH path

struct MatParams {
    GxShaderID shaderId     = GxShaderID::SD;
    GxMatAlpha alpha        = GxMatAlpha::Opaque;
    uint32_t   disables     = 0;
    Vector4f   diffuseColor = {1, 1, 1, 1};       // ToColorVec4 result (0..1)
    Vector4f   vertexPad    = {-3.4028235e38f, 1.0f, 0.0f, 0.0f}; // clipHeight, underWater
    float      popcornScale = 1.0f;
    float      emissiveGain = 0.0f;
    uint32_t   spriteFlags  = 0;

    // Per-material HD PS pixelParams fields. Engine layout (from
    // CGxMatParams::PixelParams in IDA):
    //   pixelParams1 = {inverseSoftness, cloak, fresnelTeamColor, pad=0}
    //   pixelParams2 = pad2  (always zero in engine, kept in CB for layout)
    //   fresnelColor = {fresnelR, fresnelG, fresnelB, fresnelA}
    // inverseSoftness is 1.0 for normal rendering (IStateSync writes 1.0
    // or 0.0 based on a master-enable bit); cloak is an in-game state
    // we don't drive, so default stays at 0. The fresnel fields come
    // from the MDX v1200 HD Layer chunk (fresnelColor/Opacity/TeamColor).
    float      inverseSoftness = 1.0f;
    float      cloakAmount     = 0.0f;
    float      fresnelTeamColor = 0.0f;
    Vector3f   fresnelColor    = {0.0f, 0.0f, 0.0f};
    float      fresnelOpacity  = 0.0f;

    bool LightingEnabled() const { return (disables & kDisableLighting)   == 0; }
    bool FogEnabled()      const { return (disables & kDisableFog)        == 0; }
    bool DepthTestEnabled()const { return (disables & kDisableDepthTest)  == 0; }
    bool DepthWriteEnabled()const{ return (disables & kDisableDepthWrite) == 0; }
    bool CullEnabled()     const { return (disables & kDisableCull)       == 0; }
    // Engine SelectModelMaterial sets m_disables |= 0x100 (kDisableBit8)
    // for the DEPTHFILL_DEPTH prepass clone — we mirror that as a color-mask
    // off so the depth pass only writes Z.
    bool ColorWriteEnabled()const{ return (disables & kDisableBit8)       == 0; }
};

// ---------- MDX FilterMode -> GxMatAlpha / disables ------------------------
GxMatAlpha FilterToGxAlpha(int filterMode);   // our FILTER_* enum
uint32_t   FilterToDisables(int filterMode);  // adds the blend-mode-driven bits

// ---------- Builders -------------------------------------------------------

// Build MatParams from an MDX mesh layer (mirrors ProcessTexLayers +
// GetMaterialDisables).
MatParams FromMdxLayer(int filterMode, int matFlags, GxShaderID shaderId);

// Build MatParams from a PE2 particle material (mirrors
// ILoadParticleEmitters2).
MatParams FromParticleDesc(const particle::ParticleMaterialDesc& desc,
                           GxShaderID shaderId);

} // namespace WhiteoutDex::bls
