// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

#include "ImageDecode.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

// Forward-declare WhiteoutLib storages (replace old MPQReader/CASCReader)
namespace whiteout::storages::casc { class Storage; }
namespace whiteout::storages::mpq  { class Storage; }

namespace whiteoutdex {

// ============================================================================
//  ThumbnailResult  —  one decoded thumbnail
// ============================================================================
struct ThumbnailResult {
    std::string  path;                // original archive path
    DecodedImage thumbnail;           // RGBA8, resized to requested size
    bool         success  = false;
    std::string  error;               // error message if !success
    std::vector<uint8_t> rawData;     // IMPROVED: raw bytes kept on native decode failure
                                      // so managed layer can retry with System.Drawing
                                      // (handles JPG, PNG, BMP, GIF, TGA)
};

// ============================================================================
//  ThumbnailRequest  —  what the engine needs to process
// ============================================================================
struct ThumbnailRequest {
    std::vector<std::string> paths;   // files to thumbnail
    int  thumbWidth  = 64;
    int  thumbHeight = 64;
    int  maxThreads  = 0;             // 0 = auto (hardware_concurrency)
};

// ============================================================================
//  ThumbnailProgress  —  callback for progress reporting
// ============================================================================
using ThumbnailProgressFn = std::function<void(int completed, int total)>;

// ============================================================================
//  ThumbnailEngine
//
//  Batch thumbnail generator for textures inside MPQ/CASC archives.
//
//  Pipeline per file:
//    1. Extract raw bytes from archive (MPQ or CASC)
//    2. Detect format (BLP or DDS by magic bytes)
//    3. Decode to RGBA8
//    4. Box-filter resize to thumbnail dimensions
//
//  Multithreading:
//    Files are distributed across N worker threads (default = CPU cores).
//    Each worker runs the full extract→decode→resize pipeline for its
//    assigned files.  No shared mutable state between workers.
//
//    The underlying mpq::Storage and casc::Storage are thread-safe for
//    concurrent reads. The decoders in ImageDecode.h are stateless and
//    safe for concurrent use.
//
//  Usage from MaxScript (via ManagedWrapper):
//    local engine = WhiteoutDexTextureBrowser.CreateThumbnailEngine()
//    local results = engine.GenerateFromMPQ(mpqHandle, fileList, 64, 64)
//    -- results is array of (path, bitmap) pairs
//
//  Thread safety:
//    generateFromMPQ() / generateFromCASC() — one call at a time per
//    ThumbnailEngine instance.  Multiple instances can run concurrently.
// ============================================================================
class ThumbnailEngine {
public:
    ThumbnailEngine();
    ~ThumbnailEngine() = default;

    ThumbnailEngine(const ThumbnailEngine&)            = delete;
    ThumbnailEngine& operator=(const ThumbnailEngine&) = delete;

    // ── Configuration ────────────────────────────────────────────────────────
    void setThreadCount(int threads);   // 0 = auto
    int  threadCount() const noexcept { return threadCount_; }

    void setThumbnailSize(int width, int height);
    int  thumbWidth()  const noexcept { return thumbW_; }
    int  thumbHeight() const noexcept { return thumbH_; }

    // ── Generate from MPQ (searches archives in order — first match wins) ──
    std::vector<ThumbnailResult> generateFromMPQ(
        const std::vector<whiteout::storages::mpq::Storage>& archives,
        const std::vector<std::string>& paths,
        ThumbnailProgressFn progress = nullptr) const;

    // ── Generate from CASC (WhiteoutLib casc::Storage) ─────────────────────
    std::vector<ThumbnailResult> generateFromCASC(
        const whiteout::storages::casc::Storage& storage,
        const std::vector<std::string>& paths,
        ThumbnailProgressFn progress = nullptr) const;

    // ── Generate from raw bytes (already extracted) ──────────────────────────
    //    Useful when the caller manages extraction themselves.
    std::vector<ThumbnailResult> generateFromMemory(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files,
        ThumbnailProgressFn progress = nullptr) const;

    // ── Generate from files on disk ──────────────────────────────────────────
    std::vector<ThumbnailResult> generateFromDisk(
        const std::vector<std::string>& filePaths,
        ThumbnailProgressFn progress = nullptr) const;

    // ── Cancel support ───────────────────────────────────────────────────────
    //    Call cancel() from another thread to stop an in-progress generation.
    void cancel();
    bool isCancelled() const noexcept { return cancelled_.load(); }

private:
    // ── Internal batch processor ─────────────────────────────────────────────
    //    extractFn: function that extracts a file by path → byte vector
    using ExtractFn = std::function<bool(const std::string& path,
                                         std::vector<uint8_t>& outData,
                                         std::string& outError)>;

    std::vector<ThumbnailResult> processBatch(
        const std::vector<std::string>& paths,
        ExtractFn extractFn,
        ThumbnailProgressFn progress) const;

    DecodedImage decodeAndResize(std::span<const uint8_t> data) const;

    // ── State ────────────────────────────────────────────────────────────────
    int  threadCount_ = 0;   // 0 = auto
    int  thumbW_      = 64;
    int  thumbH_      = 64;

    mutable std::atomic<bool> cancelled_{false};
};

} // namespace whiteoutdex
