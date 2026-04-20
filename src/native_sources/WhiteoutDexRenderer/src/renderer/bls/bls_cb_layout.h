#pragma once
// ============================================================================
// BLS shader constant-buffer layouts (Phase 0 RE, corrected after shader
// disassembly verification).
//
// CGxDevice::IStateSync has TWO code paths, gated on m_material.m_shaderID:
//   - Path A (shaderID==0): SD-in-SD-mode, Sprite, Movie, Imgui. VS CB at
//     root slot 0, PS CB at root slot 0. 208-byte VS, 48-byte PS base.
//   - Path B (shaderID!=0): SD_on_HD, HD, Terrain, Water, Foliage, PopcornFX,
//     Crystal. VS CB at root slot 2, PS CB at root slot 2. 288-byte VS,
//     336-byte PS base.
//
// Only Path A is used when rendering in SD mode (GxDevRenderMode() == SD).
// MatSelect canonicalises SD ↔ SD_on_HD to the mode-appropriate ID, so SD
// materials stay on shaderID=0 in SD mode.
//
// See docs/BLS_ShaderABI.md for the full derivation; the disassembly at
// src/war3.w3mod/shaders/vs/sd_highspec.bls perm 9 is the anchor for the
// Path A layout.
// ============================================================================

#include "types.h"

#include <cstdint>

namespace WhiteoutDex::bls {

struct ShaderBone {
    Vector4f row0;
    Vector4f row1;
    Vector4f row2;
};
static_assert(sizeof(ShaderBone) == 48);

struct ShaderTexMtx {
    Vector4f rows[2];
};
static_assert(sizeof(ShaderTexMtx) == 32);

// 64 bytes in the compiled DXBC, NOT 48. The Slang source carries an
// explicit `float4 _pad2` and the HLSL/Slang compiler keeps it -- verified
// by disassembling war3.w3mod/shaders/vs/sd_highspec.bls perm 45 (2 lights):
// light[0] lives at cb0[13..15], light[1] at cb0[17..19] -- stride = 4
// float4 slots = 64 B. Previewd's own upload uses 48 B stride (imul 0x30 at
// 0x1403f78fe), which looks like a shipped bug in the engine; we upload at
// the stride the shader actually reads so lights 1..N aren't garbled.
struct ShaderLight {
    Vector4f ambient;
    Vector4f diffuse;
    Vector4f position;
    Vector4f _pad;   // required to match shader cbuffer stride
};
static_assert(sizeof(ShaderLight) == 64);

inline constexpr int kMaxLights = 8;
inline constexpr int kMaxBones  = 256;

// ============================================================================
// Path A — SD in SD mode
// ============================================================================

// VS CB (Path A, cb0, 208 B + 48 B per light). Verified against the
// decompiled Slang source at C:/Projects/Wc3Shaders/wc3_shaders/types/
// cb_structs.slang (struct SDHighspecVSPerDraw).
//
// The shader uses mul(matrix, col_vector) convention (see transformClipPos
// in vs_body.slang) -- so our row-major Matrix44f must be uploaded
// DIRECTLY, without transposing. HLSL's default column_major layout then
// interprets our "rows" as "columns", producing the correct result for
// col-vector multiplication.
//
// cb0[0..3]   world          -- transformByWorld(pos, world)  -> world pos (o3)
// cb0[4..7]   worldViewProj  -- transformClipPos(pos, wvp)    -> clip pos  (o0)
// cb0[8]      diffuseColor   -- o1 = vertColor * diffuseColor
// cb0[9..10]  texMtx0Row0/1  -- o2.xy = texMtx0 * (u,v,1)
// cb0[11..12] texMtx1Row0/1  -- second UV set
// cb0[13..]   ShaderLight[numLights]
struct SdVsCbA {
    Matrix44f    world;          // 0x00
    Matrix44f    worldViewProj;  // 0x40
    Vector4f     diffuseColor;   // 0x80
    ShaderTexMtx texMtx0;        // 0x90
    ShaderTexMtx texMtx1;        // 0xB0
    ShaderLight  lights[kMaxLights]; // 0xD0 (upload only 48*N of it)
};
static_assert(offsetof(SdVsCbA, world)         == 0x00);
static_assert(offsetof(SdVsCbA, worldViewProj) == 0x40);
static_assert(offsetof(SdVsCbA, diffuseColor)  == 0x80);
static_assert(offsetof(SdVsCbA, texMtx0)       == 0x90);
static_assert(offsetof(SdVsCbA, texMtx1)       == 0xB0);
static_assert(offsetof(SdVsCbA, lights)        == 0xD0);

inline uint32_t SdVsCbASize(int numLights) { return 208u + 64u * static_cast<uint32_t>(numLights); }

// PS CB (Path A, cb0, 48 B)
struct SdPsCbA {
    float    alphaRef;        // 0x00
    float    _pad[3];         // 0x04
    Vector4f fogParams;       // 0x10 {start, end, density, 0}
    Vector4f fogColor;        // 0x20 sRGB-linear
};
static_assert(sizeof(SdPsCbA) == 48);

// ============================================================================
// Path B — HD / SD_on_HD / Crystal / Terrain / Water / Foliage / etc.
// Used when GxDevRenderMode() == HD. Both HD and SD_on_HD VS consume the
// same VS CB layout; the PS CB layouts differ in which fields are
// meaningful vs padding, but the base size is identical.
//
// Field order verified against Previewd's IStateSync path B upload block
// starting at 0x1403f84cb (VS, root slot 2) and 0x1403f89da (PS, root slot
// 2) -- see docs/BLS_ShaderABI.md sections 2.1 and 2.4.
// ============================================================================

// VS CB (HD + SD_on_HD) @ shader register b2. 288 B.
// Field names match Previewd semantics verbatim (verified against
// CGxDevice::IStateSync path B at 0x1403f82a2..0x1403f84cb). The Slang
// source at types/cb_structs.slang preserved the original engine's
// misleading "viewProj / world" labels; we discard those and use what
// each slot actually holds. The shader reads the CB by OFFSET (DXBC
// doesn't carry Slang field names), so renaming here is harmless.
//
// Slot map (16-byte slots):
//   cb0[0..3]   world         — pure model-to-world
//   cb0[4..7]   worldView     — world * view (PS worldPos is view-space)
//   cb0[8..11]  worldViewProj
//   cb0[12]     { effectTime, popcornScale, clipHeight, underWater }
//   cb0[13]     diffuseColor
//   cb0[14..15] texMtx0 rows
//   cb0[16..17] texMtx1 rows
struct HdVsCb {
    Matrix44f    world;          // 0x000  m_prismWorldMat
    Matrix44f    worldView;      // 0x040  m_prismWorldMat * m_prismViewMat
    Matrix44f    worldViewProj;  // 0x080  above * m_prismProjectionMat
    Vector4f     misc;           // 0x0C0  { effectTime, popcornScale, clipHeight, underWater }
    Vector4f     diffuseColor;   // 0x0D0
    ShaderTexMtx texMtx0;        // 0x0E0  (row0 at 0x0E0, row1 at 0x0F0)
    ShaderTexMtx texMtx1;        // 0x100  (row0 at 0x100, row1 at 0x110)
};
static_assert(sizeof(HdVsCb) == 288);
static_assert(offsetof(HdVsCb, world)         == 0x000);
static_assert(offsetof(HdVsCb, worldView)     == 0x040);
static_assert(offsetof(HdVsCb, worldViewProj) == 0x080);
static_assert(offsetof(HdVsCb, misc)          == 0x0C0);
static_assert(offsetof(HdVsCb, diffuseColor)  == 0x0D0);
static_assert(offsetof(HdVsCb, texMtx0)       == 0x0E0);
static_assert(offsetof(HdVsCb, texMtx1)       == 0x100);

// HD PS CB @ shader register b2. 336 B + N * sizeof(ShaderLight) = 64 B.
// Matches Wc3Shaders/types/cb_structs.slang::PSPerDraw. lightCount/useNdf
// are floats in Slang (the DXBC reads them via mov instructions; the bit
// pattern is what matters, not the declared type, but we use floats so
// CPU-side values flow unambiguously).
struct HdPsCb {
    float       alphaRef;              // 0x000
    float       _pad0[3];              // 0x004
    Vector4f    fogParams;             // 0x010  {start, end, density, 0}
    Vector4f    fogColor;              // 0x020  sRGB->linear
    Matrix44f   worldView;             // 0x030
    Matrix44f   view;                  // 0x070
    Matrix44f   projection;            // 0x0B0
    Vector4f    viewportRect;          // 0x0F0  {width, height, x, 1 - yHigh}
    Vector4f    pixelParams1;          // 0x100  {inverseSoftness, cloakAmount, fresnelTeamColor, 0}
    Vector4f    pixelParams2;          // 0x110  reserved
    Vector4f    fresnelColor;          // 0x120
    Vector4f    envMapParams;          // 0x130  {envFromMipEnd, envToMipEnd, envTransitionT, 0}
    float       effectTime;            // 0x140
    float       emissiveGain;          // 0x144
    float       lightCount;            // 0x148  -- stored as float per Slang
    float       useNdf;                // 0x14C
    ShaderLight lights[kMaxLights];    // 0x150
};
static_assert(offsetof(HdPsCb, lights) == 0x150);
static_assert(sizeof(HdPsCb) == 0x150 + 64 * kMaxLights);

inline uint32_t HdPsCbSize(int numLights) {
    return 336u + 64u * static_cast<uint32_t>(numLights);
}

// SD_on_HD PS CB @ shader register b2. Same base size (336 B) as HD PS CB
// but most fields are padding -- only fog, alphaRef, inv-view rows,
// envMap mip/transition, pixelParams1, lightCount, and lights[] are
// consumed by sd_on_hd_ps.slang. invViewRow0..2 are the rows of the
// TRANSPOSED view->world rotation (used to orient cubemap samples).
struct SdOnHdPsCb {
    float       alphaRef;              // 0x000
    float       _pad0[3];              // 0x004
    Vector4f    fogParams;             // 0x010
    Vector4f    fogColor;              // 0x020
    Vector4f    _pad3;                 // 0x030
    Vector4f    _pad4;                 // 0x040
    Vector4f    _pad5;                 // 0x050
    Vector4f    _pad6;                 // 0x060
    Vector4f    invViewRow0;           // 0x070
    Vector4f    invViewRow1;           // 0x080
    Vector4f    invViewRow2;           // 0x090
    Vector4f    _pad10;                // 0x0A0
    Vector4f    _pad11;                // 0x0B0
    Vector4f    _pad12;                // 0x0C0
    Vector4f    _pad13;                // 0x0D0
    Vector4f    _pad14;                // 0x0E0
    Vector4f    _pad15;                // 0x0F0
    Vector4f    pixelParams1;          // 0x100 {inverseSoftness, cloakAmount, 0, 0}
    Vector4f    _pad17;                // 0x110
    Vector4f    _pad18;                // 0x120
    Vector4f    envMapParams;          // 0x130 {envFromMipEnd, envToMipEnd, envTransitionT, 0}
    Vector4f    lightCountSlot;        // 0x140 lightCountSlot.z = reinterpret_cast<float>(numLights)
    ShaderLight lights[kMaxLights];    // 0x150
};
static_assert(offsetof(SdOnHdPsCb, invViewRow0) == 0x070);
static_assert(offsetof(SdOnHdPsCb, envMapParams) == 0x130);
static_assert(offsetof(SdOnHdPsCb, lightCountSlot) == 0x140);
static_assert(offsetof(SdOnHdPsCb, lights) == 0x150);
static_assert(sizeof(SdOnHdPsCb) == 0x150 + 64 * kMaxLights);

inline uint32_t SdOnHdPsCbSize(int numLights) {
    return 336u + 64u * static_cast<uint32_t>(numLights);
}

// VS shadow cascades CB @ shader register b1. Three view-projection
// matrices, used by hd_vs.slang when HAS_SHADOWS=1 (and by sd_on_hd_vs).
// Unused in the MVP (we ship shadows=0 perm), reserved for a later pass.
struct HdShadowCascadesCb {
    Matrix44f cascade0;   // 0x000
    Matrix44f cascade1;   // 0x040
    Matrix44f cascade2;   // 0x080
};
static_assert(sizeof(HdShadowCascadesCb) == 192);

// SD_on_HD PS shadow cascade count @ register b1. Scalar float cast into
// a float4 for alignment.
struct SdOnHdShadowCascadeCountCb {
    float numCascades;
    float _pad[3];
};
static_assert(sizeof(SdOnHdShadowCascadeCountCb) == 16);

// Debug-vis CB @ PS register b3 (only bound when the HAS_DEBUG_VIS
// permute is picked). Matches cb_structs.slang::DebugVisCB.
// `debugMode` selects the visualiser (0=normal render, 1=albedo,
// 2=world-normal, 3=LOD heatmap, 4=lightCount).
// `enabledShaders`: bit0 = replace albedo with overrideAlbedo;
//                   bit1 = replace ORM  with overrideOrm.
struct DebugVisCb {
    uint32_t enabledShaders;  float    debugMode;
    float    _p0[2];
    Vector3f overrideAlbedo;  float    _p1;
    Vector3f overrideOrm;     float    _p2;
};
static_assert(sizeof(DebugVisCb) == 48);

// ============================================================================
// Bone palette (cb3 when skinning is active)
// ============================================================================

struct BonePaletteCb {
    ShaderBone bones[kMaxBones];
};
static_assert(sizeof(BonePaletteCb) == 12288);

} // namespace WhiteoutDex::bls
