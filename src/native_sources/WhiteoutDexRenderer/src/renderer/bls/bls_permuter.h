#pragma once
// ============================================================================
// BlsPermuter — per-EGxShaderID permutation index selectors.
//
// Mirrors GetShaderIndices() at Previewd 0x1403faa50. Each selector packs a
// handful of state bits into a single uint32 index using the mixed-radix
// encoding from CalculatePermutationIndex_<radices>():
//
//   packed = d[0] + r[0]*d[1] + r[0]*r[1]*d[2] + ...
//
// The radices encode the dimension count of each state axis for that shader;
// the total permutation count is product(r[i]) and must equal the BLS
// container's permutationCount.
// ============================================================================

#include <array>
#include <cstdint>

namespace WhiteoutDex::bls {

enum class GxShaderID : uint8_t {
    SD                     = 0x00,
    HD                     = 0x01,
    SD_on_HD               = 0x02,
    Terrain                = 0x03,
    Water                  = 0x04,
    Fog                    = 0x05,
    Foliage                = 0x06,
    FoliagePush            = 0x07,
    Sprite                 = 0x08,
    DebugTexture           = 0x09,
    DepthOfField           = 0x0A,
    BloomCombine           = 0x0B,
    BloomExtract           = 0x0C,
    GaussianBlur           = 0x0D,
    Tonemap                = 0x0E,
    Movie                  = 0x0F,
    FFXCMAAEdge0           = 0x10,
    FFXCMAAEdge1           = 0x11,
    FFXCMAAEdgeCombine     = 0x12,
    FFXCMAAProcessAndApply = 0x13,
    PopcornFX              = 0x14,
    ConeIndicator          = 0x15,
    CliffBlightMiscTerrain = 0x16,
    Distortion             = 0x17,
    Crystal                = 0x18,
    Imgui                  = 0x19,
};

// Subset of CGxStateRegister / material state that drives the per-shader
// switch in GetShaderIndices. Populate the fields your shader arm uses; the
// rest are ignored.
struct RenderState {
    GxShaderID shaderId       = GxShaderID::Sprite;

    // Material
    uint32_t   materialFlags  = 0;   // Blizzard material flags.flags
    uint8_t    alphaMode      = 0;   // GxMatAlpha_Disable = 0
    bool       teamColor      = false;
    bool       debugShader    = false;

    // Master state
    bool       lightingEnabled = false;
    uint8_t    numLights      = 0;   // number of enabled m_lights entries
    uint8_t    fogStyle       = 0;   // only meaningful when fogEnabled
    bool       fogEnabled     = false;
    bool       depthWrite     = false;
    bool       shadows        = false;
    bool       prepass        = false;
    bool       darkerShadows  = false;

    // Derived-from-vertex-format state
    uint8_t    numColors      = 0;
    uint8_t    numTexCoords   = 0;
    uint8_t    numTangents    = 0;
    uint8_t    numWeights     = 0;   // 0, 1 or 4 -> weightIndex 0/1/2

    // Post
    bool       clampBloomOutput = false;
};

struct PermuteIndices {
    uint32_t vs = 0;
    uint32_t ps = 0;
};

// Total permute count for a shader, matching BLS containers that ship for it.
struct PermuteCounts {
    uint32_t vs;
    uint32_t ps;
};

PermuteIndices SelectPermutes(const RenderState& state);
PermuteCounts  ExpectedPermuteCounts(GxShaderID id);

} // namespace WhiteoutDex::bls
