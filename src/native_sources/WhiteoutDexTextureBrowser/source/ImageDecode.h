// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

// Thin glue around WhiteoutLib's BLP/DDS parsers + Texture::copyAsFormat.
// All actual decoding (BCn blocks, DX10 headers, JPEG, palettized BLP, etc.)
// lives in the library — this header just adapts to a flat RGBA8 container
// that the texture browser, ThumbnailEngine, and managed wrapper consume.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace whiteoutdex {

// ============================================================================
//  DecodedImage  —  RGBA8, row-major, top-to-bottom, 4 bytes per pixel.
// ============================================================================
struct DecodedImage {
    int                  width    = 0;
    int                  height   = 0;
    bool                 hasAlpha = false;
    std::vector<uint8_t> pixels;       // length = width * height * 4

    bool valid() const noexcept { return width > 0 && height > 0 && !pixels.empty(); }

    const uint8_t* scanline(int y) const noexcept {
        return pixels.data() + static_cast<size_t>(y) * width * 4;
    }
    uint8_t* scanline(int y) noexcept {
        return pixels.data() + static_cast<size_t>(y) * width * 4;
    }
};

// ── Single-format decoders ──────────────────────────────────────────────────
DecodedImage decodeBlp(std::span<const uint8_t> data);
DecodedImage decodeDds(std::span<const uint8_t> data);

// ── Auto-detect (BLP/DDS by magic bytes) ────────────────────────────────────
DecodedImage decodeAuto(std::span<const uint8_t> data);

// ── File helpers ────────────────────────────────────────────────────────────
DecodedImage decodeBlpFile(const std::string& path);
DecodedImage decodeDdsFile(const std::string& path);

// ── Box-filter downsample (area-average); used for thumbnail generation ────
DecodedImage boxResize(const DecodedImage& src, int dstW, int dstH);

} // namespace whiteoutdex
