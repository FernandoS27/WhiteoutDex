// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow
/**
 * @file MPQReader.cpp
 * @brief Direct C++ port of WhiteoutDexMPQ2 (WhiteoutDexMPQHelper.ms).
 *
 * Multithreading improvements over the original port:
 *
 *   1. Memory-mapped I/O  — The archive file is memory-mapped on open(),
 *      replacing FILE* + mutex.  This allows completely lock-free concurrent
 *      reads from any thread (the OS handles page-faulting/caching).
 *
 *   2. Parallel sector decompression  — Multi-sector files have their
 *      sectors decrypted and decompressed in parallel using WhiteoutLib's
 *      SimpleThreadPool.  Each sector is independent once the offset table
 *      has been parsed.
 *
 *   3. Batch extraction  — extractBatch() processes multiple files in
 *      parallel, distributing each file to a worker thread.
 *
 * The only external dependency is zlib for decompression,
 * which WhiteoutLib already brings in.
 */

#include "MPQReader.h"
#include "SharedPool.h"    // IMPROVED: single global pool

// zlib — transitive dependency via WhiteoutLib.
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#else
#   include <sys/mman.h>
#   include <sys/stat.h>
#   include <fcntl.h>
#   include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace whiteoutdex {

// ============================================================================
//  Shared thread pool  (created once, same pattern as BLPDecoder)
// ============================================================================

namespace {

// Pool access is now via whiteoutdex::getSharedPool() from SharedPool.h
// MIN_SECTORS_FOR_PARALLEL is now whiteoutdex::kMinParallelSectors

} // anonymous namespace

// ============================================================================
//  Static data
// ============================================================================

uint32_t MPQReader::cryptoTable_[1280] = {};
bool     MPQReader::cryptoReady_       = false;

static std::mutex g_cryptoInitMutex;

// ============================================================================
//  Construction / move
// ============================================================================

MPQReader::MPQReader()  = default;
MPQReader::~MPQReader() { close(); }

MPQReader::MPQReader(MPQReader&& o) noexcept
    : path_(std::move(o.path_))
    , mpqOffset_(o.mpqOffset_)
    , sectorSize_(o.sectorSize_)
    , htEntries_(o.htEntries_)
    , btEntries_(o.btEntries_)
    , hashTable_(std::move(o.hashTable_))
    , blockTable_(std::move(o.blockTable_))
    , hFile_(o.hFile_)
    , hMapping_(o.hMapping_)
    , mapBase_(o.mapBase_)
    , mapSize_(o.mapSize_)
{
    o.hFile_    = nullptr;
    o.hMapping_ = nullptr;
    o.mapBase_  = nullptr;
    o.mapSize_  = 0;
}

MPQReader& MPQReader::operator=(MPQReader&& o) noexcept {
    if (this != &o) {
        close();
        path_        = std::move(o.path_);
        mpqOffset_   = o.mpqOffset_;
        sectorSize_  = o.sectorSize_;
        htEntries_   = o.htEntries_;
        btEntries_   = o.btEntries_;
        hashTable_   = std::move(o.hashTable_);
        blockTable_  = std::move(o.blockTable_);
        hFile_       = o.hFile_;
        hMapping_    = o.hMapping_;
        mapBase_     = o.mapBase_;
        mapSize_     = o.mapSize_;
        o.hFile_    = nullptr;
        o.hMapping_ = nullptr;
        o.mapBase_  = nullptr;
        o.mapSize_  = 0;
    }
    return *this;
}

// ============================================================================
//  initCrypto  —  port of InitCrypto()
// ============================================================================

void MPQReader::initCrypto() {
    std::lock_guard<std::mutex> lock(g_cryptoInitMutex);
    if (cryptoReady_) return;

    uint32_t seed = 0x00100001u;
    for (int i = 0; i < 256; ++i) {
        int idx = i;
        for (int j = 0; j < 5; ++j) {
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t1 = (seed & 0xFFFFu) << 16;
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            uint32_t t2 = seed & 0xFFFFu;
            cryptoTable_[idx] = t1 | t2;
            idx += 256;
        }
    }
    cryptoReady_ = true;
}

// ============================================================================
//  mpqHash
// ============================================================================

uint32_t MPQReader::mpqHash(const std::string& s, int hashType) noexcept {
    uint32_t seed1 = 0x7FED7FEDu;
    uint32_t seed2 = 0xEEEEEEEEu;

    for (char raw : s) {
        uint8_t b = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(raw)));
        if (b == '/') b = '\\';

        seed1 = cryptoTable_[hashType * 256 + b] ^ (seed1 + seed2);
        seed2 = b + seed1 + seed2 + (seed2 << 5) + 3u;
    }
    return seed1;
}

// ============================================================================
//  decrypt / decryptBlock / getFileKey
// ============================================================================

void MPQReader::decrypt(uint8_t* data, size_t byteLen, uint32_t key) noexcept {
    uint32_t s1 = key;
    uint32_t s2 = 0xEEEEEEEEu;
    const size_t n = byteLen / 4;

    for (size_t i = 0; i < n; ++i) {
        s2 += cryptoTable_[0x400 + (s1 & 0xFF)];
        uint32_t enc;
        std::memcpy(&enc, data + i * 4, 4);
        uint32_t dec = enc ^ (s1 + s2);
        std::memcpy(data + i * 4, &dec, 4);
        s1 = ((~s1 << 21) + 0x11111111u) | (s1 >> 11);
        s2 = dec + s2 + (s2 << 5) + 3u;
    }
}

void MPQReader::decryptBlock(uint8_t* data, int offset,
                             int length, uint32_t key) noexcept {
    uint32_t s1 = key;
    uint32_t s2 = 0xEEEEEEEEu;
    const int n  = length / 4;

    for (int i = 0; i < n; ++i) {
        int p = offset + i * 4;
        s2 += cryptoTable_[0x400 + (s1 & 0xFF)];
        uint32_t enc;
        std::memcpy(&enc, data + p, 4);
        uint32_t dec = enc ^ (s1 + s2);
        std::memcpy(data + p, &dec, 4);
        s1 = ((~s1 << 21) + 0x11111111u) | (s1 >> 11);
        s2 = dec + s2 + (s2 << 5) + 3u;
    }
}

uint32_t MPQReader::getFileKey(const std::string& filename) noexcept {
    size_t idx = filename.find_last_of("/\\");
    std::string name = (idx != std::string::npos)
                         ? filename.substr(idx + 1)
                         : filename;
    return mpqHash(name, 3);
}

// ============================================================================
//  zlibDecompress
// ============================================================================

bool MPQReader::zlibDecompress(const uint8_t* src, size_t srcLen,
                               std::vector<uint8_t>& dst) {
    uLongf dstLen = static_cast<uLongf>(srcLen) * 4 + 256;
    for (int attempt = 0; attempt < 6; ++attempt) {
        dst.resize(dstLen);
        int ret = uncompress(dst.data(), &dstLen, src, static_cast<uLong>(srcLen));
        if (ret == Z_OK) { dst.resize(dstLen); return true; }
        if (ret == Z_BUF_ERROR) { dstLen *= 2; continue; }
        break;
    }
    dst.clear();
    return false;
}

// ============================================================================
//  open  —  Memory-mapped file version
//
//  Uses CreateFileMapping/MapViewOfFile on Windows, mmap on POSIX.
//  After mapping, the entire archive is accessible as a flat byte array
//  (mapBase_[0..mapSize_-1]) — no FILE*, no mutex, no fread.
// ============================================================================

MpqResult MPQReader::open(const std::string& path) {
    if (mapBase_) close();
    initCrypto();

#ifdef _WIN32
    // ── Windows memory-mapped I/O ────────────────────────────────────────
    HANDLE hf = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                            nullptr);
    if (hf == INVALID_HANDLE_VALUE) return MpqResult::OpenFailed;

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hf, &fileSize)) { CloseHandle(hf); return MpqResult::OpenFailed; }

    HANDLE hm = CreateFileMappingA(hf, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hm) { CloseHandle(hf); return MpqResult::OpenFailed; }

    const uint8_t* base = static_cast<const uint8_t*>(
        MapViewOfFile(hm, FILE_MAP_READ, 0, 0, 0));
    if (!base) { CloseHandle(hm); CloseHandle(hf); return MpqResult::OpenFailed; }

    hFile_   = static_cast<void*>(hf);
    hMapping_= static_cast<void*>(hm);
    mapBase_ = base;
    mapSize_ = fileSize.QuadPart;

#else
    // ── POSIX memory-mapped I/O ──────────────────────────────────────────
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return MpqResult::OpenFailed;

    struct stat st;
    if (fstat(fd, &st) != 0) { ::close(fd); return MpqResult::OpenFailed; }

    void* base = mmap(nullptr, static_cast<size_t>(st.st_size),
                      PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);   // fd can be closed after mmap
    if (base == MAP_FAILED) return MpqResult::OpenFailed;

    hFile_   = nullptr;
    hMapping_= nullptr;
    mapBase_ = static_cast<const uint8_t*>(base);
    mapSize_ = st.st_size;
#endif

    // ── Find MPQ\x1A signature ───────────────────────────────────────────
    constexpr size_t SCAN_SIZE = 4096;
    const size_t scanEnd = std::min(static_cast<size_t>(mapSize_), SCAN_SIZE);
    constexpr uint32_t MPQ_MAGIC = 0x1A51504Du;

    uint32_t mpqOfs = UINT32_MAX;
    for (size_t i = 0; i + 4 <= scanEnd; i += 512) {
        uint32_t sig;
        std::memcpy(&sig, mapBase_ + i, 4);
        if (sig == MPQ_MAGIC) { mpqOfs = static_cast<uint32_t>(i); break; }
    }
    if (mpqOfs == UINT32_MAX && scanEnd >= 4) {
        uint32_t sig;
        std::memcpy(&sig, mapBase_, 4);
        if (sig == MPQ_MAGIC) mpqOfs = 0;
    }
    if (mpqOfs == UINT32_MAX) { close(); return MpqResult::OpenFailed; }

    mpqOffset_ = mpqOfs;
    const uint8_t* hdr = mapBase_ + mpqOfs;

    // ── Parse header ─────────────────────────────────────────────────────
    uint16_t ss;
    std::memcpy(&ss, hdr + 14, 2);
    sectorSize_ = 512u << ss;

    int32_t htOfs, btOfs;
    std::memcpy(&htOfs,      hdr + 16, 4);
    std::memcpy(&btOfs,      hdr + 20, 4);
    std::memcpy(&htEntries_, hdr + 24, 4);
    std::memcpy(&btEntries_, hdr + 28, 4);

    if (htEntries_ <= 0 || btEntries_ <= 0) { close(); return MpqResult::OpenFailed; }

    // Validate table positions are within file bounds
    int64_t htAbsOfs = static_cast<int64_t>(mpqOfs) + htOfs;
    int64_t btAbsOfs = static_cast<int64_t>(mpqOfs) + btOfs;
    int64_t htSize   = static_cast<int64_t>(htEntries_) * 16;
    int64_t btSize   = static_cast<int64_t>(btEntries_) * 16;

    if (htAbsOfs + htSize > mapSize_ || btAbsOfs + btSize > mapSize_) {
        close(); return MpqResult::OpenFailed;
    }

    // ── Read and decrypt hash table ──────────────────────────────────────
    hashTable_.resize(static_cast<size_t>(htSize));
    std::memcpy(hashTable_.data(), mapBase_ + htAbsOfs, hashTable_.size());
    decrypt(hashTable_.data(), hashTable_.size(), mpqHash("(hash table)", 3));

    // ── Read and decrypt block table ─────────────────────────────────────
    blockTable_.resize(static_cast<size_t>(btSize));
    std::memcpy(blockTable_.data(), mapBase_ + btAbsOfs, blockTable_.size());
    decrypt(blockTable_.data(), blockTable_.size(), mpqHash("(block table)", 3));

    path_ = path;
    return MpqResult::Ok;
}

// ============================================================================
//  close
// ============================================================================

void MPQReader::close() noexcept {
#ifdef _WIN32
    if (mapBase_)  UnmapViewOfFile(mapBase_);
    if (hMapping_) CloseHandle(static_cast<HANDLE>(hMapping_));
    if (hFile_)    CloseHandle(static_cast<HANDLE>(hFile_));
#else
    if (mapBase_)
        munmap(const_cast<uint8_t*>(mapBase_), static_cast<size_t>(mapSize_));
#endif
    mapBase_  = nullptr;
    hFile_    = nullptr;
    hMapping_ = nullptr;
    mapSize_  = 0;
    path_.clear();
    mpqOffset_ = sectorSize_ = 0;
    htEntries_ = btEntries_ = 0;
    hashTable_.clear();
    blockTable_.clear();
}

// ============================================================================
//  findBlockIndex
// ============================================================================

int MPQReader::findBlockIndex(const std::string& innerPath) const noexcept {
    if (!mapBase_ || htEntries_ <= 0) return -1;

    const uint32_t hA    = mpqHash(innerPath, 1);
    const uint32_t hB    = mpqHash(innerPath, 2);
    const uint32_t start = mpqHash(innerPath, 0) % static_cast<uint32_t>(htEntries_);
    const uint8_t* ht    = hashTable_.data();

    for (uint32_t a = 0; a < static_cast<uint32_t>(htEntries_); ++a) {
        int     hi    = static_cast<int>((start + a) % static_cast<uint32_t>(htEntries_));
        int     ho    = hi * 16;
        uint32_t eA   = ru(ht, ho);
        uint32_t eB   = ru(ht, ho + 4);
        int32_t eBlock = ri(ht, ho + 12);

        if (eBlock == -1) break;
        if (eBlock == -2) continue;
        if (eA == hA && eB == hB) return eBlock;
    }
    return -1;
}

// ============================================================================
//  hasFile
// ============================================================================

bool MPQReader::hasFile(const std::string& innerPath) const noexcept {
    int bi = findBlockIndex(innerPath);
    if (bi < 0 || bi >= btEntries_) return false;
    int bo = bi * 16;
    int32_t compSize   = ri(blockTable_.data(), bo + 4);
    int32_t uncompSize = ri(blockTable_.data(), bo + 8);
    return (compSize > 0 && uncompSize > 0);
}

// ============================================================================
//  extractRaw  —  single-threaded fallback (used for small files)
// ============================================================================

MpqResult MPQReader::extractRaw(const std::string& innerPath,
                                std::vector<uint8_t>& out) const {
    int bi = findBlockIndex(innerPath);
    if (bi < 0) return MpqResult::FileNotFound;

    const uint8_t* bt  = blockTable_.data();
    int            bo  = bi * 16;
    int32_t  fileOfs    = ri(bt, bo);
    int32_t  compSize   = ri(bt, bo + 4);
    int32_t  uncompSize = ri(bt, bo + 8);
    uint32_t flags      = ru(bt, bo + 12);

    if (compSize <= 0 || uncompSize <= 0) return MpqResult::ExtractFailed;

    const bool encrypted  = (flags & 0x00010000u) != 0;
    const bool fixKey     = (flags & 0x00020000u) != 0;
    const bool hasSectors = (flags & 0x00000200u) != 0;
    const bool compressed = (flags & 0x00000100u) != 0;
    const bool singleUnit = (flags & 0x01000000u) != 0;

    uint32_t fileKey = 0;
    if (encrypted) {
        fileKey = getFileKey(innerPath);
        if (fixKey)
            fileKey = (fileKey + static_cast<uint32_t>(fileOfs))
                    ^ static_cast<uint32_t>(uncompSize);
    }

    // Validate offsets against mapped file size
    int64_t absOfs = static_cast<int64_t>(mpqOffset_) + fileOfs;
    if (absOfs < 0 || absOfs + compSize > mapSize_)
        return MpqResult::ExtractFailed;

    // Copy raw data from memory map (zero-copy read)
    std::vector<uint8_t> raw(static_cast<size_t>(compSize));
    std::memcpy(raw.data(), mapBase_ + absOfs, raw.size());

    // ── Case 1: single-unit or no-sector file ────────────────────────────
    if (singleUnit || (!hasSectors && !compressed)) {
        if (encrypted)
            decrypt(raw.data(), raw.size(), fileKey);

        if (compSize == uncompSize) {
            out = std::move(raw);
            return MpqResult::Ok;
        }

        if (compSize < uncompSize && raw.size() > 1) {
            uint8_t cType = raw[0];
            const uint8_t* payload = raw.data() + 1;
            size_t  payLen  = raw.size() - 1;

            if (cType == 2 || cType == 8) {
                if (zlibDecompress(payload, payLen, out))
                    return MpqResult::Ok;
            }
        }
        out = std::move(raw);
        return MpqResult::Ok;
    }

    // ── Case 2: multi-sector file ────────────────────────────────────────
    const int numSectors   = (uncompSize + static_cast<int>(sectorSize_) - 1)
                           / static_cast<int>(sectorSize_);
    const int ofsTableSize = (numSectors + 1) * 4;

    if (encrypted && ofsTableSize <= static_cast<int>(raw.size()))
        decryptBlock(raw.data(), 0, ofsTableSize, fileKey - 1u);

    std::vector<int32_t> offsets(numSectors + 1);
    for (int i = 0; i <= numSectors; ++i) {
        if (i * 4 + 3 < static_cast<int>(raw.size()))
            offsets[i] = ri(raw.data(), i * 4);
        else
            offsets[i] = compSize;
        if (offsets[i] < 0 || offsets[i] > compSize)
            offsets[i] = compSize;
    }

    out.resize(static_cast<size_t>(uncompSize));
    int outPos = 0;

    for (int sec = 0; sec < numSectors; ++sec) {
        int sOfs   = offsets[sec];
        int sEnd   = offsets[sec + 1];
        int sLen   = sEnd - sOfs;
        int remain = uncompSize - outPos;

        if (sLen <= 0 || sOfs < 0 || sEnd > compSize || remain <= 0) break;
        const int expectLen = std::min(static_cast<int>(sectorSize_), remain);

        if (encrypted)
            decryptBlock(raw.data(), sOfs, sLen,
                         fileKey + static_cast<uint32_t>(sec));

        if (sLen == expectLen) {
            std::memcpy(out.data() + outPos, raw.data() + sOfs,
                        std::min(sLen, remain));
            outPos += sLen;
        } else if (sLen > 1 && sLen < expectLen) {
            uint8_t cType = raw[sOfs];
            const uint8_t* payload = raw.data() + sOfs + 1;
            size_t  payLen  = static_cast<size_t>(sLen - 1);

            std::vector<uint8_t> dec;
            bool ok = false;
            if (cType == 2 || cType == 8)
                ok = zlibDecompress(payload, payLen, dec);

            if (ok && !dec.empty()) {
                int toCopy = std::min(static_cast<int>(dec.size()), remain);
                std::memcpy(out.data() + outPos, dec.data(), toCopy);
                outPos += toCopy;
            } else {
                std::memcpy(out.data() + outPos, raw.data() + sOfs,
                            std::min(sLen, remain));
                outPos += std::min(sLen, remain);
            }
        } else {
            std::memcpy(out.data() + outPos, raw.data() + sOfs,
                        std::min(sLen, remain));
            outPos += std::min(sLen, remain);
        }
    }

    out.resize(static_cast<size_t>(outPos));
    return MpqResult::Ok;
}

// ============================================================================
//  extractRawParallel  —  parallel sector decompression
//
//  Strategy:
//    1. Read raw data from memory map (instant, zero-copy via memcpy)
//    2. Decrypt sector offset table (sequential, tiny — one decrypt call)
//    3. For each sector in parallel:
//       a. Copy sector data to a private buffer (avoids contention on raw[])
//       b. Decrypt the copy (key = fileKey + sectorIndex — independent!)
//       c. Decompress with zlib
//    4. Assemble results in order into the output buffer
//
//  Falls back to sequential for files with < MIN_SECTORS_FOR_PARALLEL sectors.
// ============================================================================

MpqResult MPQReader::extractRawParallel(const std::string& innerPath,
                                        std::vector<uint8_t>& out) const {
    int bi = findBlockIndex(innerPath);
    if (bi < 0) return MpqResult::FileNotFound;

    const uint8_t* bt  = blockTable_.data();
    int            bo  = bi * 16;
    int32_t  fileOfs    = ri(bt, bo);
    int32_t  compSize   = ri(bt, bo + 4);
    int32_t  uncompSize = ri(bt, bo + 8);
    uint32_t flags      = ru(bt, bo + 12);

    if (compSize <= 0 || uncompSize <= 0) return MpqResult::ExtractFailed;

    const bool encrypted  = (flags & 0x00010000u) != 0;
    const bool fixKey     = (flags & 0x00020000u) != 0;
    const bool hasSectors = (flags & 0x00000200u) != 0;
    const bool compressed = (flags & 0x00000100u) != 0;
    const bool singleUnit = (flags & 0x01000000u) != 0;

    // For single-unit or small files, use sequential path
    if (singleUnit || (!hasSectors && !compressed))
        return extractRaw(innerPath, out);

    const int numSectors = (uncompSize + static_cast<int>(sectorSize_) - 1)
                         / static_cast<int>(sectorSize_);

    // Below threshold → sequential is faster (avoid thread dispatch overhead)
    if (numSectors < kMinParallelSectors)
        return extractRaw(innerPath, out);

    // ── Read raw data from memory map ────────────────────────────────────
    uint32_t fileKey = 0;
    if (encrypted) {
        fileKey = getFileKey(innerPath);
        if (fixKey)
            fileKey = (fileKey + static_cast<uint32_t>(fileOfs))
                    ^ static_cast<uint32_t>(uncompSize);
    }

    int64_t absOfs = static_cast<int64_t>(mpqOffset_) + fileOfs;
    if (absOfs < 0 || absOfs + compSize > mapSize_)
        return MpqResult::ExtractFailed;

    // We need a mutable copy for decryption
    std::vector<uint8_t> raw(static_cast<size_t>(compSize));
    std::memcpy(raw.data(), mapBase_ + absOfs, raw.size());

    // ── Decrypt sector offset table (sequential — must happen first) ─────
    const int ofsTableSize = (numSectors + 1) * 4;
    if (encrypted && ofsTableSize <= static_cast<int>(raw.size()))
        decryptBlock(raw.data(), 0, ofsTableSize, fileKey - 1u);

    std::vector<int32_t> offsets(numSectors + 1);
    for (int i = 0; i <= numSectors; ++i) {
        if (i * 4 + 3 < static_cast<int>(raw.size()))
            offsets[i] = ri(raw.data(), i * 4);
        else
            offsets[i] = compSize;
        if (offsets[i] < 0 || offsets[i] > compSize)
            offsets[i] = compSize;
    }

    // ── Prepare per-sector output buffers ─────────────────────────────────
    struct SectorResult {
        std::vector<uint8_t> data;
        int                  bytesWritten = 0;
    };
    std::vector<SectorResult> results(numSectors);

    // ── Dispatch sectors to thread pool ───────────────────────────────────
    //    Each lambda captures sector-specific data by value (offset, length,
    //    key).  The raw[] buffer is shared read-only after offset-table
    //    decryption — each sector copies its slice to a private buffer
    //    before decrypting.
    //
    //    IMPROVED: Uses shared WorkerPool instead of unbounded std::async.
    auto* pool = getSharedPool();

    for (int sec = 0; sec < numSectors; ++sec) {
        int sOfs   = offsets[sec];
        int sEnd   = offsets[sec + 1];
        int sLen   = sEnd - sOfs;
        int outOfs = sec * static_cast<int>(sectorSize_);
        int remain = uncompSize - outOfs;

        if (sLen <= 0 || sOfs < 0 || sEnd > compSize || remain <= 0) {
            results[sec].bytesWritten = 0;
            continue;
        }

        const int expectLen = std::min(static_cast<int>(sectorSize_), remain);

        whiteout::interfaces::WorkerTask task;
        task.fn = [&raw, &results, sec, sOfs, sLen, remain, expectLen,
             encrypted, fileKey]()
        {
            // Private copy of this sector's data (for decryption)
            std::vector<uint8_t> sectorBuf(raw.begin() + sOfs,
                                           raw.begin() + sOfs + sLen);

            // Decrypt with sector-specific key
            if (encrypted)
                decryptBlock(sectorBuf.data(), 0, sLen,
                             fileKey + static_cast<uint32_t>(sec));

            if (sLen == expectLen) {
                // Uncompressed sector
                int n = std::min(sLen, remain);
                results[sec].data.assign(sectorBuf.begin(),
                                         sectorBuf.begin() + n);
                results[sec].bytesWritten = n;
            } else if (sLen > 1 && sLen < expectLen) {
                // Compressed sector
                uint8_t cType = sectorBuf[0];
                std::vector<uint8_t> dec;
                bool ok = false;

                if (cType == 2 || cType == 8)
                    ok = zlibDecompress(sectorBuf.data() + 1,
                                       static_cast<size_t>(sLen - 1), dec);

                if (ok && !dec.empty()) {
                    int n = std::min(static_cast<int>(dec.size()), remain);
                    dec.resize(n);
                    results[sec].data = std::move(dec);
                    results[sec].bytesWritten = n;
                } else {
                    int n = std::min(sLen, remain);
                    results[sec].data.assign(sectorBuf.begin(),
                                             sectorBuf.begin() + n);
                    results[sec].bytesWritten = n;
                }
            } else {
                int n = std::min(sLen, remain);
                results[sec].data.assign(sectorBuf.begin(),
                                         sectorBuf.begin() + n);
                results[sec].bytesWritten = n;
            }
        };
        pool->submit(task);
    }

    // ── Wait for all sectors and assemble output ─────────────────────────
    pool->waitIdle();

    out.resize(static_cast<size_t>(uncompSize));
    int outPos = 0;
    for (int sec = 0; sec < numSectors; ++sec) {
        const auto& r = results[sec];
        if (r.bytesWritten > 0 && !r.data.empty()) {
            std::memcpy(out.data() + outPos, r.data.data(), r.bytesWritten);
            outPos += r.bytesWritten;
        }
    }
    out.resize(static_cast<size_t>(outPos));
    return MpqResult::Ok;
}

// ============================================================================
//  extractFile  —  dispatches to parallel or sequential path
// ============================================================================

MpqResult MPQReader::extractFile(const std::string& innerPath,
                                 std::vector<uint8_t>& outData) const {
    if (!mapBase_) return MpqResult::InvalidHandle;
    return extractRawParallel(innerPath, outData);
}

// ============================================================================
//  extractFileToDisk
// ============================================================================

MpqResult MPQReader::extractFileToDisk(const std::string& innerPath,
                                       const std::string& destPath) const {
    std::vector<uint8_t> data;
    MpqResult r = extractFile(innerPath, data);
    if (r != MpqResult::Ok) return r;

    try {
        fs::path dest(destPath);
        if (dest.has_parent_path()) fs::create_directories(dest.parent_path());
    } catch (...) { return MpqResult::ExtractFailed; }

    std::ofstream ofs(destPath, std::ios::binary);
    if (!ofs) return MpqResult::ExtractFailed;
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return ofs.good() ? MpqResult::Ok : MpqResult::ExtractFailed;
}

// ============================================================================
//  extractBatch  —  parallel multi-file extraction
//
//  Each file is extracted on its own async task.  Since we use memory-mapped
//  I/O, there's no file-handle contention — multiple threads can read
//  different regions of the mapped archive simultaneously.
// ============================================================================

std::vector<MpqBatchEntry> MPQReader::extractBatch(
    const std::vector<std::string>& paths) const
{
    std::vector<MpqBatchEntry> results(paths.size());

    // Initialize path fields
    for (size_t i = 0; i < paths.size(); ++i)
        results[i].path = paths[i];

    if (!mapBase_) {
        for (auto& r : results) r.result = MpqResult::InvalidHandle;
        return results;
    }

    // IMPROVED: Dispatch to shared pool (bounded threads) instead of
    //           unbounded std::async.  Prevents spawning 10,000 OS threads
    //           for large archives.
    auto* pool = getSharedPool();

    for (size_t i = 0; i < paths.size(); ++i) {
        whiteout::interfaces::WorkerTask task;
        task.fn = [this, &results, i]() {
            results[i].result = extractRawParallel(
                results[i].path, results[i].data);
        };
        pool->submit(task);
    }

    pool->waitIdle();
    return results;
}

// ============================================================================
//  listFiles
// ============================================================================

MpqResult MPQReader::listFiles(std::vector<MpqFileEntry>& out,
                               const std::string& filter) const {
    if (!mapBase_) return MpqResult::InvalidHandle;
    out.clear();

    std::vector<uint8_t> raw;
    MpqResult r = extractRaw("(listfile)", raw);
    if (r != MpqResult::Ok) return MpqResult::ListfileMissing;

    std::string content(reinterpret_cast<const char*>(raw.data()), raw.size());
    std::string filterLower = filter;
    std::transform(filterLower.begin(), filterLower.end(),
                   filterLower.begin(), ::tolower);

    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (!filterLower.empty()) {
            std::string lineLower = line;
            std::transform(lineLower.begin(), lineLower.end(),
                           lineLower.begin(), ::tolower);
            if (lineLower.find(filterLower) == std::string::npos) continue;
        }

        MpqFileEntry e;
        e.path = line;

        int bi2 = findBlockIndex(line);
        if (bi2 >= 0 && bi2 < btEntries_) {
            int bo2 = bi2 * 16;
            e.compSize   = static_cast<uint32_t>(ri(blockTable_.data(), bo2 + 4));
            e.fileSize   = static_cast<uint32_t>(ri(blockTable_.data(), bo2 + 8));
            uint32_t fl  = ru(blockTable_.data(), bo2 + 12);
            e.encrypted  = (fl & 0x00010000u) != 0;
            e.compressed = (fl & 0x00000100u) != 0;
        }

        out.push_back(std::move(e));
    }
    return MpqResult::Ok;
}

// ============================================================================
//  listPaths
// ============================================================================

MpqResult MPQReader::listPaths(std::vector<std::string>& out,
                               const std::string& filter) const {
    std::vector<MpqFileEntry> entries;
    MpqResult r = listFiles(entries, filter);
    if (r != MpqResult::Ok) return r;
    out.reserve(entries.size());
    for (auto& e : entries) out.push_back(std::move(e.path));
    return MpqResult::Ok;
}

// ============================================================================
//  MPQManager
// ============================================================================

int MPQManager::openArchive(const std::string& path) {
    archives_.emplace_back();
    MpqResult r = archives_.back().open(path);
    if (r != MpqResult::Ok) {
        archives_.pop_back();
        return static_cast<int>(r);
    }
    return static_cast<int>(archives_.size()) - 1;
}

void MPQManager::closeArchive(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(archives_.size())) return;
    archives_[index].close();
}

void MPQManager::closeAll() noexcept {
    for (auto& a : archives_) a.close();
    archives_.clear();
}

int MPQManager::archiveCount() const noexcept {
    return static_cast<int>(archives_.size());
}

bool MPQManager::isOpen(int index) const noexcept {
    if (index < 0 || index >= static_cast<int>(archives_.size())) return false;
    return archives_[index].isOpen();
}

MpqResult MPQManager::listAllFiles(std::vector<MpqFileEntry>& out,
                                   const std::string& filter) const {
    out.clear();
    std::unordered_set<std::string> seen;
    std::vector<MpqFileEntry> tmp;

    for (const auto& a : archives_) {
        if (!a.isOpen()) continue;
        tmp.clear();
        a.listFiles(tmp, filter);
        for (auto& e : tmp) {
            std::string key = e.path;
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            if (seen.insert(key).second)
                out.push_back(std::move(e));
        }
    }
    return MpqResult::Ok;
}

MpqResult MPQManager::listAllPaths(std::vector<std::string>& out,
                                   const std::string& filter) const {
    std::vector<MpqFileEntry> entries;
    MpqResult r = listAllFiles(entries, filter);
    if (r != MpqResult::Ok) return r;
    out.reserve(entries.size());
    for (auto& e : entries) out.push_back(std::move(e.path));
    return MpqResult::Ok;
}

int MPQManager::findArchiveContaining(const std::string& innerPath) const noexcept {
    for (int i = 0; i < static_cast<int>(archives_.size()); ++i) {
        if (archives_[i].isOpen() && archives_[i].hasFile(innerPath))
            return i;
    }
    return -1;
}

MpqResult MPQManager::extractFile(const std::string& innerPath,
                                  std::vector<uint8_t>& out) const {
    for (const auto& a : archives_) {
        if (!a.isOpen()) continue;
        MpqResult r = a.extractFile(innerPath, out);
        if (r == MpqResult::Ok)          return MpqResult::Ok;
        if (r != MpqResult::FileNotFound) return r;
    }
    return MpqResult::FileNotFound;
}

MpqResult MPQManager::extractFileToDisk(const std::string& innerPath,
                                        const std::string& destPath) const {
    for (const auto& a : archives_) {
        if (!a.isOpen()) continue;
        MpqResult r = a.extractFileToDisk(innerPath, destPath);
        if (r == MpqResult::Ok)          return MpqResult::Ok;
        if (r != MpqResult::FileNotFound) return r;
    }
    return MpqResult::FileNotFound;
}

std::vector<MpqBatchEntry> MPQManager::extractBatch(
    const std::vector<std::string>& paths) const
{
    std::vector<MpqBatchEntry> results(paths.size());
    for (size_t i = 0; i < paths.size(); ++i)
        results[i].path = paths[i];

    // IMPROVED: Dispatch to shared pool instead of unbounded std::async
    auto* pool = getSharedPool();

    for (size_t i = 0; i < paths.size(); ++i) {
        whiteout::interfaces::WorkerTask task;
        task.fn = [this, &results, i]() {
            for (const auto& a : archives_) {
                if (!a.isOpen()) continue;
                MpqResult r = a.extractFile(results[i].path,
                                           results[i].data);
                if (r == MpqResult::Ok) {
                    results[i].result = MpqResult::Ok;
                    return;
                }
                if (r != MpqResult::FileNotFound) {
                    results[i].result = r;
                    return;
                }
            }
            results[i].result = MpqResult::FileNotFound;
        };
        pool->submit(task);
    }

    pool->waitIdle();
    return results;
}

} // namespace whiteoutdex
