// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
/**
 * @file ManagedWrapper.cpp
 * @brief C++/CLI .NET wrapper — bridges native WhiteoutDexTextureBrowser modules to MaxScript.
 *
 * Compile with:  /clr /std:c++20
 */

#include "ManagedWrapper.h"
#include "MPQReader.h"
#include "SharedPool.h"
#include "BLPDecoder.h"
#include "DDSDecoder.h"
#include "ThumbnailEngine.h"

// WHITEOUT_HAS_CASC must be defined before including the CASC header.
// CMake sets this via target properties, but the vcxproj compiles separately.
#ifndef WHITEOUT_HAS_CASC
#define WHITEOUT_HAS_CASC 1
#endif
#include <whiteout/storages/casc/storage.h>

#include <filesystem>
#include <fstream>

#include <msclr/marshal_cppstd.h>
#include <vcclr.h>

using namespace System;
using namespace System::Drawing;
using namespace System::Drawing::Imaging;
using namespace System::Runtime::InteropServices;
using namespace msclr::interop;

namespace WhiteoutDex {

// ============================================================================
//  Native singletons — IMPROVED: Meyer's singletons with static storage.
//  No manual new/delete, no memory leaks when the DLL is unloaded.
//  Thread-safe initialization guaranteed by C++11.
// ============================================================================

static whiteoutdex::MPQManager& getMpqManager() {
    static whiteoutdex::MPQManager instance;
    return instance;
}

// ============================================================================
//  CASC state — wraps WhiteoutLib's casc::Storage and adds
//  search, HD/SD tag detection, and shader type detection.
//  Replaces the old whiteoutdex::CASCReader (1400 lines → ~100 lines).
// ============================================================================

struct CascState {
    std::optional<whiteout::storages::casc::Storage> storage;
    std::vector<std::string> cachedFileList;  // sorted, built on open
    bool initialized = false;

    // ── HD/SD tag detection (path-based) ─────────────────────────────
    //  HD models live under _hd.w3mod:  war3.w3mod/_hd.w3mod/units/...
    //  SD models don't have it:         war3.w3mod/units/...
    static std::string detectTag(const std::string& path) {
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("_hd.w3mod") != std::string::npos)
            return "HD";
        return "SD";
    }

    // ── Search (substring match, case-insensitive) ───────────────────
    std::vector<std::string> searchFiles(const std::string& pattern) const {
        std::string lp = pattern;
        std::transform(lp.begin(), lp.end(), lp.begin(), ::tolower);
        std::vector<std::string> out;
        for (const auto& f : cachedFileList) {
            std::string fl = f;
            std::transform(fl.begin(), fl.end(), fl.begin(), ::tolower);
            if (fl.find(lp) != std::string::npos) out.push_back(f);
        }
        return out;
    }

    std::vector<std::string> searchFilesHD(const std::string& pattern) const {
        std::string lp = pattern;
        std::transform(lp.begin(), lp.end(), lp.begin(), ::tolower);
        std::vector<std::string> out;
        for (const auto& f : cachedFileList) {
            std::string fl = f;
            std::transform(fl.begin(), fl.end(), fl.begin(), ::tolower);
            if (fl.find(lp) != std::string::npos && detectTag(f) == "HD")
                out.push_back(f);
        }
        return out;
    }

    std::vector<std::string> searchFilesSD(const std::string& pattern) const {
        std::string lp = pattern;
        std::transform(lp.begin(), lp.end(), lp.begin(), ::tolower);
        std::vector<std::string> out;
        for (const auto& f : cachedFileList) {
            std::string fl = f;
            std::transform(fl.begin(), fl.end(), fl.begin(), ::tolower);
            if (fl.find(lp) != std::string::npos && detectTag(f) != "HD")
                out.push_back(f);
        }
        return out;
    }

    // ── MDX shader type detection ────────────────────────────────────
    static std::string detectShaderType(const uint8_t* mdx, size_t len) {
        if (!mdx || len < 20) return "?";
        if (mdx[0]!='M'||mdx[1]!='D'||mdx[2]!='L'||mdx[3]!='X') return "?";

        auto rle32 = [](const uint8_t* d, int p) -> int32_t {
            return static_cast<int32_t>(d[p])
                 | (static_cast<int32_t>(d[p+1]) << 8)
                 | (static_cast<int32_t>(d[p+2]) << 16)
                 | (static_cast<int32_t>(d[p+3]) << 24);
        };

        for (int p = 4; p < static_cast<int>(len) - 12; ++p) {
            if (mdx[p]=='M'&&mdx[p+1]=='T'&&mdx[p+2]=='L'&&mdx[p+3]=='S') {
                int mtlsSize = rle32(mdx, p+4);
                int mpos = p + 8;
                int mend = std::min(mpos + mtlsSize, static_cast<int>(len));
                while (mpos + 12 <= mend) {
                    int matSize  = rle32(mdx, mpos);
                    if (matSize < 12 || matSize > 100000) break;
                    int shaderId = rle32(mdx, mpos + 8);
                    if (shaderId > 0) return "HD";
                    mpos += matSize;
                }
                return "SD";
            }
            if (p + 8 <= static_cast<int>(len)) {
                int cs = rle32(mdx, p+4);
                if (cs > 0 && cs < static_cast<int>(len) && p + 8 + cs <= static_cast<int>(len))
                    p += 7 + cs;
            }
        }
        return "?";
    }

    void reset() {
        storage.reset();
        cachedFileList.clear();
        initialized = false;
    }
};

static CascState& getCascState() {
    static CascState instance;
    return instance;
}

static whiteoutdex::BLPDecoder& getBlpDecoder() {
    static whiteoutdex::BLPDecoder instance;
    return instance;
}

static whiteoutdex::DDSDecoder& getDdsDecoder() {
    static whiteoutdex::DDSDecoder instance;
    return instance;
}

static whiteoutdex::ThumbnailEngine& getThumbEngine() {
    static whiteoutdex::ThumbnailEngine instance;
    return instance;
}

// ============================================================================
//  Marshalling helpers
// ============================================================================

static std::string toNative(System::String^ s) {
    if (s == nullptr) return "";
    return marshal_as<std::string>(s);
}

static System::String^ toManaged(const std::string& s) {
    return gcnew System::String(s.c_str());
}

static array<System::String^>^ toManagedArray(const std::vector<std::string>& v) {
    array<System::String^>^ arr = gcnew array<System::String^>(
        static_cast<int>(v.size()));
    for (int i = 0; i < static_cast<int>(v.size()); ++i)
        arr[i] = toManaged(v[i]);
    return arr;
}

static std::vector<std::string> toNativeVector(array<System::String^>^ arr) {
    std::vector<std::string> v;
    if (arr == nullptr) return v;
    v.reserve(arr->Length);
    for (int i = 0; i < arr->Length; ++i)
        v.push_back(toNative(arr[i]));
    return v;
}

static array<System::Byte>^ toManagedBytes(const std::vector<uint8_t>& v) {
    if (v.empty()) return nullptr;
    array<System::Byte>^ arr = gcnew array<System::Byte>(
        static_cast<int>(v.size()));
    pin_ptr<System::Byte> pin = &arr[0];
    memcpy(pin, v.data(), v.size());
    return arr;
}

static System::Drawing::Bitmap^ toBitmap(const whiteoutdex::DecodedImage& img) {
    if (!img.valid()) return nullptr;

    Bitmap^ bmp = gcnew Bitmap(img.width, img.height,
                                PixelFormat::Format32bppArgb);

    BitmapData^ bmpData = bmp->LockBits(
        System::Drawing::Rectangle(0, 0, img.width, img.height),
        ImageLockMode::WriteOnly,
        PixelFormat::Format32bppArgb);

    const int stride = bmpData->Stride;
    uint8_t* dstBase = static_cast<uint8_t*>(
        bmpData->Scan0.ToPointer());

    for (int y = 0; y < img.height; ++y) {
        const uint8_t* srcRow = img.scanline(y);
        uint8_t*       dstRow = dstBase + y * stride;

        for (int x = 0; x < img.width; ++x) {
            dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // B
            dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G
            dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // R
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3];  // A
        }
    }

    bmp->UnlockBits(bmpData);
    return bmp;
}

// ============================================================================
//  TGA Decoder — handles Type 2 (uncompressed) and Type 10 (RLE)
//  with 24-bit BGR and 32-bit BGRA pixel data.
//  System.Drawing doesn't support TGA, so we decode manually.
// ============================================================================

static System::Drawing::Bitmap^ tryDecodeTGA(array<System::Byte>^ data) {
    if (data == nullptr || data->Length < 18) return nullptr;

    // TGA Header (18 bytes)
    int idLen      = data[0];
    int cmapType   = data[1];
    int imgType    = data[2];   // 2=uncompressed RGB, 10=RLE RGB
    int width      = data[12] | (data[13] << 8);
    int height     = data[14] | (data[15] << 8);
    int bpp        = data[16];  // 24 or 32
    int descriptor = data[17];
    bool topDown   = (descriptor & 0x20) != 0;

    // Only support uncompressed (2) and RLE (10) true-color
    if (imgType != 2 && imgType != 10) return nullptr;
    if (bpp != 24 && bpp != 32) return nullptr;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return nullptr;
    if (cmapType != 0) return nullptr;  // no color-mapped support

    int bytesPerPixel = bpp / 8;
    int pixelStart = 18 + idLen;  // skip header + image ID

    if (pixelStart >= data->Length) return nullptr;

    auto bmp = gcnew Bitmap(width, height, PixelFormat::Format32bppArgb);
    auto bd = bmp->LockBits(
        System::Drawing::Rectangle(0, 0, width, height),
        ImageLockMode::WriteOnly, PixelFormat::Format32bppArgb);

    unsigned char* dst = static_cast<unsigned char*>(bd->Scan0.ToPointer());
    int stride = bd->Stride;

    if (imgType == 2) {
        // Uncompressed
        int srcIdx = pixelStart;
        for (int y = 0; y < height; y++) {
            int row = topDown ? y : (height - 1 - y);
            unsigned char* rowPtr = dst + row * stride;
            for (int x = 0; x < width; x++) {
                if (srcIdx + bytesPerPixel > data->Length) break;
                rowPtr[x*4+0] = data[srcIdx+0];  // B
                rowPtr[x*4+1] = data[srcIdx+1];  // G
                rowPtr[x*4+2] = data[srcIdx+2];  // R
                rowPtr[x*4+3] = (bpp == 32) ? data[srcIdx+3] : (unsigned char)255;
                srcIdx += bytesPerPixel;
            }
        }
    }
    else if (imgType == 10) {
        // RLE compressed
        int srcIdx = pixelStart;
        int pixelIdx = 0;
        int totalPixels = width * height;

        while (pixelIdx < totalPixels && srcIdx < data->Length) {
            unsigned char header = data[srcIdx++];
            int count = (header & 0x7F) + 1;

            if (header & 0x80) {
                // RLE packet: one pixel repeated
                if (srcIdx + bytesPerPixel > data->Length) break;
                unsigned char b = data[srcIdx+0];
                unsigned char g = data[srcIdx+1];
                unsigned char r = data[srcIdx+2];
                unsigned char a = (bpp == 32 && srcIdx+3 < data->Length) ? data[srcIdx+3] : (unsigned char)255;
                srcIdx += bytesPerPixel;

                for (int i = 0; i < count && pixelIdx < totalPixels; i++, pixelIdx++) {
                    int x = pixelIdx % width;
                    int y = pixelIdx / width;
                    int row = topDown ? y : (height - 1 - y);
                    unsigned char* p = dst + row * stride + x * 4;
                    p[0] = b; p[1] = g; p[2] = r; p[3] = a;
                }
            }
            else {
                // Raw packet: `count` pixels follow
                for (int i = 0; i < count && pixelIdx < totalPixels; i++, pixelIdx++) {
                    if (srcIdx + bytesPerPixel > data->Length) break;
                    int x = pixelIdx % width;
                    int y = pixelIdx / width;
                    int row = topDown ? y : (height - 1 - y);
                    unsigned char* p = dst + row * stride + x * 4;
                    p[0] = data[srcIdx+0];
                    p[1] = data[srcIdx+1];
                    p[2] = data[srcIdx+2];
                    p[3] = (bpp == 32) ? data[srcIdx+3] : (unsigned char)255;
                    srcIdx += bytesPerPixel;
                }
            }
        }
    }

    bmp->UnlockBits(bd);
    return bmp;
}

// ============================================================================
//  Standard image format helper (PNG, JPG, BMP, GIF, TIFF, TGA)
//  Uses System.Drawing.Bitmap for PNG/JPG/BMP, custom decoder for TGA.
// ============================================================================

static System::Drawing::Bitmap^ tryDecodeStandard(array<System::Byte>^ data) {
    if (data == nullptr || data->Length < 8) return nullptr;

    // Try System.Drawing first (PNG, JPG, BMP, GIF, TIFF)
    try {
        auto ms = gcnew System::IO::MemoryStream(data);
        auto bmp = gcnew Bitmap(ms);
        if (bmp->PixelFormat != PixelFormat::Format32bppArgb) {
            auto converted = gcnew Bitmap(bmp->Width, bmp->Height, PixelFormat::Format32bppArgb);
            auto g = Graphics::FromImage(converted);
            g->DrawImage(bmp, 0, 0, bmp->Width, bmp->Height);
            g->~Graphics();
            bmp->~Bitmap();
            return converted;
        }
        return bmp;
    }
    catch (...) {}

    // Fallback: try TGA decoder
    try {
        return tryDecodeTGA(data);
    }
    catch (...) {}

    return nullptr;
}

static System::Drawing::Bitmap^ resizeBitmap(Bitmap^ src, int w, int h) {
    if (src == nullptr) return nullptr;
    if (src->Width <= w && src->Height <= h) return src;
    auto dst = gcnew Bitmap(w, h, PixelFormat::Format32bppArgb);
    auto g = Graphics::FromImage(dst);
    g->InterpolationMode = System::Drawing::Drawing2D::InterpolationMode::HighQualityBilinear;
    g->DrawImage(src, 0, 0, w, h);
    g->~Graphics();
    return dst;
}

// ============================================================================
//  Thumbnail retry helper — for entries that failed native decode,
//  try System.Drawing (handles PNG, JPG, BMP, GIF, TIFF)
// ============================================================================

static void retryFailedWithStandardDecode(
    array<ThumbnailEntry^>^ out,
    const std::vector<std::string>& paths,
    const std::vector<std::vector<uint8_t>>& rawDataVec,
    int thumbSize)
{
    for (int i = 0; i < out->Length; ++i) {
        if (out[i]->Success) continue;
        if (i >= static_cast<int>(rawDataVec.size())) continue;
        if (rawDataVec[i].empty()) continue;

        auto managedBytes = toManagedBytes(rawDataVec[i]);
        auto bmp = tryDecodeStandard(managedBytes);
        if (bmp != nullptr) {
            out[i]->Thumbnail = resizeBitmap(bmp, thumbSize, thumbSize);
            out[i]->Success = true;
            out[i]->Error = "";
        }
    }
}

// ============================================================================
//  MPQ
// ============================================================================

int WhiteoutDexTextureBrowser::MPQ_Open(System::String^ path) {

    return getMpqManager().openArchive(toNative(path));
}

void WhiteoutDexTextureBrowser::MPQ_Close(int handle) {

    getMpqManager().closeArchive(handle);
}

void WhiteoutDexTextureBrowser::MPQ_CloseAll() {

    getMpqManager().closeAll();
}

int WhiteoutDexTextureBrowser::MPQ_Count() {

    return getMpqManager().archiveCount();
}

array<System::String^>^ WhiteoutDexTextureBrowser::MPQ_ListFiles(int handle) {
    return MPQ_ListFiles(handle, nullptr);
}

array<System::String^>^ WhiteoutDexTextureBrowser::MPQ_ListFiles(
    int handle, System::String^ filter)
{

    if (handle < 0 || handle >= getMpqManager().archiveCount())
        return gcnew array<System::String^>(0);
    if (!getMpqManager().isOpen(handle))
        return gcnew array<System::String^>(0);

    std::vector<std::string> paths;
    getMpqManager().listAllPaths(paths, filter ? toNative(filter) : "");
    return toManagedArray(paths);
}

array<System::String^>^ WhiteoutDexTextureBrowser::MPQ_ListAllFiles() {
    return MPQ_ListAllFiles(nullptr);
}

array<System::String^>^ WhiteoutDexTextureBrowser::MPQ_ListAllFiles(
    System::String^ filter)
{

    std::vector<std::string> paths;
    getMpqManager().listAllPaths(paths, filter ? toNative(filter) : "");
    return toManagedArray(paths);
}

array<System::Byte>^ WhiteoutDexTextureBrowser::MPQ_Extract(
    int handle, System::String^ innerPath)
{

    (void)handle;
    std::vector<uint8_t> data;
    whiteoutdex::MpqResult r = getMpqManager().extractFile(toNative(innerPath), data);
    if (r != whiteoutdex::MpqResult::Ok) return nullptr;
    return toManagedBytes(data);
}

bool WhiteoutDexTextureBrowser::MPQ_ExtractToDisk(
    int handle, System::String^ innerPath, System::String^ destPath)
{

    (void)handle;
    whiteoutdex::MpqResult r = getMpqManager().extractFileToDisk(
        toNative(innerPath), toNative(destPath));
    return (r == whiteoutdex::MpqResult::Ok);
}

bool WhiteoutDexTextureBrowser::MPQ_HasFile(int handle, System::String^ innerPath) {

    if (handle < 0 || handle >= getMpqManager().archiveCount()) return false;
    return getMpqManager().findArchiveContaining(toNative(innerPath)) >= 0;
}

// ============================================================================
//  CASC — now powered by WhiteoutLib's casc::Storage
//  Replaces ~1400 lines of custom CASCReader with Fernando's implementation.
// ============================================================================

System::String^ WhiteoutDexTextureBrowser::CASC_Open(System::String^ w3path) {
    auto& cs = getCascState();
    cs.reset();

    std::string path = toNative(w3path);
    std::string error;
    auto* pool = whiteoutdex::getSharedPool();

    cs.storage = whiteout::storages::casc::Storage::open(path, &error, pool);

    if (!cs.storage || !*cs.storage) {
        cs.initialized = false;
        return toManaged("ERROR: " + (error.empty() ? "Failed to open CASC storage" : error));
    }

    // Cache sorted file list
    cs.cachedFileList = cs.storage->listFiles();
    std::sort(cs.cachedFileList.begin(), cs.cachedFileList.end());

    auto fileCount = cs.storage->totalFileCount();
    cs.initialized = true;

    std::string log = "CASC initialized: " + path + "\n";
    log += "Files: " + std::to_string(fileCount.value_or(0)) + "\n";
    log += "=== Initialization complete ===\n";
    return toManaged(log);
}

void WhiteoutDexTextureBrowser::CASC_Close() {
    getCascState().reset();
}

bool WhiteoutDexTextureBrowser::CASC_IsOpen() {
    auto& cs = getCascState();
    return cs.initialized && cs.storage && *cs.storage;
}

array<System::String^>^ WhiteoutDexTextureBrowser::CASC_ListFiles() {
    return toManagedArray(getCascState().cachedFileList);
}

int WhiteoutDexTextureBrowser::CASC_FileCount() {
    auto& cs = getCascState();
    if (!cs.initialized || !cs.storage) return 0;
    return static_cast<int>(cs.storage->totalFileCount().value_or(0));
}

array<System::String^>^ WhiteoutDexTextureBrowser::CASC_SearchFiles(System::String^ pattern) {
    return toManagedArray(getCascState().searchFiles(toNative(pattern)));
}

array<System::String^>^ WhiteoutDexTextureBrowser::CASC_SearchFilesHD(System::String^ pattern) {
    return toManagedArray(getCascState().searchFilesHD(toNative(pattern)));
}

array<System::String^>^ WhiteoutDexTextureBrowser::CASC_SearchFilesSD(System::String^ pattern) {
    return toManagedArray(getCascState().searchFilesSD(toNative(pattern)));
}

System::String^ WhiteoutDexTextureBrowser::CASC_GetFileTag(System::String^ cascPath) {
    return toManaged(CascState::detectTag(toNative(cascPath)));
}

array<System::Byte>^ WhiteoutDexTextureBrowser::CASC_Extract(System::String^ cascPath) {
    auto& cs = getCascState();
    if (!cs.initialized || !cs.storage) return nullptr;

    auto result = cs.storage->readFile(toNative(cascPath));
    if (!result) return nullptr;
    return toManagedBytes(*result);
}

int WhiteoutDexTextureBrowser::CASC_ExtractToDisk(
    System::String^ cascPath, System::String^ outputPath)
{
    auto& cs = getCascState();
    if (!cs.initialized || !cs.storage) return -1;

    auto result = cs.storage->readFile(toNative(cascPath));
    if (!result) return -1;

    // Create parent directories
    try {
        std::filesystem::path dest(toNative(outputPath));
        if (dest.has_parent_path())
            std::filesystem::create_directories(dest.parent_path());
    } catch (...) { return -1; }

    std::ofstream ofs(toNative(outputPath), std::ios::binary);
    if (!ofs) return -1;
    ofs.write(reinterpret_cast<const char*>(result->data()),
              static_cast<std::streamsize>(result->size()));
    return ofs.good() ? static_cast<int>(result->size()) : -1;
}

System::String^ WhiteoutDexTextureBrowser::CASC_GetShaderType(System::String^ cascPath) {
    auto& cs = getCascState();
    if (!cs.initialized || !cs.storage) return toManaged("?");

    auto data = cs.storage->readFile(toNative(cascPath));
    if (!data || data->empty()) return toManaged("?");
    return toManaged(CascState::detectShaderType(data->data(), data->size()));
}

// ============================================================================
//  BLP / DDS Decoding
// ============================================================================

System::Drawing::Bitmap^ WhiteoutDexTextureBrowser::DecodeBLP(System::String^ filePath) {

    whiteoutdex::DecodedImage img = getBlpDecoder().decodeFile(toNative(filePath));
    return toBitmap(img);
}

System::Drawing::Bitmap^ WhiteoutDexTextureBrowser::DecodeBLPFromMemory(
    array<System::Byte>^ data)
{

    if (data == nullptr || data->Length == 0) return nullptr;
    pin_ptr<System::Byte> pin = &data[0];
    whiteoutdex::DecodedImage img = getBlpDecoder().decode(
        reinterpret_cast<const uint8_t*>(pin),
        static_cast<size_t>(data->Length));
    return toBitmap(img);
}

System::Drawing::Bitmap^ WhiteoutDexTextureBrowser::DecodeDDS(System::String^ filePath) {

    whiteoutdex::DecodedImage img = getDdsDecoder().decodeFile(toNative(filePath));
    return toBitmap(img);
}

System::Drawing::Bitmap^ WhiteoutDexTextureBrowser::DecodeDDSFromMemory(
    array<System::Byte>^ data)
{

    if (data == nullptr || data->Length == 0) return nullptr;
    pin_ptr<System::Byte> pin = &data[0];
    whiteoutdex::DecodedImage img = getDdsDecoder().decode(
        reinterpret_cast<const uint8_t*>(pin),
        static_cast<size_t>(data->Length));
    return toBitmap(img);
}

System::Drawing::Bitmap^ WhiteoutDexTextureBrowser::DecodeTexture(
    array<System::Byte>^ data)
{

    if (data == nullptr || data->Length < 4) return nullptr;
    pin_ptr<System::Byte> pin = &data[0];
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(pin);

    // BLP
    if (ptr[0] == 'B' && ptr[1] == 'L' && ptr[2] == 'P') {
        whiteoutdex::DecodedImage img = getBlpDecoder().decode(
            ptr, static_cast<size_t>(data->Length));
        return toBitmap(img);
    }
    // DDS
    if (ptr[0] == 'D' && ptr[1] == 'D' && ptr[2] == 'S') {
        whiteoutdex::DecodedImage img = getDdsDecoder().decode(
            ptr, static_cast<size_t>(data->Length));
        return toBitmap(img);
    }

    // Fallback: PNG, JPG, BMP, GIF, TIFF via System.Drawing
    return tryDecodeStandard(data);
}

// ============================================================================
//  Thumbnails
// ============================================================================

array<ThumbnailEntry^>^ WhiteoutDexTextureBrowser::GenerateThumbnailsMPQ(
    int mpqHandle, array<System::String^>^ fileList, int thumbSize)
{

    (void)mpqHandle;

    auto paths = toNativeVector(fileList);
    getThumbEngine().setThumbnailSize(thumbSize, thumbSize);

    auto results = getThumbEngine().generateFromMPQ(getMpqManager(), paths);

    array<ThumbnailEntry^>^ out = gcnew array<ThumbnailEntry^>(
        static_cast<int>(results.size()));

    // IMPROVED: Use raw bytes from native result directly instead of
    //           re-extracting.  The ThumbnailEngine now keeps raw data
    //           when native decode fails (JPG, PNG, TGA, etc.)
    std::vector<std::vector<uint8_t>> rawDataVec(results.size());
    bool hasFailures = false;

    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        ThumbnailEntry^ entry = gcnew ThumbnailEntry();
        entry->Path      = toManaged(results[i].path);
        entry->Success   = results[i].success;
        entry->Error     = toManaged(results[i].error);
        entry->Thumbnail = results[i].success
            ? toBitmap(results[i].thumbnail) : nullptr;
        out[i] = entry;

        if (!results[i].success && !results[i].rawData.empty()) {
            rawDataVec[i] = std::move(results[i].rawData);
            hasFailures = true;
        }
    }

    // Retry failed entries with System.Drawing (PNG, JPG, BMP, etc.)
    if (hasFailures)
        retryFailedWithStandardDecode(out, paths, rawDataVec, thumbSize);

    return out;
}

array<ThumbnailEntry^>^ WhiteoutDexTextureBrowser::GenerateThumbnailsMPQAll(
    array<System::String^>^ fileList, int thumbSize)
{
    return GenerateThumbnailsMPQ(0, fileList, thumbSize);
}

array<ThumbnailEntry^>^ WhiteoutDexTextureBrowser::GenerateThumbnailsCASC(
    array<System::String^>^ fileList, int thumbSize)
{
    auto paths = toNativeVector(fileList);
    getThumbEngine().setThumbnailSize(thumbSize, thumbSize);

    auto results = getThumbEngine().generateFromCASC(*getCascState().storage, paths);

    array<ThumbnailEntry^>^ out = gcnew array<ThumbnailEntry^>(
        static_cast<int>(results.size()));

    // IMPROVED: Use raw bytes from native result directly
    std::vector<std::vector<uint8_t>> rawDataVec(results.size());
    bool hasFailures = false;

    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        ThumbnailEntry^ entry = gcnew ThumbnailEntry();
        entry->Path      = toManaged(results[i].path);
        entry->Success   = results[i].success;
        entry->Error     = toManaged(results[i].error);
        entry->Thumbnail = results[i].success
            ? toBitmap(results[i].thumbnail) : nullptr;
        out[i] = entry;

        if (!results[i].success && !results[i].rawData.empty()) {
            rawDataVec[i] = std::move(results[i].rawData);
            hasFailures = true;
        }
    }

    if (hasFailures)
        retryFailedWithStandardDecode(out, paths, rawDataVec, thumbSize);

    return out;
}

array<ThumbnailEntry^>^ WhiteoutDexTextureBrowser::GenerateThumbnailsDisk(
    array<System::String^>^ filePaths, int thumbSize)
{
    auto paths = toNativeVector(filePaths);
    getThumbEngine().setThumbnailSize(thumbSize, thumbSize);

    auto results = getThumbEngine().generateFromDisk(paths);

    array<ThumbnailEntry^>^ out = gcnew array<ThumbnailEntry^>(
        static_cast<int>(results.size()));

    // IMPROVED: Disk thumbnails now also retry with System.Drawing
    std::vector<std::vector<uint8_t>> rawDataVec(results.size());
    bool hasFailures = false;

    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        ThumbnailEntry^ entry = gcnew ThumbnailEntry();
        entry->Path      = toManaged(results[i].path);
        entry->Success   = results[i].success;
        entry->Error     = toManaged(results[i].error);
        entry->Thumbnail = results[i].success
            ? toBitmap(results[i].thumbnail) : nullptr;
        out[i] = entry;

        if (!results[i].success && !results[i].rawData.empty()) {
            rawDataVec[i] = std::move(results[i].rawData);
            hasFailures = true;
        }
    }

    if (hasFailures)
        retryFailedWithStandardDecode(out, paths, rawDataVec, thumbSize);

    return out;
}

// ============================================================================
//  Thread configuration
// ============================================================================

void WhiteoutDexTextureBrowser::SetThreadCount(int threads) {

    getThumbEngine().setThreadCount(threads);
}

int WhiteoutDexTextureBrowser::GetThreadCount() {

    return getThumbEngine().threadCount();
}

} // namespace WhiteoutDex
