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
// Path B — SD_on_HD / HD / Terrain / etc. (preserved for HD mode, not used
// in SD-mode draws but we keep the definitions to avoid a later re-add.)
// ============================================================================

struct SdVsCbB {
    Matrix44f    world;          // 0x000
    Matrix44f    worldView;      // 0x040
    Matrix44f    worldViewProj;  // 0x080
    Vector4f     misc;           // 0x0C0 {effectTime, popcornScale, clipHeight, underWater}
    Vector4f     diffuseColor;   // 0x0D0
    ShaderTexMtx texMtx0;        // 0x0E0
    ShaderTexMtx texMtx1;        // 0x100
};
static_assert(sizeof(SdVsCbB) == 288);

struct SdPsCbB {
    float       alphaRef;              // 0x000
    float       _pad0[3];              // 0x004
    Vector4f    fogParams;             // 0x010
    Vector4f    fogColor;              // 0x020
    Matrix44f   worldView;             // 0x030
    Matrix44f   viewInverse;           // 0x070
    Matrix44f   projection;            // 0x0B0
    Vector4f    viewportRect;          // 0x0F0
    Vector4f    pixelParams1;          // 0x100
    Vector4f    pixelParams2;          // 0x110
    Vector4f    pixelParams3;          // 0x120
    Vector4f    envMapParams;          // 0x130
    float       effectTime;            // 0x140
    float       emissiveGain;          // 0x144
    int32_t     numLights;             // 0x148
    int32_t     useNdf;                // 0x14C
    ShaderLight lights[kMaxLights];    // 0x150
};
static_assert(offsetof(SdPsCbB, lights) == 0x150);

inline uint32_t SdPsCbBSize(int numLights) { return 336u + 64u * static_cast<uint32_t>(numLights); }

// ============================================================================
// Bone palette (cb3 when skinning is active)
// ============================================================================

struct BonePaletteCb {
    ShaderBone bones[kMaxBones];
};
static_assert(sizeof(BonePaletteCb) == 12288);

} // namespace WhiteoutDex::bls
