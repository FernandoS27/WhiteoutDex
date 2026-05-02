// ============================================================================
// WindowsSoundEmitter — implementation.
// ============================================================================

#include "windows_sound_emitter.h"

#include "../io/content_provider.h"
#include "../io/event_data.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace WhiteoutDex {

namespace {

// Resolve a SndEntry to a playable WAV byte buffer. `entry.filePaths`
// is the engine's MASTERSOUNDENTRY::m_SoundInstances list, already
// resolved at SLK load time to either an asset table's Filepath
// (e.g. "Units/Human/Peasant/PeasantDeath.flac") or a bare-stem token.
//
// Per-path probing: PlaySoundW ONLY accepts `.wav` — handing it FLAC
// bytes silently no-ops (no error, no audio), so we cannot use the
// path as-authored when its extension isn't `.wav`. Reforged ships
// most sound stems as both `.flac` (HD/studio) and `.wav` (legacy SD)
// siblings, so we substitute every non-wav extension to `.wav`. If a
// path already ends in `.wav` we use it directly.
//
// Across-paths probing: pick a random candidate first to vary playback,
// then fall through the rest in order so per-variant gaps in CASC
// (e.g. AnimSounds row PeasantDeath lists 4 stems but only one of
// them — PeasantDeath1 → "PeasantDeath.wav" — has an SD `.wav`
// sibling) don't silence the whole emitter.
std::optional<std::vector<uint8_t>> ResolveSoundBytes(
    const IContentProvider& cp,
    const io::SndEntry&     entry) {

    if (entry.filePaths.empty()) return std::nullopt;

    // Build the .wav-extension variant of a path. Lower-cases the
    // existing extension before comparing so `.WAV` / `.Wav` aren't
    // re-substituted into `.wav` (would still produce the same string,
    // but the equality check lets us short-circuit when no rewrite
    // is needed).
    auto to_wav = [](const std::string& path) -> std::string {
        const auto sepPos = path.find_last_of("/\\");
        const auto dotPos = path.rfind('.');
        const bool hasExt =
            dotPos != std::string::npos &&
            (sepPos == std::string::npos || dotPos > sepPos);
        if (!hasExt) return path + ".wav";
        std::string ext = path.substr(dotPos);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".wav") return path;
        return path.substr(0, dotPos) + ".wav";
    };

    auto try_path = [&](const std::string& p)
        -> std::optional<std::vector<uint8_t>> {
        if (p.empty()) return std::nullopt;
        return cp.ReadFile(p, nullptr);
    };

    static thread_local std::mt19937 rng{std::random_device{}()};
    const size_t n     = entry.filePaths.size();
    const size_t start = (n == 1) ? 0 : (rng() % n);
    for (size_t i = 0; i < n; ++i) {
        if (auto bytes = try_path(to_wav(entry.filePaths[(start + i) % n])))
            return bytes;
    }
    return std::nullopt;
}

// Scan a WAV byte buffer for the data chunk and scale every PCM sample
// in place by `gain`. Touches only PCM 8-bit (unsigned, 128 = silence)
// and PCM 16-bit (signed) — the common formats for WC3 SND samples.
// Non-PCM (formatTag != 1: float, ADPCM, …) and unknown bit depths are
// left as-is; the user just won't hear the gain on those rare assets.
//
// WAV layout (little-endian):
//   0..3   "RIFF"
//   4..7   file size − 8 (uint32)
//   8..11  "WAVE"
//   then a sequence of chunks:
//     0..3   chunk id (e.g. "fmt ", "data", "LIST", "JUNK")
//     4..7   chunk size (uint32)
//     8..    chunk payload (padded to even length)
//
// We rely on the layout being well-formed; PlaySound itself will reject
// malformed buffers, so a partial parse just leaves the bytes alone.
void ScalePcmGain(uint8_t* buf, size_t size, float gain) {
    if (gain == 1.0f || size < 44) return;
    if (std::memcmp(buf, "RIFF", 4) != 0) return;
    if (std::memcmp(buf + 8, "WAVE", 4) != 0) return;

    uint16_t formatTag    = 0;
    uint16_t bitsPerSample = 0;

    auto rd_u32 = [](const uint8_t* p) {
        return static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
    };
    auto rd_u16 = [](const uint8_t* p) {
        return static_cast<uint16_t>(
            static_cast<uint16_t>(p[0]) |
            (static_cast<uint16_t>(p[1]) << 8));
    };

    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* hdr  = buf + pos;
        const uint32_t csz  = rd_u32(hdr + 4);
        const size_t   next = pos + 8 + ((csz + 1u) & ~1u);  // pad to even

        if (std::memcmp(hdr, "fmt ", 4) == 0 && csz >= 16 && pos + 8 + 16 <= size) {
            formatTag     = rd_u16(hdr + 8);
            bitsPerSample = rd_u16(hdr + 8 + 14);
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            // Bail if we never saw a fmt chunk or it's not raw PCM.
            if (formatTag != 1) return;
            const size_t dataOff = pos + 8;
            if (dataOff > size) return;
            const size_t dataSz = std::min(static_cast<size_t>(csz), size - dataOff);

            if (bitsPerSample == 16) {
                int16_t* samples = reinterpret_cast<int16_t*>(buf + dataOff);
                const size_t n   = dataSz / sizeof(int16_t);
                for (size_t i = 0; i < n; ++i) {
                    int s = static_cast<int>(static_cast<float>(samples[i]) * gain);
                    if (s >  32767) s =  32767;
                    if (s < -32768) s = -32768;
                    samples[i] = static_cast<int16_t>(s);
                }
            } else if (bitsPerSample == 8) {
                // 8-bit PCM is unsigned with 128 as silence; centre,
                // scale, re-bias.
                uint8_t* samples = buf + dataOff;
                for (size_t i = 0; i < dataSz; ++i) {
                    int centred = static_cast<int>(samples[i]) - 128;
                    int scaled  = static_cast<int>(static_cast<float>(centred) * gain);
                    int out     = scaled + 128;
                    if (out > 255) out = 255;
                    if (out <   0) out =   0;
                    samples[i] = static_cast<uint8_t>(out);
                }
            }
            return;
        }

        if (next <= pos) return;   // malformed: zero/negative chunk
        pos = next;
    }
}

} // namespace

WindowsSoundEmitter::WindowsSoundEmitter(const IContentProvider* content)
    : content_(content) {}

void WindowsSoundEmitter::SetVolume(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    volume_.store(v, std::memory_order_relaxed);
}

float WindowsSoundEmitter::GetVolume() const {
    return volume_.load(std::memory_order_relaxed);
}

WindowsSoundEmitter::~WindowsSoundEmitter() {
    // Stop any in-flight playback so the OS doesn't keep reading from
    // currentBuffer_ after we destroy it.
    PlaySoundW(nullptr, nullptr, SND_PURGE);
}

void WindowsSoundEmitter::Play(const io::SndEntry& entry,
                               const Vector3f&     /*worldPos*/) {
    if (!content_) return;

    auto bytes = ResolveSoundBytes(*content_, entry);
    if (!bytes) {
        // Log on miss — every candidate path (including the .wav
        // substitution) failed to resolve through the content
        // provider. Either the user has a partial install, the
        // SLK row points at an extension we don't probe yet, or
        // the bare-stem token had no matching asset-table entry
        // and isn't a real CASC path on its own.
        const char* sample =
            entry.filePaths.empty() ? "<empty>" : entry.filePaths.front().c_str();
        std::fprintf(stderr, "[WDEX sound] missing - first candidate '%s' "
                             "(tried %zu paths, .wav substitution)\n",
                     sample, entry.filePaths.size());
        return;
    }

    // Hand-off to PlaySoundW. SND_MEMORY: the LPCWSTR is actually a
    // pointer to in-memory WAV bytes. SND_ASYNC: return immediately,
    // OS plays in the background. The buffer must remain valid until
    // playback ends — we keep it pinned in `currentBuffer_`. When the
    // next call lands, PlaySoundW(nullptr, …, SND_PURGE) inside the
    // dtor (or the implicit cancel from the next PlaySoundW) releases
    // the OS's borrow before we mutate the vector.
    std::lock_guard<std::mutex> lk(mu_);
    PlaySoundW(nullptr, nullptr, SND_PURGE);   // cancel any prior borrow
    currentBuffer_ = std::move(*bytes);

    // Apply the master gain. Skipped (and ScalePcmGain returns
    // immediately) when volume == 1.0, so the common case is free.
    ScalePcmGain(currentBuffer_.data(), currentBuffer_.size(),
                 volume_.load(std::memory_order_relaxed));

    PlaySoundW(reinterpret_cast<LPCWSTR>(currentBuffer_.data()),
               nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace WhiteoutDex
