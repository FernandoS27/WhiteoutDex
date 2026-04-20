// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
/**
 * @file ThumbnailEngine.cpp
 * @brief Parallel batch thumbnail generator for MPQ/CASC textures.
 *
 * Distributes the extract→decode→resize pipeline across N worker threads.
 * Each worker processes a subset of files independently — no shared
 * mutable state, no locks on the hot path.
 *
 * Target: Windows x64, 3ds Max plugin context.
 */

#include "ThumbnailEngine.h"
#include "MPQReader.h"

#ifndef WHITEOUT_HAS_CASC
#define WHITEOUT_HAS_CASC 1
#endif
#include <whiteout/storages/casc/storage.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <future>
#include <thread>

namespace whiteoutdex {

// ============================================================================
//  Construction / configuration
// ============================================================================

ThumbnailEngine::ThumbnailEngine() {
    threadCount_ = 0;  // auto
}

void ThumbnailEngine::setThreadCount(int threads) {
    threadCount_ = (threads > 0)
        ? threads
        : 0;  // 0 = auto
}

void ThumbnailEngine::setThumbnailSize(int width, int height) {
    thumbW_ = (width  > 0) ? width  : 64;
    thumbH_ = (height > 0) ? height : 64;
}

void ThumbnailEngine::cancel() {
    cancelled_.store(true);
}

// ============================================================================
//  detectFormat  —  identify texture type by magic bytes
// ============================================================================

ThumbnailEngine::TextureFormat
ThumbnailEngine::detectFormat(const uint8_t* data, size_t len) {
    if (!data || len < 4) return TextureFormat::Unknown;

    // BLP: "BLP1" or "BLP2"
    if (data[0] == 'B' && data[1] == 'L' && data[2] == 'P' &&
        (data[3] == '1' || data[3] == '2'))
        return TextureFormat::BLP;

    // DDS: "DDS " (0x20534444)
    if (data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
        return TextureFormat::DDS;

    return TextureFormat::Unknown;
}

// ============================================================================
//  decodeTexture  —  detect + decode + resize to thumbnail
// ============================================================================

DecodedImage ThumbnailEngine::decodeTexture(const uint8_t* data,
                                             size_t len) const {
    TextureFormat fmt = detectFormat(data, len);

    switch (fmt) {
    case TextureFormat::BLP:
        return blpDecoder_.decodeThumbnail(data, len, thumbW_, thumbH_);

    case TextureFormat::DDS:
        return ddsDecoder_.decodeThumbnail(data, len, thumbW_, thumbH_);

    default:
        return {};
    }
}

// ============================================================================
//  processBatch  —  core parallel engine
//
//  Takes a generic ExtractFn so the same logic works for MPQ, CASC,
//  in-memory, and disk-based sources.
//
//  Work distribution:
//    Files are partitioned into N chunks (one per worker thread).
//    Each worker processes its chunk sequentially:
//      for each file in chunk:
//        1. extractFn(path) → raw bytes
//        2. detectFormat + decode → DecodedImage
//        3. store result
//
//  This is simpler and more cache-friendly than a shared work-queue,
//  and avoids any synchronization on the hot path.  The only shared
//  state is the atomic progress counter.
// ============================================================================

std::vector<ThumbnailResult> ThumbnailEngine::processBatch(
    const std::vector<std::string>& paths,
    ExtractFn extractFn,
    ThumbnailProgressFn progress) const
{
    cancelled_.store(false);

    const int fileCount = static_cast<int>(paths.size());
    std::vector<ThumbnailResult> results(fileCount);

    // Initialize path fields
    for (int i = 0; i < fileCount; ++i)
        results[i].path = paths[i];

    if (fileCount == 0) return results;

    // Determine thread count
    int numThreads = threadCount_;
    if (numThreads <= 0)
        numThreads = static_cast<int>(
            std::max<unsigned>(std::thread::hardware_concurrency(), 2));
    numThreads = std::min(numThreads, fileCount);

    // Shared progress counter
    std::atomic<int> completed{0};

    // Progress reporting mutex (callbacks may not be thread-safe)
    std::mutex progressMutex;

    // ── Worker function ──────────────────────────────────────────────────
    auto workerFn = [&](int startIdx, int endIdx) {
        for (int i = startIdx; i < endIdx; ++i) {
            // Check cancellation
            if (cancelled_.load()) {
                results[i].success = false;
                results[i].error   = "Cancelled";
                continue;
            }

            // Step 1: Extract
            std::vector<uint8_t> rawData;
            std::string extractError;
            bool ok = extractFn(paths[i], rawData, extractError);

            if (!ok || rawData.empty()) {
                results[i].success = false;
                results[i].error   = extractError.empty()
                    ? "Extraction failed" : extractError;

                // Report progress even on failure
                int done = ++completed;
                if (progress) {
                    std::lock_guard<std::mutex> lock(progressMutex);
                    progress(done, fileCount);
                }
                continue;
            }

            // Step 2: Detect format + decode + resize
            DecodedImage thumb = decodeTexture(rawData.data(), rawData.size());

            if (!thumb.valid()) {
                results[i].success = false;
                results[i].error   = "Decode failed (unsupported format)";
                // IMPROVED: Keep raw bytes so managed layer can retry
                // with System.Drawing (handles JPG, PNG, BMP, GIF, TGA)
                results[i].rawData = std::move(rawData);
            } else {
                results[i].thumbnail = std::move(thumb);
                results[i].success   = true;
            }

            // Step 3: Report progress
            int done = ++completed;
            if (progress) {
                std::lock_guard<std::mutex> lock(progressMutex);
                progress(done, fileCount);
            }
        }
    };

    // ── Partition work across threads ────────────────────────────────────
    if (numThreads == 1 || fileCount <= 2) {
        // Single-threaded: avoid async overhead
        workerFn(0, fileCount);
    } else {
        std::vector<std::future<void>> futures;
        futures.reserve(numThreads);

        int chunkSize = fileCount / numThreads;
        int remainder = fileCount % numThreads;
        int startIdx  = 0;

        for (int t = 0; t < numThreads; ++t) {
            int count = chunkSize + (t < remainder ? 1 : 0);
            int endIdx = startIdx + count;

            futures.push_back(std::async(std::launch::async,
                [&workerFn, startIdx, endIdx]() {
                    workerFn(startIdx, endIdx);
                }));

            startIdx = endIdx;
        }

        for (auto& f : futures) f.get();
    }

    return results;
}

// ============================================================================
//  generateFromMPQ (single reader)
// ============================================================================

std::vector<ThumbnailResult> ThumbnailEngine::generateFromMPQ(
    const MPQReader& reader,
    const std::vector<std::string>& paths,
    ThumbnailProgressFn progress) const
{
    ExtractFn extractFn = [&reader](const std::string& path,
                                     std::vector<uint8_t>& outData,
                                     std::string& outError) -> bool {
        MpqResult r = reader.extractFile(path, outData);
        if (r != MpqResult::Ok) {
            outError = "MPQ extract error: " + std::to_string(static_cast<int>(r));
            return false;
        }
        return true;
    };

    return processBatch(paths, extractFn, progress);
}

// ============================================================================
//  generateFromMPQ (manager — searches all open archives)
// ============================================================================

std::vector<ThumbnailResult> ThumbnailEngine::generateFromMPQ(
    const MPQManager& manager,
    const std::vector<std::string>& paths,
    ThumbnailProgressFn progress) const
{
    ExtractFn extractFn = [&manager](const std::string& path,
                                      std::vector<uint8_t>& outData,
                                      std::string& outError) -> bool {
        MpqResult r = manager.extractFile(path, outData);
        if (r != MpqResult::Ok) {
            outError = "MPQ extract error: " + std::to_string(static_cast<int>(r));
            return false;
        }
        return true;
    };

    return processBatch(paths, extractFn, progress);
}

// ============================================================================
//  generateFromCASC  (uses WhiteoutLib casc::Storage)
// ============================================================================

std::vector<ThumbnailResult> ThumbnailEngine::generateFromCASC(
    const whiteout::storages::casc::Storage& storage,
    const std::vector<std::string>& paths,
    ThumbnailProgressFn progress) const
{
    ExtractFn extractFn = [&storage](const std::string& path,
                                     std::vector<uint8_t>& outData,
                                     std::string& outError) -> bool {
        auto result = storage.readFile(path);
        if (!result || result->empty()) {
            outError = "CASC extract failed: file not found or decode error";
            return false;
        }
        outData = std::move(*result);
        return true;
    };

    return processBatch(paths, extractFn, progress);
}

// ============================================================================
//  generateFromMemory  —  data already extracted
// ============================================================================

std::vector<ThumbnailResult> ThumbnailEngine::generateFromMemory(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files,
    ThumbnailProgressFn progress) const
{
    // Build path list + data lookup
    std::vector<std::string> paths;
    paths.reserve(files.size());
    for (const auto& f : files)
        paths.push_back(f.first);

    ExtractFn extractFn = [&files](const std::string& path,
                                    std::vector<uint8_t>& outData,
                                    std::string& outError) -> bool {
        for (const auto& f : files) {
            if (f.first == path) {
                outData = f.second;  // copy
                return true;
            }
        }
        outError = "Not found in memory buffer";
        return false;
    };

    return processBatch(paths, extractFn, progress);
}

// ============================================================================
//  generateFromDisk  —  files on disk (loose .blp / .dds files)
// ============================================================================

std::vector<ThumbnailResult> ThumbnailEngine::generateFromDisk(
    const std::vector<std::string>& filePaths,
    ThumbnailProgressFn progress) const
{
    ExtractFn extractFn = [](const std::string& path,
                              std::vector<uint8_t>& outData,
                              std::string& outError) -> bool {
        std::ifstream ifs(path, std::ios::binary | std::ios::ate);
        if (!ifs.is_open()) {
            outError = "File not found: " + path;
            return false;
        }
        auto size = ifs.tellg();
        if (size <= 0) {
            outError = "Empty file: " + path;
            return false;
        }
        outData.resize(static_cast<size_t>(size));
        ifs.seekg(0);
        ifs.read(reinterpret_cast<char*>(outData.data()), size);
        return true;
    };

    return processBatch(filePaths, extractFn, progress);
}

} // namespace whiteoutdex
