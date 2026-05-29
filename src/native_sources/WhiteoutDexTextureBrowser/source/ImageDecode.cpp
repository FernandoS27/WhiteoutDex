// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow

#include "ImageDecode.h"
#include "SharedPool.h"

#include <whiteout/textures/blp/blp.h>
#include <whiteout/textures/dds/dds.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>

namespace wt  = whiteout::textures;
namespace blp = whiteout::textures::blp;
namespace dds = whiteout::textures::dds;

namespace whiteoutdex {
namespace {

bool detectAlpha(const uint8_t* rgba, int width, int height) {
    const size_t pixels = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixels; ++i) {
        if (rgba[i * 4 + 3] != 255) return true;
    }
    return false;
}

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

DecodedImage textureToDecoded(std::optional<wt::Texture>& texture) {
    if (!texture) return {};

    if (texture->format() != wt::PixelFormat::RGBA8) {
        *texture = texture->copyAsFormat(wt::PixelFormat::RGBA8, getSharedPool());
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

} // namespace

DecodedImage decodeBlp(std::span<const uint8_t> data) {
    if (data.size() < 4) return {};
    blp::Parser parser;
    auto texture = parser.parse(
        std::span<const whiteout::u8>{data.data(), data.size()});
    return textureToDecoded(texture);
}

DecodedImage decodeDds(std::span<const uint8_t> data) {
    if (data.size() < 4) return {};
    dds::Parser parser;
    auto texture = parser.parse(
        std::span<const whiteout::u8>{data.data(), data.size()});
    return textureToDecoded(texture);
}

DecodedImage decodeAuto(std::span<const uint8_t> data) {
    if (data.size() < 4) return {};
    if (data[0] == 'B' && data[1] == 'L' && data[2] == 'P' &&
        (data[3] == '1' || data[3] == '2'))
        return decodeBlp(data);
    if (data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
        return decodeDds(data);
    return {};
}

DecodedImage decodeBlpFile(const std::string& path) {
    auto bytes = readFileBytes(path);
    return bytes.empty() ? DecodedImage{} : decodeBlp(bytes);
}

DecodedImage decodeDdsFile(const std::string& path) {
    auto bytes = readFileBytes(path);
    return bytes.empty() ? DecodedImage{} : decodeDds(bytes);
}

// Box-filter (area-average) downsample. Box filter is plenty for 64x64
// thumbnails and handles non-power-of-two and non-square src/dst.
DecodedImage boxResize(const DecodedImage& src, int dstW, int dstH) {
    if (dstW <= 0 || dstH <= 0 || !src.valid()) return {};

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

} // namespace whiteoutdex
