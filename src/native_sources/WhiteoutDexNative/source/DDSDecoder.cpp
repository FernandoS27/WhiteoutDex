// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
/**
 * @file DDSDecoder.cpp
 * @brief DDS decoder wrapping WhiteoutLib's dds::Parser.
 *
 * Same pattern as BLPDecoder.cpp — all heavy lifting (BCn block decoding,
 * DX10 header parsing, format conversion) is done by WhiteoutLib.
 * This file is just a thin adapter to the DecodedImage API.
 *
 * Replaces ~500 lines of hand-written BCn decoders with ~80 lines of
 * wrapper code.  Full BC7 support (all 8 modes) + BC6H now included.
 *
 * Multithreaded: uses WhiteoutLib's shared WorkerPool for parallel
 * BCn block decoding via Texture::copyAsFormat().
 */

#include "DDSDecoder.h"
#include "SharedPool.h"

#include <whiteout/textures/dds/dds.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace wt  = whiteout::textures;
namespace dds = whiteout::textures::dds;

namespace whiteoutdex {

// ============================================================================
//  Pool access (shared with BLPDecoder, MPQReader, etc.)
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
//  Alpha detection
// ============================================================================

bool DDSDecoder::detectAlpha(const uint8_t* rgba, int width, int height) {
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
//    1.  dds::Parser::parse()  →  Texture  (may be BCn/uncompressed)
//    2.  Texture::copyAsFormat(RGBA8, pool)  →  RGBA8 Texture (threaded)
//    3.  Copy mip-level 0 into DecodedImage::pixels
// ============================================================================

DecodedImage DDSDecoder::decode(const uint8_t* data, size_t length) const {
    if (!data || length < 4) return {};

    dds::Parser parser(dds::Parser::ParseMode::Lenient);
    auto texture = parser.parse(
        std::span<const whiteout::u8>{data, length});

    if (!texture) return {};

    // Convert to RGBA8 if needed (BCn → RGBA8 via thread pool)
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

DecodedImage DDSDecoder::decode(const std::vector<uint8_t>& data) const {
    return decode(data.data(), data.size());
}

DecodedImage DDSDecoder::decode(std::span<const uint8_t> data) const {
    return decode(data.data(), data.size());
}

// ============================================================================
//  decodeFile
// ============================================================================

DecodedImage DDSDecoder::decodeFile(const std::string& path) const {
    auto bytes = readFileBytes(path);
    if (bytes.empty()) return {};
    return decode(bytes.data(), bytes.size());
}

// ============================================================================
//  decodeThumbnail  —  decode + box-filter resize
// ============================================================================

DecodedImage DDSDecoder::decodeThumbnail(const uint8_t* data, size_t length,
                                          int thumbW, int thumbH) const {
    DecodedImage full = decode(data, length);
    if (!full.valid()) return {};

    if (full.width <= thumbW && full.height <= thumbH) return full;

    return boxResize(full, thumbW, thumbH);
}

DecodedImage DDSDecoder::decodeThumbnail(const std::vector<uint8_t>& data,
                                          int thumbW, int thumbH) const {
    return decodeThumbnail(data.data(), data.size(), thumbW, thumbH);
}

// ============================================================================
//  boxResize  —  simple box-filter downsample (same as BLPDecoder)
// ============================================================================

DecodedImage DDSDecoder::boxResize(const DecodedImage& src,
                                    int dstW, int dstH) {
    if (dstW <= 0 || dstH <= 0) return {};

    DecodedImage dst;
    dst.width  = dstW;
    dst.height = dstH;
    dst.pixels.resize(static_cast<size_t>(dstW) * dstH * 4);

    const int srcW = src.width;
    const int srcH = src.height;

    for (int dy = 0; dy < dstH; ++dy) {
        const int sy0 = (dy * srcH) / dstH;
        const int sy1 = std::min(((dy + 1) * srcH) / dstH, srcH);
        const int rowCount = std::max(sy1 - sy0, 1);

        for (int dx = 0; dx < dstW; ++dx) {
            const int sx0 = (dx * srcW) / dstW;
            const int sx1 = std::min(((dx + 1) * srcW) / dstW, srcW);
            const int colCount = std::max(sx1 - sx0, 1);

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
//  getInfo  —  quick DDS header parse without full decode
// ============================================================================

DDSDecoder::DDSInfo DDSDecoder::getInfo(const uint8_t* data, size_t length) {
    DDSInfo info;
    if (!data || length < 128) return info;  // DDS header is 128 bytes minimum

    // Check magic "DDS "
    if (data[0] != 'D' || data[1] != 'D' || data[2] != 'S' || data[3] != ' ')
        return info;

    auto readU32 = [](const uint8_t* d, int p) -> uint32_t {
        return static_cast<uint32_t>(d[p])
             | (static_cast<uint32_t>(d[p+1]) << 8)
             | (static_cast<uint32_t>(d[p+2]) << 16)
             | (static_cast<uint32_t>(d[p+3]) << 24);
    };

    // DDS_HEADER starts at offset 4
    info.height    = static_cast<int>(readU32(data, 4 + 8));
    info.width     = static_cast<int>(readU32(data, 4 + 12));
    info.mipLevels = static_cast<int>(readU32(data, 4 + 24));
    if (info.mipLevels == 0) info.mipLevels = 1;

    // Detect format from pixel format flags
    uint32_t pfFlags = readU32(data, 4 + 76 + 4);
    uint32_t fourCC  = readU32(data, 4 + 76 + 8);

    constexpr uint32_t DDPF_FOURCC = 0x4;

    if (pfFlags & DDPF_FOURCC) {
        constexpr uint32_t DXT1 = 0x31545844u;  // "DXT1"
        constexpr uint32_t DXT3 = 0x33545844u;  // "DXT3"
        constexpr uint32_t DXT5 = 0x35545844u;  // "DXT5"
        constexpr uint32_t ATI1 = 0x31495441u;  // "ATI1"
        constexpr uint32_t ATI2 = 0x32495441u;  // "ATI2"
        constexpr uint32_t BC4U = 0x55344342u;  // "BC4U"
        constexpr uint32_t BC5U = 0x55354342u;  // "BC5U"
        constexpr uint32_t DX10 = 0x30315844u;  // "DX10"

        if      (fourCC == DXT1) info.format = DDSFormat::DXT1;
        else if (fourCC == DXT3) info.format = DDSFormat::DXT3;
        else if (fourCC == DXT5) info.format = DDSFormat::DXT5;
        else if (fourCC == ATI1 || fourCC == BC4U) info.format = DDSFormat::BC4;
        else if (fourCC == ATI2 || fourCC == BC5U) info.format = DDSFormat::BC5;
        else if (fourCC == DX10 && length >= 148) {
            // DX10 extended header at offset 128
            uint32_t dxgiFormat = readU32(data, 128);
            // DXGI_FORMAT_BC7_UNORM = 98, DXGI_FORMAT_BC7_UNORM_SRGB = 99
            if (dxgiFormat == 98 || dxgiFormat == 99)
                info.format = DDSFormat::BC7;
            // DXGI_FORMAT_BC6H_UF16 = 95, BC6H_SF16 = 96
            else if (dxgiFormat == 95 || dxgiFormat == 96)
                info.format = DDSFormat::BC6H;
            // DXGI_FORMAT_BC1_UNORM = 71, BC1_UNORM_SRGB = 72
            else if (dxgiFormat == 71 || dxgiFormat == 72)
                info.format = DDSFormat::DXT1;
            // DXGI_FORMAT_BC2_UNORM = 74, BC2_UNORM_SRGB = 75
            else if (dxgiFormat == 74 || dxgiFormat == 75)
                info.format = DDSFormat::DXT3;
            // DXGI_FORMAT_BC3_UNORM = 77, BC3_UNORM_SRGB = 78
            else if (dxgiFormat == 77 || dxgiFormat == 78)
                info.format = DDSFormat::DXT5;
            // DXGI_FORMAT_R8G8B8A8_UNORM = 28
            else if (dxgiFormat == 28 || dxgiFormat == 29)
                info.format = DDSFormat::RGBA8;
            // DXGI_FORMAT_B8G8R8A8_UNORM = 87
            else if (dxgiFormat == 87 || dxgiFormat == 91)
                info.format = DDSFormat::BGRA8;
        }
    } else {
        // Uncompressed — detect from bit count
        uint32_t rgbBits = readU32(data, 4 + 76 + 12);
        if (rgbBits == 32) info.format = DDSFormat::BGRA8;
        else if (rgbBits == 24) info.format = DDSFormat::BGR8;
        else if (rgbBits == 16) info.format = DDSFormat::BGRA16;
    }

    info.valid = (info.width > 0 && info.height > 0);
    return info;
}

DDSDecoder::DDSInfo DDSDecoder::getInfo(const std::vector<uint8_t>& data) {
    return getInfo(data.data(), data.size());
}

} // namespace whiteoutdex
