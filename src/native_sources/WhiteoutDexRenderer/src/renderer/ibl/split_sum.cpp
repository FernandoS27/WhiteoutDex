#include "split_sum.h"

#include <cmath>
#include <algorithm>
#include <numbers>

namespace WhiteoutDex::ibl {

namespace {

// Radical-inverse base 2 -- van der Corput sequence used by the Hammersley
// quasi-random pair. Ports Previewd's Hammersley helper (inlined by the
// decompiler) directly.
inline float RadicalInverseVdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;  // / 0x100000000
}

// Hammersley sequence: 2D low-discrepancy points for Monte Carlo.
inline void Hammersley(float& x, float& y, uint32_t i, uint32_t count) {
    x = static_cast<float>(i) / static_cast<float>(count);
    y = RadicalInverseVdC(i);
}

// Importance-sampled GGX half-vector. Ports Previewd's ImportanceSampleGGX
// at 0x1405817f0. The decompile takes (Xi, N, roughness) -> world-space
// half vector, Normalize()d before return (final `Normalize(result, &v33)`).
// The tangent/bitangent basis is built from an up-vector that flips to
// (1,0,0) when |N.z| is within 0.001 of the pole, matching the disasm.
inline void ImportanceSampleGGX(float Xi_x, float Xi_y, float roughness,
                                float N_x, float N_y, float N_z,
                                float& H_x, float& H_y, float& H_z) {
    const float a     = roughness * roughness;
    const float phi   = 2.0f * std::numbers::pi_v<float> * Xi_x;
    const float cosTh = std::sqrt((1.0f - Xi_y) / (1.0f + (a * a - 1.0f) * Xi_y));
    const float sinTh = std::sqrt(std::max(0.0f, 1.0f - cosTh * cosTh));

    // Tangent-space half vector.
    const float Ht_x = std::cos(phi) * sinTh;
    const float Ht_y = std::sin(phi) * sinTh;
    const float Ht_z = cosTh;

    // Up-vector selection matches Previewd (0x140581998): flip to the
    // world-X axis only when N is within 0.001 of the ±Z pole.
    const float up_x = (std::fabs(N_z) >= 0.999f) ? 1.0f : 0.0f;
    const float up_y = 0.0f;
    const float up_z = (std::fabs(N_z) >= 0.999f) ? 0.0f : 1.0f;

    // tangent = normalize(cross(up, N))
    float T_x = up_y * N_z - up_z * N_y;
    float T_y = up_z * N_x - up_x * N_z;
    float T_z = up_x * N_y - up_y * N_x;
    const float Tlen = std::sqrt(T_x * T_x + T_y * T_y + T_z * T_z);
    if (Tlen > 1e-6f) { T_x /= Tlen; T_y /= Tlen; T_z /= Tlen; }

    // bitangent = cross(N, tangent)
    const float B_x = N_y * T_z - N_z * T_y;
    const float B_y = N_z * T_x - N_x * T_z;
    const float B_z = N_x * T_y - N_y * T_x;

    // sampleVec = T*Hx + B*Hy + N*Hz
    float S_x = Ht_x * T_x + Ht_y * B_x + Ht_z * N_x;
    float S_y = Ht_x * T_y + Ht_y * B_y + Ht_z * N_y;
    float S_z = Ht_x * T_z + Ht_y * B_z + Ht_z * N_z;

    // Previewd's final `Normalize(result, &v33)` — the importance-sampled
    // half-vector is unit-length in theory but the engine explicitly
    // renormalises to kill float drift. Preserve that so our LUT bits
    // match the engine's at 1-sample-per-pixel granularity.
    const float Slen = std::sqrt(S_x * S_x + S_y * S_y + S_z * S_z);
    if (Slen > 1e-6f) { S_x /= Slen; S_y /= Slen; S_z /= Slen; }
    H_x = S_x; H_y = S_y; H_z = S_z;
}

// Karis-style G1 product used by shader::G_Schlick_IBL at 0x140581470.
// Verified verbatim against Previewd's decompile:
//   k  = roughness² / 2
//   G1 = x / (x*(1-k) + k)
//   G  = G1(NoL) * G1(NoV)
// (No height correlation despite the original comment — it's the standard
// Schlick-approximated Smith product Blizzard ships.)
inline float GSmithGGX(float NoV, float NoL, float roughness) {
    const float k = (roughness * roughness) * 0.5f;
    const float GV = NoV / (NoV * (1.0f - k) + k);
    const float GL = NoL / (NoL * (1.0f - k) + k);
    return GV * GL;
}

// Previewd's shader::ClampRoughness (0x140581530):
//   out = (roughness * 255.0 + 1.0) / 256.0
// This is NOT a min-clamp — it simulates the 8-bit requantisation that
// the runtime roughness sample goes through (textures are uint8), with a
// +1 bias so the mirror extreme (roughness=0) never hits the denominator
// singularities in the GGX distribution. Matching this exactly keeps
// LUT values bit-identical with what the engine bakes.
inline float ClampRoughness(float r) {
    return (r * 255.0f + 1.0f) / 256.0f;
}

} // namespace

void GenerateSplitSumLut(int size, int sampleCount,
                         std::vector<uint8_t>& outPixels) {
    outPixels.assign(static_cast<size_t>(size) * size * 4, 0);

    // Fixed normal -- the BRDF integral is rotation-invariant, so pinning
    // N to +Z simplifies every loop iteration. V is parameterised by NoV.
    const float N_x = 0.0f, N_y = 0.0f, N_z = 1.0f;

    for (int iy = 0; iy < size; ++iy) {
        const float roughness =
            ClampRoughness((static_cast<float>(iy) + 0.5f) / static_cast<float>(size));

        for (int ix = 0; ix < size; ++ix) {
            const float NoV = (static_cast<float>(ix) + 0.5f) / static_cast<float>(size);

            // V in tangent space where N = +Z.
            const float V_x = std::sqrt(std::max(0.0f, 1.0f - NoV * NoV));
            const float V_y = 0.0f;
            const float V_z = NoV;

            float A = 0.0f;
            float B = 0.0f;

            for (uint32_t i = 0; i < static_cast<uint32_t>(sampleCount); ++i) {
                float Xi_x, Xi_y;
                Hammersley(Xi_x, Xi_y, i, static_cast<uint32_t>(sampleCount));

                float H_x, H_y, H_z;
                ImportanceSampleGGX(Xi_x, Xi_y, roughness,
                                    N_x, N_y, N_z,
                                    H_x, H_y, H_z);

                const float VoH = V_x * H_x + V_y * H_y + V_z * H_z;

                // L = normalize(2*dot(V,H)*H - V). Previewd explicitly
                // Normalize()s the reflection (0x140581dc7) — the result
                // is unit-length in theory but the decompile renormalises
                // to absorb float drift from the H normalisation above.
                float L_x = 2.0f * VoH * H_x - V_x;
                float L_y = 2.0f * VoH * H_y - V_y;
                float L_z = 2.0f * VoH * H_z - V_z;
                const float Llen = std::sqrt(L_x*L_x + L_y*L_y + L_z*L_z);
                if (Llen > 1e-6f) { L_x /= Llen; L_y /= Llen; L_z /= Llen; }

                const float NoL  = std::max(0.0f, L_z);
                const float NoH  = std::max(0.0f, H_z);
                const float VoHc = std::max(0.0f, VoH);

                if (NoL > 0.0f) {
                    const float G  = GSmithGGX(NoV, NoL, roughness);
                    // Previewd clamps G_Vis to [0,1] (0x140581f61). Without
                    // this, NoH*NoV underflows push G_Vis above 1 at grazing
                    // roughness extremes and the accumulated (A, B) drift
                    // away from the engine's LUT values.
                    float G_Vis = (G * VoHc) / (NoH * NoV);
                    G_Vis = std::clamp(G_Vis, 0.0f, 1.0f);
                    const float Fc = std::pow(1.0f - VoHc, 5.0f);

                    A += (1.0f - Fc) * G_Vis;
                    B += Fc * G_Vis;
                }
            }

            A /= static_cast<float>(sampleCount);
            B /= static_cast<float>(sampleCount);

            // Match FloatImage::Read (0x14057cdc0): clamp to [0,1] then
            // cast-to-int (truncate, no rounding) for 8-bit storage.
            auto pack8 = [](float v) -> uint8_t {
                const float s = std::clamp(v, 0.0f, 1.0f);
                return static_cast<uint8_t>(s * 255.0f);
            };

            // BGRA32Pixel byte order — `{ uint8_t b, g, r, a }` in memory
            // (see IDA dump of the struct). Previewd's FloatImage::Read
            // writes .r = pixel.x, .g = pixel.y, .b = pixel.z, .a = pixel.w
            // into those byte-ordered fields, so the on-disk layout is
            // { pixel.z, pixel.y, pixel.x, pixel.w } = { 0, B, A, 0 }.
            const size_t idx = (static_cast<size_t>(iy) * size + ix) * 4;
            outPixels[idx + 0] = 0;        // B byte — pixel.z (unused)
            outPixels[idx + 1] = pack8(B); // G byte — F0 bias
            outPixels[idx + 2] = pack8(A); // R byte — F0 scale
            outPixels[idx + 3] = 0;        // A byte — pixel.w (unused)
        }
    }
}

gfx::TextureHandle CreateSplitSumLutTexture(gfx::IGFXDevice& gfx) {
    std::vector<uint8_t> pixels;
    GenerateSplitSumLut(kSplitSumSize, kSplitSumSamples, pixels);

    gfx::TextureDesc desc;
    desc.width  = kSplitSumSize;
    desc.height = kSplitSumSize;
    // Match Previewd's upload: BGRA byte order so the shader's .r / .g
    // fetch lines up with the F0 scale / bias pair the generator packs.
    desc.format = gfx::Format::B8G8R8A8_UNORM;
    desc.usage  = gfx::TextureUsage::ShaderResource;
    return gfx.CreateTexture(desc, pixels.data());
}

} // namespace WhiteoutDex::ibl
