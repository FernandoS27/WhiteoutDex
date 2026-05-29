// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
/**
 * @file blpio.cpp
 * @brief BLP Bitmap I/O implementation for 3ds Max 2025.
 *
 * Multithreaded: uses WhiteoutLib's SimpleThreadPool for parallel
 * BCn decode/encode and palette quantization.
 */

#include "blpio.h"

#include <whiteout/textures/blp/blp.h>
#include <whiteout/utils/simple_thread_pool.h>

#include <fstream>
#include <vector>
#include <cstring>
#include <thread>
#include <mutex>

using namespace whiteout::textures;
using namespace whiteout::textures::blp;

// ===========================================================================
// Thread pool — created once, reused for all Load/Write calls
// ===========================================================================

namespace {

std::once_flag g_poolInitFlag;
std::unique_ptr<whiteout::utils::SimpleThreadPool> g_pool;

whiteout::interfaces::WorkerPool* getPool() {
    std::call_once(g_poolInitFlag, []() {
        // Use all available cores, minimum 2
        const size_t cores = std::max<size_t>(std::thread::hardware_concurrency(), 2);
        g_pool = std::make_unique<whiteout::utils::SimpleThreadPool>(cores);
    });
    return g_pool.get();
}

} // anonymous namespace

// ===========================================================================
// Helpers
// ===========================================================================

namespace {

std::vector<whiteout::u8> readFileBytes(const MCHAR* path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return {};
    const auto size = ifs.tellg();
    if (size <= 0) return {};
    std::vector<whiteout::u8> buf(static_cast<size_t>(size));
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

bool writeFileBytes(const MCHAR* path, const std::vector<whiteout::u8>& data) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return ofs.good();
}

void rgba8_to_bmm64(const whiteout::u8* src, BMM_Color_64* dst, int width) {
    for (int x = 0; x < width; ++x) {
        dst[x].r = static_cast<WORD>(src[x * 4 + 0]) * 257;
        dst[x].g = static_cast<WORD>(src[x * 4 + 1]) * 257;
        dst[x].b = static_cast<WORD>(src[x * 4 + 2]) * 257;
        dst[x].a = static_cast<WORD>(src[x * 4 + 3]) * 257;
    }
}

void bmm64_to_rgba8(const BMM_Color_64* src, whiteout::u8* dst, int width) {
    for (int x = 0; x < width; ++x) {
        dst[x * 4 + 0] = static_cast<whiteout::u8>(src[x].r >> 8);
        dst[x * 4 + 1] = static_cast<whiteout::u8>(src[x].g >> 8);
        dst[x * 4 + 2] = static_cast<whiteout::u8>(src[x].b >> 8);
        dst[x * 4 + 3] = static_cast<whiteout::u8>(src[x].a >> 8);
    }
}

bool texture_has_alpha(const Texture& tex) {
    auto data = tex.mipData(0);
    const auto pixels = static_cast<size_t>(tex.width()) * tex.height();
    for (size_t i = 0; i < pixels; ++i) {
        if (data[i * 4 + 3] != 255) return true;
    }
    return false;
}

BlpVersion mapVersion(int v) {
    return (v == 0) ? BlpVersion::BLP1 : BlpVersion::BLP2;
}

BlpEncoding mapEncoding(int e) {
    switch (e) {
    case 1:  return BlpEncoding::BGRA;
    case 2:  return BlpEncoding::DXT;
    case 3:  return BlpEncoding::Palettized;
    case 4:  return BlpEncoding::JPEG;
    default: return BlpEncoding::Infer;
    }
}

BlpAlphaDepth mapAlpha(int bits) {
    switch (bits) {
    case 1:  return BlpAlphaDepth::One;
    case 4:  return BlpAlphaDepth::Four;
    case 8:  return BlpAlphaDepth::Eight;
    default: return BlpAlphaDepth::Zero;
    }
}

} // anonymous namespace

// ===========================================================================
// Construction / defaults
// ===========================================================================

BlpBitmapIO::BlpBitmapIO() { initDefaults(); }
BlpBitmapIO::~BlpBitmapIO() = default;

void BlpBitmapIO::initDefaults() {
    std::memset(&userData_, 0, sizeof(userData_));
    userData_.structVersion  = BLP_USERDATA_VER;
    userData_.jpegQuality    = 75;
    userData_.alphaBits      = 8;
    userData_.encoding       = 0;
    userData_.blpVersion     = 0;
    userData_.dither         = 0;
    userData_.ditherStrength = 0.8f;
}

// ===========================================================================
// Identity
// ===========================================================================

const MCHAR* BlpBitmapIO::LongDesc()         { return _T("Blizzard BLP Texture (Warcraft III / WoW)"); }
const MCHAR* BlpBitmapIO::ShortDesc()        { return _T("BLP Texture"); }
const MCHAR* BlpBitmapIO::AuthorName()       { return _T("Fernando Sahmkow"); }
const MCHAR* BlpBitmapIO::CopyrightMessage() { return _T("(c) 2026 Fernando Sahmkow"); }
unsigned int BlpBitmapIO::Version()          { return 100; }

int          BlpBitmapIO::ExtCount()         { return 1; }
const MCHAR* BlpBitmapIO::Ext(int /*n*/)     { return _T("blp"); }

int BlpBitmapIO::Capability() {
    return BMMIO_READER | BMMIO_WRITER | BMMIO_EXTENSION | BMMIO_CONTROLWRITE;
}

// ===========================================================================
// About dialog
// ===========================================================================

void BlpBitmapIO::ShowAbout(HWND hWnd) {
    MessageBox(hWnd,
        _T("BLP Texture I/O Plugin v1.0\n\n")
        _T("Supports BLP1 (Warcraft III) and BLP2 (World of Warcraft)\n")
        _T("JPEG, Palettized, DXT, and BGRA encodings.\n\n")
        _T("Multithreaded via WhiteoutLib.\n\n")
        _T("Powered by WhiteoutLib"),
        _T("About BLP Texture Plugin"),
        MB_OK | MB_ICONINFORMATION);
}

// ===========================================================================
// GetImageInfo
// ===========================================================================

BMMRES BlpBitmapIO::GetImageInfo(BitmapInfo* bi) {
    auto fileData = readFileBytes(bi->Name());
    if (fileData.empty())
        return BMMRES_IOERROR;

    blp::Parser parser;
    auto texture = parser.parse(
        std::span<const whiteout::u8>{fileData.data(), fileData.size()});

    if (!texture)
        return BMMRES_INVALIDFORMAT;

    bi->SetWidth(static_cast<int>(texture->width()));
    bi->SetHeight(static_cast<int>(texture->height()));
    bi->SetType(BMM_TRUE_32);
    bi->SetAspect(1.0f);
    bi->SetFirstFrame(0);
    bi->SetLastFrame(0);
    bi->SetFlags(MAP_HAS_ALPHA);

    return BMMRES_SUCCESS;
}

// ===========================================================================
// Config persistence
// ===========================================================================

DWORD BlpBitmapIO::EvaluateConfigure() { return sizeof(BlpUserData); }

BOOL BlpBitmapIO::SaveConfigure(void* ptr) {
    if (!ptr) return FALSE;
    std::memcpy(ptr, &userData_, sizeof(BlpUserData));
    return TRUE;
}

BOOL BlpBitmapIO::LoadConfigure(void* ptr, DWORD piDataSize) {
    if (piDataSize >= sizeof(BlpUserData)) {
        std::memcpy(&userData_, ptr, sizeof(BlpUserData));
        return TRUE;
    }
    initDefaults();
    return FALSE;
}

BOOL BlpBitmapIO::ShowControl(HWND, DWORD) { return TRUE; }

// ===========================================================================
// LOAD — BLP file -> 3ds Max Bitmap (multithreaded)
// ===========================================================================

BitmapStorage* BlpBitmapIO::Load(BitmapInfo* pbi, Bitmap* pmap, BMMRES* status) {

    auto fileData = readFileBytes(pbi->Name());
    if (fileData.empty()) {
        *status = ProcessImageIOError(pbi);
        return nullptr;
    }

    blp::Parser parser;
    auto texture = parser.parse(
        std::span<const whiteout::u8>{fileData.data(), fileData.size()});

    if (!texture) {
        *status = ProcessImageIOError(pbi, _T("BLP parse error"));
        return nullptr;
    }

    // Always convert to RGBA8 — ensures the mip data is in a
    // normalized pixel layout regardless of BLP internal encoding.
    *texture = texture->copyAsFormat(PixelFormat::RGBA8, getPool());

    const int w = static_cast<int>(texture->width());
    const int h = static_cast<int>(texture->height());

    pbi->SetWidth(w);
    pbi->SetHeight(h);
    pbi->SetType(BMM_TRUE_32);
    pbi->SetAspect(1.0f);
    pbi->SetFirstFrame(0);
    pbi->SetLastFrame(0);

    const bool hasAlpha = texture_has_alpha(*texture);
    pbi->SetFlags(hasAlpha ? MAP_HAS_ALPHA : 0);

    BitmapStorage* storage = BMMCreateStorage(pmap->Manager(), BMM_TRUE_32);
    if (!storage) {
        *status = ProcessImageIOError(pbi, _T("Cannot create bitmap storage"));
        return nullptr;
    }

    if (!storage->Allocate(pbi, pmap->Manager(), BMM_OPEN_R)) {
        delete storage;
        *status = ProcessImageIOError(pbi, _T("Storage allocation failed"));
        return nullptr;
    }

    auto mipData = texture->mipData(0);
    std::vector<BMM_Color_64> scanline(w);

    for (int y = 0; y < h; ++y) {
        const whiteout::u8* row = mipData.data()
                                + static_cast<size_t>(y) * w * 4;
        rgba8_to_bmm64(row, scanline.data(), w);
        storage->PutPixels(0, y, w, scanline.data());
    }

    *status = BMMRES_SUCCESS;
    return storage;
}

// ===========================================================================
// SAVE — 3ds Max Bitmap -> BLP file (multithreaded)
// ===========================================================================

BMMRES BlpBitmapIO::OpenOutput(BitmapInfo* pbi, Bitmap* pmap) {
    if (!pmap) return BMMRES_IOERROR;
    outMap_ = pmap;
    outBi_  = pbi;
    return BMMRES_SUCCESS;
}

BMMRES BlpBitmapIO::Write(int /*frame*/) {
    if (!outMap_ || !outBi_) return BMMRES_IOERROR;

    const int w = outMap_->Width();
    const int h = outMap_->Height();
    if (w <= 0 || h <= 0) return BMMRES_IOERROR;

    std::vector<whiteout::u8> rgba(static_cast<size_t>(w) * h * 4);
    std::vector<BMM_Color_64> scanline(w);

    for (int y = 0; y < h; ++y) {
        if (!GetOutputPixels(0, y, w, scanline.data()))
            return BMMRES_IOERROR;
        whiteout::u8* row = rgba.data()
                          + static_cast<size_t>(y) * w * 4;
        bmm64_to_rgba8(scanline.data(), row, w);
    }

    Texture tex = Texture::create2D(
        PixelFormat::RGBA8,
        static_cast<whiteout::u32>(w),
        static_cast<whiteout::u32>(h),
        1);

    auto dst = tex.mipData(0);
    std::memcpy(dst.data(), rgba.data(), rgba.size());

    SaveOptions opts{};
    opts.version        = mapVersion(userData_.blpVersion);
    opts.encoding       = mapEncoding(userData_.encoding);
    opts.alpha          = mapAlpha(userData_.alphaBits);
    opts.jpegQuality    = userData_.jpegQuality;
    opts.dither         = (userData_.dither != 0);
    opts.ditherStrength = userData_.ditherStrength;

    // Writer uses thread pool for parallel palette quantization
    blp::Writer writer(getPool());
    auto blpData = writer.write(tex, opts);
    if (blpData.empty())
        return BMMRES_IOERROR;

    if (!writeFileBytes(outBi_->Name(), blpData))
        return BMMRES_IOERROR;

    return BMMRES_SUCCESS;
}

int BlpBitmapIO::Close(int /*flag*/) {
    outMap_ = nullptr;
    outBi_  = nullptr;
    return 1;
}
