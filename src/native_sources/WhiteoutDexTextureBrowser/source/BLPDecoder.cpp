// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
/**
 * @file BLPDecoder.cpp
 * @brief Standalone BLP decoder wrapping WhiteoutLib.
 *
 * Extracted from blpio.cpp (the 3ds Max BLP plugin) — same decode logic,
 * but without any Max SDK dependency.  Can be used by ThumbnailEngine,
 * ManagedWrapper, or any other consumer.
 *
 * Multithreaded: uses WhiteoutLib's SimpleThreadPool for parallel BCn
 * block decoding, shared across all BLPDecoder instances.
 */

#include "BLPDecoder.h"
#include "SharedPool.h"    // IMPROVED: single global pool

#include <whiteout/textures/blp/blp.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

// Avoid "using namespace whiteout::textures" — it conflicts with
// whiteout::textures::blp (both have a Parser class).
// Use fully qualified names instead.
namespace wt  = whiteout::textures;
namespace blp = whiteout::textures::blp;

namespace whiteoutdex {

// ============================================================================
//  Shared thread pool — now uses global pool from SharedPool.h
// ============================================================================

namespace {

whiteout::interfaces::WorkerPool* getPool() {
    return whiteoutdex::getSharedPool();
}

} // anonymous namespace

// ============================================================================
//  File reading helper
// ============================================================================

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return {};
    const auto size = ifs.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

} // anonymous namespace

// ============================================================================
//  Alpha detection  (same as texture_has_alpha in blpio.cpp)
// ============================================================================

bool BLPDecoder::detectAlpha(const uint8_t* rgba, int width, int height) {
    const size_t pixels = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixels; ++i) {
        if (rgba[i * 4 + 3] != 255) return true;
    }
    return false;
}

// ============================================================================
//  decode  (from raw memory)
//
//  Pipeline:
//    1.  blp::Parser::parse()  →  Texture  (may be DXT/BC/Palette format)
//    2.  Texture::copyAsFormat(RGBA8, pool)  →  RGBA8 Texture (threaded)
//    3.  Copy mip-level 0 into DecodedImage::pixels
// ============================================================================

DecodedImage BLPDecoder::decode(const uint8_t* data, size_t length) const {
    if (!data || length < 4) return {};

    blp::Parser parser(blp::Parser::ParseMode::Lenient);
    auto texture = parser.parse(
        std::span<const whiteout::u8>{data, length});

    if (!texture) return {};

    // Convert to RGBA8 if needed (e.g. DXT/BC1-3, Palette, JPEG)
    // Uses thread pool for parallel BCn block decoding
    if (texture->format() != wt::PixelFormat::RGBA8) {
        *texture = texture->copyAsFormat(wt::PixelFormat::RGBA8, getPool());
    }

    const int w = static_cast<int>(texture->width());
    const int h = static_cast<int>(texture->height());
    if (w <= 0 || h <= 0) return {};

    auto mipData = texture->mipData(0);
    const size_t byteCount = static_cast<size_t>(w) * h * 4;

    DecodedImage img;
    img.width  = w;
    img.height = h;
    img.pixels.resize(byteCount);
    std::memcpy(img.pixels.data(), mipData.data(),
                std::min(byteCount, mipData.size()));
    img.hasAlpha = detectAlpha(img.pixels.data(), w, h);

    return img;
}

DecodedImage BLPDecoder::decode(const std::vector<uint8_t>& data) const {
    return decode(data.data(), data.size());
}

DecodedImage BLPDecoder::decode(std::span<const uint8_t> data) const {
    return decode(data.data(), data.size());
}

// ============================================================================
//  decodeFile
// ============================================================================

DecodedImage BLPDecoder::decodeFile(const std::string& path) const {
    auto bytes = readFileBytes(path);
    if (bytes.empty()) return {};
    return decode(bytes.data(), bytes.size());
}

// ============================================================================
//  decodeThumbnail  —  decode + box-filter resize
// ============================================================================

DecodedImage BLPDecoder::decodeThumbnail(const uint8_t* data, size_t length,
                                          int thumbW, int thumbH) const {
    DecodedImage full = decode(data, length);
    if (!full.valid()) return {};

    // If source is already at or below target size, return as-is
    if (full.width <= thumbW && full.height <= thumbH) return full;

    return boxResize(full, thumbW, thumbH);
}

DecodedImage BLPDecoder::decodeThumbnail(const std::vector<uint8_t>& data,
                                          int thumbW, int thumbH) const {
    return decodeThumbnail(data.data(), data.size(), thumbW, thumbH);
}

// ============================================================================
//  boxResize  —  simple box-filter (area-average) downsample
//
//  No fancy bicubic/Lanczos — for 64×64 thumbnails box-filter is plenty.
//  Handles non-power-of-two and non-square source/dest.
// ============================================================================

DecodedImage BLPDecoder::boxResize(const DecodedImage& src,
                                    int dstW, int dstH) {
    if (dstW <= 0 || dstH <= 0) return {};

    DecodedImage dst;
    dst.width  = dstW;
    dst.height = dstH;
    dst.pixels.resize(static_cast<size_t>(dstW) * dstH * 4);

    const int srcW = src.width;
    const int srcH = src.height;

    for (int dy = 0; dy < dstH; ++dy) {
        // Source row range for this destination row
        const int sy0 = (dy * srcH) / dstH;
        const int sy1 = std::min(((dy + 1) * srcH) / dstH, srcH);
        const int rowCount = std::max(sy1 - sy0, 1);

        for (int dx = 0; dx < dstW; ++dx) {
            // Source column range
            const int sx0 = (dx * srcW) / dstW;
            const int sx1 = std::min(((dx + 1) * srcW) / dstW, srcW);
            const int colCount = std::max(sx1 - sx0, 1);

            // Accumulate
            uint32_t rr = 0, gg = 0, bb = 0, aa = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const uint8_t* row = src.scanline(sy);
                for (int sx = sx0; sx < sx1; ++sx) {
                    rr += row[sx * 4 + 0];
                    gg += row[sx * 4 + 1];
                    bb += row[sx * 4 + 2];
                    aa += row[sx * 4 + 3];
                }
            }

            const uint32_t count = static_cast<uint32_t>(rowCount * colCount);
            uint8_t* out = dst.pixels.data()
                         + (static_cast<size_t>(dy) * dstW + dx) * 4;
            out[0] = static_cast<uint8_t>(rr / count);
            out[1] = static_cast<uint8_t>(gg / count);
            out[2] = static_cast<uint8_t>(bb / count);
            out[3] = static_cast<uint8_t>(aa / count);
        }
    }

    dst.hasAlpha = detectAlpha(dst.pixels.data(), dstW, dstH);
    return dst;
}

// ============================================================================
//  getInfo  —  quick BLP header parse without full decode
//
//  BLP1: magic "BLP1", offset 4 = compression, 8 = alphaDepth,
//        12 = width, 16 = height
//  BLP2: magic "BLP2", offset 4 = version(1), 5 = encoding, 6 = alphaBits,
//        12 = width, 16 = height
// ============================================================================

BLPDecoder::BLPInfo BLPDecoder::getInfo(const uint8_t* data, size_t length) {
    BLPInfo info;
    if (!data || length < 20) return info;

    // Check magic
    if (data[0] == 'B' && data[1] == 'L' && data[2] == 'P') {
        if (data[3] == '1') {
            info.blpVersion = 1;
        } else if (data[3] == '2') {
            info.blpVersion = 2;
        } else {
            return info;
        }
    } else {
        return info;
    }

    // Width and height at offset 12 and 16 (little-endian uint32)
    auto readU32 = [](const uint8_t* d, int p) -> uint32_t {
        return static_cast<uint32_t>(d[p])
             | (static_cast<uint32_t>(d[p+1]) << 8)
             | (static_cast<uint32_t>(d[p+2]) << 16)
             | (static_cast<uint32_t>(d[p+3]) << 24);
    };

    info.width  = static_cast<int>(readU32(data, 12));
    info.height = static_cast<int>(readU32(data, 16));

    // Count mip levels from the mip offset table
    // BLP1: offsets at 28..91 (16 entries × 4 bytes)
    // BLP2: offsets at 28..91 (same layout)
    if (length >= 92) {
        info.mipLevels = 0;
        for (int i = 0; i < 16; ++i) {
            uint32_t offset = readU32(data, 28 + i * 4);
            if (offset == 0) break;
            ++info.mipLevels;
        }
    } else {
        info.mipLevels = 1;
    }

    info.valid = (info.width > 0 && info.height > 0);
    return info;
}

BLPDecoder::BLPInfo BLPDecoder::getInfo(const std::vector<uint8_t>& data) {
    return getInfo(data.data(), data.size());
}

} // namespace whiteoutdex
