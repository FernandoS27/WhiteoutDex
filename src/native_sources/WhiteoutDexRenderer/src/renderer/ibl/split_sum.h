#pragma once
// ============================================================================
// Split-sum BRDF LUT — CPU generator + GPU upload for the HD pipeline's t15
// slot. Bit-for-bit matches Previewd's split-sum precompute pipeline:
//   UpdateSplitSumTexture (0x14036daf0) — caller, hard-codes sampleCount=128
//     GenerateSplitSumTexture (0x140581250) — writes FloatImage.xy = (A,B)
//       IntegrateBRDF         (0x140581bc0) — per-pixel Monte Carlo
//     FloatImage::ConvertToBGRA (0x14057cbc0) — packs into BGRA32Pixel
//       FloatImage::Read      (0x14057cdc0) — .r=pixel.x, .g=pixel.y,
//                                             .b=pixel.z, .a=pixel.w, all
//                                             saturate()*255 truncated
//
// BGRA32Pixel is `{ uint8_t b, g, r, a }` in memory, so the bytes come out
// as { pixel.z*255, pixel.y*255, pixel.x*255, pixel.w*255 } = { 0, B, A, 0 }
// where A is the F0 scale and B is the F0 bias. Sampled via DXGI
// B8G8R8A8_UNORM the shader reads .r=A (F0 scale), .g=B (F0 bias).
// ============================================================================

#include "../../gfx/gfx.h"
#include <cstdint>
#include <vector>

namespace WhiteoutDex::ibl {

// Engine ships 128x128 with **128 samples** per texel (the `sampleCount = 128`
// literal at 0x14036dd12 in UpdateSplitSumTexture). Size is hard-asserted
// to 128x128 at 0x14036db2e / 0x14036dbce.
constexpr int kSplitSumSize    = 128;
constexpr int kSplitSumSamples = 128;

// CPU side: fill `outPixels` with (size * size) 32-bit BGRA texels — byte
// order matches the engine's BGRA32Pixel so the blob can be uploaded as
// DXGI_FORMAT_B8G8R8A8_UNORM and sampled with .r/.g yielding (A, B).
//   byte 0: 0            (blue  / pixel.z)
//   byte 1: B * 255      (green / pixel.y — F0 bias)
//   byte 2: A * 255      (red   / pixel.x — F0 scale)
//   byte 3: 0            (alpha / pixel.w)
void GenerateSplitSumLut(int size, int sampleCount,
                         std::vector<uint8_t>& outPixels);

// Convenience: generate the default 128x128 LUT and upload as a GPU texture.
// Caller owns the returned handle (Destroy at shutdown).
gfx::TextureHandle CreateSplitSumLutTexture(gfx::IGFXDevice& gfx);

} // namespace WhiteoutDex::ibl
