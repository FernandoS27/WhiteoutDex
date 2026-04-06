// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

#include "BLPDecoder.h"   // shares DecodedImage

#include <cstdint>
#include <string>
#include <vector>
#include <span>

namespace whiteoutdex {

// ============================================================================
//  DDSFormat  —  detected pixel format of a DDS file
// ============================================================================
enum class DDSFormat : int {
    Unknown   = 0,
    DXT1      = 1,   // BC1 — 4 bpp, 0/1-bit alpha
    DXT3      = 3,   // BC2 — 8 bpp, explicit 4-bit alpha
    DXT5      = 5,   // BC3 — 8 bpp, interpolated alpha
    BC4       = 6,   // BC4/ATI1 — 4 bpp, single channel (alpha/height)
    BC7       = 7,   // BC7/BPTC — 8 bpp, high quality RGBA
    BC5       = 8,   // BC5/ATI2 — 8 bpp, two channel (normal maps RG)
    BC6H      = 9,   // BC6H — HDR format (new via WhiteoutLib)
    BGRA8     = 10,  // 32-bit B8G8R8A8
    RGBA8     = 11,  // 32-bit R8G8B8A8
    BGR8      = 12,  // 24-bit B8G8R8 (no alpha)
    RGB8      = 13,  // 24-bit R8G8B8 (no alpha)
    BGRA16    = 14,  // 16-bit B5G6R5 or B5G5R5A1 (rare in WC3)
};

// ============================================================================
//  DDSDecoder
//
//  DDS decoder wrapping WhiteoutLib's dds::Parser + Texture::copyAsFormat().
//  Same pattern as BLPDecoder — all BCn block decoding is handled by
//  WhiteoutLib (BC1–BC7, BC6H), multithreaded via shared WorkerPool.
//
//  Supports:
//    - All BCn formats (BC1–BC7, BC6H) via WhiteoutLib
//    - DX10 extended header (DXGI formats)
//    - Uncompressed RGBA/BGRA/RGB/BGR
//    - sRGB detection
//    - Cubemaps and volume textures
//
//  Thread safety:
//    All decode methods are safe to call from multiple threads.
// ============================================================================
class DDSDecoder {
public:
    DDSDecoder()  = default;
    ~DDSDecoder() = default;

    DecodedImage decode(const uint8_t* data, size_t length) const;
    DecodedImage decode(const std::vector<uint8_t>& data) const;
    DecodedImage decode(std::span<const uint8_t> data) const;

    DecodedImage decodeFile(const std::string& path) const;

    DecodedImage decodeThumbnail(const uint8_t* data, size_t length,
                                 int thumbW = 64, int thumbH = 64) const;
    DecodedImage decodeThumbnail(const std::vector<uint8_t>& data,
                                 int thumbW = 64, int thumbH = 64) const;

    struct DDSInfo {
        int       width      = 0;
        int       height     = 0;
        int       mipLevels  = 0;
        DDSFormat format     = DDSFormat::Unknown;
        bool      valid      = false;
    };
    static DDSInfo getInfo(const uint8_t* data, size_t length);
    static DDSInfo getInfo(const std::vector<uint8_t>& data);

private:
    static DecodedImage boxResize(const DecodedImage& src, int dstW, int dstH);
    static bool detectAlpha(const uint8_t* rgba, int width, int height);
};

} // namespace whiteoutdex
