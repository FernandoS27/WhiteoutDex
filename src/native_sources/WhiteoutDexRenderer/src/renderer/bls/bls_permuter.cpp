#include "bls_permuter.h"

#include <initializer_list>

namespace WhiteoutDex::bls {

namespace {

// Mixed-radix pack (least-significant-first), matching
// CalculatePermutationIndex_<r0>_<r1>_...(d0, d1, ...) at Previewd 0x1403fb850.
// dims[i] MUST satisfy dims[i] < radices[i]; callers clamp at the state mapping.
uint32_t Pack(std::initializer_list<uint32_t> radices,
              std::initializer_list<uint32_t> dims) {
    const auto n = radices.size();
    uint32_t packed = 0;
    uint32_t stride = 1;
    auto r = radices.begin();
    auto d = dims.begin();
    for (size_t i = 0; i < n; ++i) {
        packed += (*d++) * stride;
        stride *= (*r++);
    }
    return packed;
}

uint32_t WeightIndex(uint8_t numWeights) {
    switch (numWeights) {
        case 1: return 1;
        case 4: return 2;
        default: return 0;
    }
}

uint32_t FogIndex(const RenderState& s) {
    return s.fogEnabled ? (uint32_t(s.fogStyle) + 1u) : 0u;
}

} // namespace

PermuteIndices SelectPermutes(const RenderState& s) {
    const uint32_t flags  = s.materialFlags;
    const uint32_t lights = (!s.prepass && s.lightingEnabled && s.numLights > 0) ? 1u : 0u;
    const uint32_t fogIdx = FogIndex(s);
    // fogIdx_prepass — v33 in GetShaderIndices: zeroed when prepass is active.
    const uint32_t fogIdxPrepass = s.prepass ? 0u : fogIdx;
    const uint32_t depthWrite = s.depthWrite ? 1u : 0u;
    const uint32_t prepass    = s.prepass    ? 1u : 0u;
    const uint32_t shadows    = s.shadows    ? 1u : 0u;
    const uint32_t alphaTest  = (s.alphaMode != 0) ? 1u : 0u;
    const uint32_t teamColor  = s.teamColor ? 1u : 0u;
    const uint32_t debug      = s.debugShader ? 1u : 0u;

    PermuteIndices out{0, 0};

    switch (s.shaderId) {
        case GxShaderID::PopcornFX: {
            // VS: _2_2_2_3_3 -> 72 permutes
            out.vs = Pack({2,2,2,3,3}, {
                (flags >> 5) & 1u,
                (flags >> 4) & 1u,
                lights,
                (flags >> 2) & 3u,
                flags        & 3u,
            });
            // PS: _2_4_2_2_2_2_3_3 -> 2304 permutes
            out.ps = Pack({2,4,2,2,2,2,3,3}, {
                depthWrite,
                fogIdx,
                (flags >> 6) & 1u,
                (flags >> 5) & 1u,
                (flags >> 4) & 1u,
                lights,
                (flags >> 2) & 3u,
                flags        & 3u,
            });
            break;
        }
        case GxShaderID::HD:
        case GxShaderID::Crystal: {
            // VS: _2_3_2_3_2_2 -> 144 permutes
            out.vs = Pack({2,3,2,3,2,2}, {
                uint32_t(s.numTangents != 0 ? 1 : 0),
                WeightIndex(s.numWeights),
                uint32_t(s.numColors),
                uint32_t(s.numTexCoords),
                prepass,
                shadows,
            });
            // PS: _2_2_2_2_4_2_2_2 -> 512 permutes
            out.ps = Pack({2,2,2,2,4,2,2,2}, {
                depthWrite,
                prepass,
                shadows,
                lights,
                fogIdxPrepass,
                alphaTest,
                teamColor,
                debug,
            });
            break;
        }
        case GxShaderID::SD_on_HD: {
            out.vs = Pack({2,3,2,3,2,2}, {
                uint32_t(s.numTangents != 0 ? 1 : 0),
                WeightIndex(s.numWeights),
                uint32_t(s.numColors),
                uint32_t(s.numTexCoords),
                prepass,
                shadows,
            });
            // PS: _2_2_2_2_4_3_2 -> 384 permutes. The alpha dim is 3 for
            // SD_on_HD: { opaque, tested, additive }.
            uint32_t alphaDim = (s.alphaMode == 2 /* Add */) ? 2u : alphaTest;
            out.ps = Pack({2,2,2,2,4,3,2}, {
                depthWrite,
                prepass,
                shadows,
                lights,
                fogIdxPrepass,
                alphaDim,
                debug,
            });
            break;
        }
        case GxShaderID::SD: {
            out.vs = Pack({3,2,3,9}, {
                WeightIndex(s.numWeights),
                uint32_t(s.numColors),
                uint32_t(s.numTexCoords),
                uint32_t(s.numLights),
            });
            // PS: _4_2_5_5 -> fog*alpha*texOp*texOp. Two texOps unused here
            // (RE shows Modulate/Disable constants) so we pin dim2=1, dim3=0.
            out.ps = Pack({4,2,5,5}, {
                fogIdx,
                alphaTest,
                1u,
                0u,
            });
            break;
        }
        case GxShaderID::Terrain: {
            out.vs = Pack({2,2,2}, {
                prepass,
                shadows,
                uint32_t(s.numColors),
            });
            out.ps = Pack({2,2,2,2,4,2}, {
                depthWrite,
                prepass,
                shadows,
                s.darkerShadows ? 1u : 0u,
                fogIdxPrepass,
                debug,
            });
            break;
        }
        case GxShaderID::CliffBlightMiscTerrain: {
            out.vs = Pack({2,2}, {prepass, shadows});
            out.ps = Pack({2,2,2,2,4,2,2}, {
                depthWrite,
                prepass,
                shadows,
                s.darkerShadows ? 1u : 0u,
                fogIdxPrepass,
                alphaTest,
                debug,
            });
            break;
        }
        case GxShaderID::Foliage: {
            out.vs = Pack({2,2,2}, {
                prepass,
                shadows,
                flags & 1u,
            });
            out.ps = Pack({2,2,2,4,2,2}, {
                depthWrite,
                prepass,
                shadows,
                fogIdxPrepass,
                alphaTest,
                debug,
            });
            break;
        }
        case GxShaderID::Water:
        case GxShaderID::Fog:
        case GxShaderID::ConeIndicator: {
            out.vs = 0;
            out.ps = Pack({4}, {fogIdxPrepass});
            break;
        }
        case GxShaderID::Sprite: {
            out.vs = 0;
            out.ps = Pack({2,2}, {(flags >> 1) & 1u, flags & 1u});
            break;
        }
        case GxShaderID::Movie: {
            out.vs = 0;
            out.ps = Pack({2,2,3,2}, {
                (flags >> 3) & 1u,
                (flags >> 2) & 1u,
                (flags >> 1) & 1u,
                flags        & 1u,
            });
            break;
        }
        case GxShaderID::BloomCombine: {
            out.vs = 0;
            out.ps = Pack({2}, {s.clampBloomOutput ? 1u : 0u});
            break;
        }
        case GxShaderID::Imgui: {
            out.vs = 0;
            out.ps = Pack({2}, {flags & 1u});
            break;
        }
        default: {
            out.vs = 0;
            out.ps = 0;
            break;
        }
    }

    return out;
}

PermuteCounts ExpectedPermuteCounts(GxShaderID id) {
    switch (id) {
        case GxShaderID::PopcornFX:              return {  72, 2304 };
        case GxShaderID::HD:
        case GxShaderID::Crystal:                return { 144,  512 };
        case GxShaderID::SD_on_HD:               return { 144,  384 };
        case GxShaderID::SD:                     return { 162,  200 };
        case GxShaderID::Terrain:                return {   8,  128 };
        case GxShaderID::CliffBlightMiscTerrain: return {   4,  256 };
        case GxShaderID::Foliage:                return {   8,  192 };
        case GxShaderID::Water:
        case GxShaderID::Fog:
        case GxShaderID::ConeIndicator:          return {   1,    4 };
        case GxShaderID::Sprite:                 return {   1,    4 };
        case GxShaderID::Movie:                  return {   1,   24 };
        case GxShaderID::BloomCombine:           return {   1,    2 };
        case GxShaderID::Imgui:                  return {   1,    2 };
        default:                                 return {   1,    1 };
    }
}

} // namespace WhiteoutDex::bls
