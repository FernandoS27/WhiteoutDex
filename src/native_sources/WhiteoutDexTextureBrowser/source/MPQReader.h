// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <mutex>

// Forward-declare WhiteoutLib pool interface so we don't leak the header
namespace whiteout::interfaces { class WorkerPool; }

namespace whiteoutdex {

// ============================================================================
//  MpqResult
// ============================================================================
enum class MpqResult : int {
    Ok               =  0,
    FileNotFound     = -1,
    OpenFailed       = -2,
    ExtractFailed    = -3,
    InvalidHandle    = -4,
    ListfileMissing  = -5,
    AllocationFailed = -6,
};

// ============================================================================
//  MpqFileEntry
// ============================================================================
struct MpqFileEntry {
    std::string path;
    uint32_t    fileSize   = 0;
    uint32_t    compSize   = 0;
    bool        encrypted  = false;
    bool        compressed = false;
};

// ============================================================================
//  BatchResult  —  per-file result for batch extraction
// ============================================================================
struct MpqBatchEntry {
    std::string          path;
    MpqResult            result = MpqResult::FileNotFound;
    std::vector<uint8_t> data;
};

// ============================================================================
//  MPQReader
//
//  Direct C++ port of the WhiteoutDexMPQ2 C# class from WhiteoutDexMPQHelper.ms.
//  No StormLib — only zlib (already present via WhiteoutLib).
//
//  Multithreading:
//    - Memory-mapped file I/O (lock-free parallel reads)
//    - Parallel sector decompression via WhiteoutLib thread pool
//    - extractBatch() for parallel multi-file extraction
//
//  Thread safety:
//    open() / close() — call from one thread at a time.
//    extractFile() / hasFile() / listFiles() — safe to call concurrently.
// ============================================================================
class MPQReader {
public:
    MPQReader();
    ~MPQReader();

    MPQReader(const MPQReader&)            = delete;
    MPQReader& operator=(const MPQReader&) = delete;
    MPQReader(MPQReader&&)            noexcept;
    MPQReader& operator=(MPQReader&&) noexcept;

    // ── Open / Close ─────────────────────────────────────────────────────────
    MpqResult   open(const std::string& path);
    void        close() noexcept;
    bool        isOpen()       const noexcept { return mapBase_ != nullptr; }
    const std::string& archivePath() const noexcept { return path_; }

    // ── File listing ─────────────────────────────────────────────────────────
    MpqResult listFiles(std::vector<MpqFileEntry>& out,
                        const std::string& filter = "") const;
    MpqResult listPaths(std::vector<std::string>& out,
                        const std::string& filter = "") const;

    // ── Single-file extraction ───────────────────────────────────────────────
    MpqResult extractFile(const std::string& innerPath,
                          std::vector<uint8_t>& outData) const;
    MpqResult extractFileToDisk(const std::string& innerPath,
                                const std::string& destPath) const;

    // ── Batch extraction (parallel, uses thread pool) ────────────────────────
    //    Extracts multiple files concurrently.  Each entry in `paths` gets its
    //    own result + data in the returned vector.
    std::vector<MpqBatchEntry> extractBatch(
        const std::vector<std::string>& paths) const;

    // ── Existence check (hash-table lookup, no decompression) ────────────────
    bool hasFile(const std::string& innerPath) const noexcept;

private:
    // ── Crypto table (built once, shared across all instances) ───────────────
    static uint32_t cryptoTable_[1280];
    static bool     cryptoReady_;

    static void     initCrypto();
    static uint32_t mpqHash(const std::string& s, int hashType) noexcept;
    static void     decrypt(uint8_t* data, size_t byteLen,
                            uint32_t key) noexcept;
    static void     decryptBlock(uint8_t* data, int offset, int length,
                                 uint32_t key) noexcept;
    static uint32_t getFileKey(const std::string& filename) noexcept;

    // ── zlib helper ──────────────────────────────────────────────────────────
    static bool zlibDecompress(const uint8_t* src, size_t srcLen,
                               std::vector<uint8_t>& dst);

    // ── Internal helpers ─────────────────────────────────────────────────────
    int       findBlockIndex(const std::string& innerPath) const noexcept;
    MpqResult extractRaw(const std::string& innerPath,
                         std::vector<uint8_t>& out) const;

    // ── Parallel sector decompression ────────────────────────────────────────
    MpqResult extractRawParallel(const std::string& innerPath,
                                 std::vector<uint8_t>& out) const;

    // ── Convenience read helpers (mirror RU / RI from C#) ────────────────────
    static inline uint32_t ru(const uint8_t* d, int p) noexcept {
        return static_cast<uint32_t>(d[p])
             | (static_cast<uint32_t>(d[p+1]) << 8)
             | (static_cast<uint32_t>(d[p+2]) << 16)
             | (static_cast<uint32_t>(d[p+3]) << 24);
    }
    static inline int32_t ri(const uint8_t* d, int p) noexcept {
        return static_cast<int32_t>(ru(d, p));
    }

    // ── Data ─────────────────────────────────────────────────────────────────
    std::string          path_;
    uint32_t             mpqOffset_  = 0;
    uint32_t             sectorSize_ = 0;
    int                  htEntries_  = 0;
    int                  btEntries_  = 0;
    std::vector<uint8_t> hashTable_;
    std::vector<uint8_t> blockTable_;

    // ── Memory-mapped file ───────────────────────────────────────────────────
    //    Replaces FILE* + mutex — allows lock-free parallel reads.
    //    On Windows: HANDLE hFile_, hMapping_; const uint8_t* mapBase_;
    void*          hFile_    = nullptr;   // HANDLE (INVALID_HANDLE_VALUE)
    void*          hMapping_ = nullptr;   // HANDLE
    const uint8_t* mapBase_  = nullptr;   // pointer to mapped region
    int64_t        mapSize_  = 0;         // total file size
};

// ============================================================================
//  MPQManager  —  multiple archives, mirrors mpqHandles array in .ms
// ============================================================================
class MPQManager {
public:
    MPQManager()  = default;
    ~MPQManager() = default;

    MPQManager(const MPQManager&)            = delete;
    MPQManager& operator=(const MPQManager&) = delete;

    int  openArchive(const std::string& path);
    void closeArchive(int index) noexcept;
    void closeAll()              noexcept;

    int  archiveCount()    const noexcept;
    bool isOpen(int index) const noexcept;

    MpqResult listAllFiles(std::vector<MpqFileEntry>& out,
                           const std::string& filter = "") const;
    MpqResult listAllPaths(std::vector<std::string>& out,
                           const std::string& filter = "") const;

    MpqResult extractFile(const std::string& innerPath,
                          std::vector<uint8_t>& out) const;
    MpqResult extractFileToDisk(const std::string& innerPath,
                                const std::string& destPath) const;

    // ── Batch extraction across all archives (parallel) ──────────────────────
    std::vector<MpqBatchEntry> extractBatch(
        const std::vector<std::string>& paths) const;

    int findArchiveContaining(const std::string& innerPath) const noexcept;

private:
    std::vector<MPQReader> archives_;
};

} // namespace whiteoutdex
