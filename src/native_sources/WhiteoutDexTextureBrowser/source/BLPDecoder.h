// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>

namespace whiteoutdex {

// ============================================================================
//  DecodedImage  —  simple container for raw RGBA8 pixel data
//
//  Pixel layout: row-major, top-to-bottom, 4 bytes per pixel (R,G,B,A).
//  Total size of pixels: width * height * 4 bytes.
// ============================================================================
struct DecodedImage {
    int                  width  = 0;
    int                  height = 0;
    bool                 hasAlpha = false;
    std::vector<uint8_t> pixels;       // RGBA8, length = width*height*4

    bool valid() const noexcept { return width > 0 && height > 0 && !pixels.empty(); }

    // Convenience: get a single scanline
    const uint8_t* scanline(int y) const noexcept {
        return pixels.data() + static_cast<size_t>(y) * width * 4;
    }
    uint8_t* scanline(int y) noexcept {
        return pixels.data() + static_cast<size_t>(y) * width * 4;
    }
};

// ============================================================================
//  BLPDecoder
//
//  Standalone BLP decoder — no Max SDK dependency.
//  Wraps WhiteoutLib's blp::Parser + Texture::copyAsFormat().
//
//  Supports:
//    - BLP1 (Warcraft III classic) and BLP2 (WoW / Reforged)
//    - JPEG-compressed, Palette-based, DXT1/3/5, BGRA
//    - Multithreaded BCn decompression via shared thread pool
//
//  Thread safety:
//    All decode methods are safe to call from multiple threads concurrently.
//    The underlying thread pool is created once and shared.
// ============================================================================
class BLPDecoder {
public:
    BLPDecoder()  = default;
    ~BLPDecoder() = default;

    // ── Decode from memory ───────────────────────────────────────────────────
    // Returns a DecodedImage with RGBA8 pixels.
    // On failure, returns an invalid DecodedImage (width/height == 0).
    DecodedImage decode(const uint8_t* data, size_t length) const;
    DecodedImage decode(const std::vector<uint8_t>& data) const;
    DecodedImage decode(std::span<const uint8_t> data) const;

    // ── Decode from file ─────────────────────────────────────────────────────
    DecodedImage decodeFile(const std::string& path) const;

    // ── Decode + resize (for thumbnails) ─────────────────────────────────────
    // Decodes the BLP and then box-filters down to thumbW × thumbH.
    // If the source is smaller than the target, it is returned as-is.
    DecodedImage decodeThumbnail(const uint8_t* data, size_t length,
                                 int thumbW = 64, int thumbH = 64) const;
    DecodedImage decodeThumbnail(const std::vector<uint8_t>& data,
                                 int thumbW = 64, int thumbH = 64) const;

    // ── Query BLP info without full decode ───────────────────────────────────
    struct BLPInfo {
        int  width       = 0;
        int  height      = 0;
        int  mipLevels   = 0;
        int  blpVersion  = 0;   // 1 = BLP1, 2 = BLP2
        bool valid       = false;
    };
    static BLPInfo getInfo(const uint8_t* data, size_t length);
    static BLPInfo getInfo(const std::vector<uint8_t>& data);

private:
    // Box-filter downsample RGBA8 image
    static DecodedImage boxResize(const DecodedImage& src, int dstW, int dstH);

    // Alpha detection (same logic as blpio.cpp)
    static bool detectAlpha(const uint8_t* rgba, int width, int height);
};

} // namespace whiteoutdex
