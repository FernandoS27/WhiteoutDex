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

// dr_flac (single-header, MIT/public-domain). This TU is the sole
// implementation site — every other includer must NOT define
// DR_FLAC_IMPLEMENTATION to avoid duplicate symbols.
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG  // we never feed Ogg-encapsulated FLAC; saves a chunk of binary
#include "third_party/dr_flac.h"

#pragma comment(lib, "winmm.lib")

namespace WhiteoutDex {

namespace {

// Wrap interleaved 16-bit PCM samples in a minimal RIFF/WAVE container
// so PlaySoundW (which only accepts WAVE) can play decoded FLAC. Layout
// is the canonical 44-byte WAVE header + raw `data` payload.
//   0..3   "RIFF"
//   4..7   total size - 8  (uint32 LE)
//   8..11  "WAVE"
//  12..15  "fmt "
//  16..19  16              (PCM fmt chunk size)
//  20..21  1               (PCM format tag)
//  22..23  channels        (uint16)
//  24..27  sampleRate      (uint32)
//  28..31  byteRate        = sampleRate * channels * 2
//  32..33  blockAlign      = channels * 2
//  34..35  16              (bitsPerSample)
//  36..39  "data"
//  40..43  payloadBytes    (uint32)
std::vector<uint8_t> WrapPcmS16AsWav(const drflac_int16* samples,
                                      drflac_uint64       totalFrames,
                                      unsigned int        channels,
                                      unsigned int        sampleRate) {
    const uint32_t payloadBytes =
        static_cast<uint32_t>(totalFrames) *
        static_cast<uint32_t>(channels)    * sizeof(int16_t);
    const uint32_t totalBytes = 44u + payloadBytes;

    std::vector<uint8_t> wav;
    wav.resize(totalBytes);
    auto put32 = [&](size_t off, uint32_t v) {
        wav[off]     = static_cast<uint8_t>(v        & 0xFF);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        wav[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        wav[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    auto put16 = [&](size_t off, uint16_t v) {
        wav[off]     = static_cast<uint8_t>(v        & 0xFF);
        wav[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    std::memcpy(wav.data() + 0, "RIFF", 4);
    put32(4, totalBytes - 8);
    std::memcpy(wav.data() + 8,  "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);
    put32(16, 16);
    put16(20, 1);                           // PCM
    put16(22, static_cast<uint16_t>(channels));
    put32(24, sampleRate);
    put32(28, sampleRate * channels * 2);   // byteRate
    put16(32, static_cast<uint16_t>(channels * 2));  // blockAlign
    put16(34, 16);                          // bitsPerSample
    std::memcpy(wav.data() + 36, "data", 4);
    put32(40, payloadBytes);
    if (payloadBytes > 0) {
        std::memcpy(wav.data() + 44, samples, payloadBytes);
    }
    return wav;
}

// Decode a FLAC byte buffer to a WAVE-wrapped PCM s16 buffer. Returns
// nullopt on bad/unsupported input. Reforged FLAC assets are typically
// 16-bit, mono or stereo; dr_flac handles 24-bit by truncation, so we
// silently downconvert on the rare studio-master asset.
std::optional<std::vector<uint8_t>> DecodeFlacToWav(const std::vector<uint8_t>& flac) {
    unsigned int    channels    = 0;
    unsigned int    sampleRate  = 0;
    drflac_uint64   totalFrames = 0;
    drflac_int16*   samples     = drflac_open_memory_and_read_pcm_frames_s16(
        flac.data(), flac.size(), &channels, &sampleRate, &totalFrames, nullptr);
    if (!samples || channels == 0 || sampleRate == 0 || totalFrames == 0) {
        if (samples) drflac_free(samples, nullptr);
        return std::nullopt;
    }
    auto wav = WrapPcmS16AsWav(samples, totalFrames, channels, sampleRate);
    drflac_free(samples, nullptr);
    return wav;
}

// Resolve a SndEntry to a playable WAV byte buffer. `entry.filePaths`
// is the engine's MASTERSOUNDENTRY::m_SoundInstances list, already
// resolved at SLK load time to either an asset table's Filepath
// (e.g. "Units/Human/Peasant/PeasantDeath.flac") or a bare-stem token.
//
// PlaySoundW ONLY accepts WAVE — handing it FLAC bytes silently no-ops
// (no error, no audio). So for each source path we probe a small ladder
// of candidates and convert FLAC to WAVE when needed:
//   1. As-authored path. If `.flac` (or detected by magic), decode via
//      dr_flac and wrap as WAVE; if `.wav`, return raw.
//   2. Same path with extension forced to `.wav` (legacy SD CASC may
//      ship a `.wav` sibling alongside the HD `.flac`).
//   3. Same path with extension forced to `.flac` and decoded
//      (covers asset entries authored without an extension or with an
//      odd extension like `.ogg` we don't decode).
//   4. Trailing-digit-strip variants of #2 and #3 (Reforged HD asset
//      map keeps the variant digit, but the SD CASC often stores one
//      legacy file with the digit dropped — e.g. `PeasantDeath1.flac`
//      vs `PeasantDeath.wav`).
//
// Across-paths probing: pick a random candidate first to vary playback,
// then fall through the rest in order so per-variant gaps in CASC
// don't silence the whole emitter.
std::optional<std::vector<uint8_t>> ResolveSoundBytes(
    const IContentProvider& cp,
    const io::SndEntry&     entry,
    std::string*            attemptedOut) {

    if (entry.filePaths.empty()) return std::nullopt;

    // Split a path into directory, stem, and lowercased extension.
    // `hasExt` is false when no `.` appears after the last separator.
    struct PathParts {
        std::string dir;     // includes trailing separator if any
        std::string stem;
        std::string extLow;  // lowercased, includes the leading dot
        bool        hasExt;
    };
    auto split = [](const std::string& path) -> PathParts {
        const auto sepPos = path.find_last_of("/\\");
        const auto dotPos = path.rfind('.');
        const bool hasExt =
            dotPos != std::string::npos &&
            (sepPos == std::string::npos || dotPos > sepPos);
        const size_t baseStart = (sepPos == std::string::npos) ? 0 : sepPos + 1;
        PathParts p;
        p.dir    = path.substr(0, baseStart);
        p.stem   = hasExt ? path.substr(baseStart, dotPos - baseStart)
                          : path.substr(baseStart);
        p.extLow = hasExt ? path.substr(dotPos) : std::string{};
        for (auto& c : p.extLow) c = (char)std::tolower((unsigned char)c);
        p.hasExt = hasExt;
        return p;
    };

    // Heuristic: bytes are FLAC iff they start with the "fLaC" magic.
    auto looks_like_flac = [](const std::vector<uint8_t>& b) {
        return b.size() >= 4 && b[0] == 'f' && b[1] == 'L' &&
                                b[2] == 'a' && b[3] == 'C';
    };

    // Read a path, decode if FLAC, return WAVE-wrapped bytes (or raw
    // WAVE if the file already is one). nullopt = path missing or
    // FLAC decode failed.
    auto fetch = [&](const std::string& p, bool decodeFlac)
        -> std::optional<std::vector<uint8_t>> {
        if (p.empty()) return std::nullopt;
        auto bytes = cp.ReadFile(p, nullptr);
        if (!bytes) return std::nullopt;
        if (decodeFlac && looks_like_flac(*bytes)) {
            auto wav = DecodeFlacToWav(*bytes);
            if (!wav) return std::nullopt;
            return wav;
        }
        return bytes;
    };

    // Build the probe ladder (path, decode-as-flac) for one source.
    auto candidates = [&](const std::string& path)
        -> std::vector<std::pair<std::string, bool>> {
        std::vector<std::pair<std::string, bool>> out;
        const PathParts p = split(path);

        // Stem variants: as-authored, then trailing-digit-stripped if
        // it actually reduces the stem.
        std::vector<std::string> stems{ p.stem };
        size_t end = p.stem.size();
        while (end > 0 && p.stem[end - 1] >= '0' && p.stem[end - 1] <= '9') --end;
        if (end > 0 && end < p.stem.size()) {
            stems.push_back(p.stem.substr(0, end));
        }

        auto push = [&](std::string path, bool flac) {
            for (const auto& [existing, _] : out) {
                if (existing == path) return;
            }
            out.emplace_back(std::move(path), flac);
        };

        // 1: as-authored (decode if `.flac`, raw if `.wav`, FLAC-magic
        // sniff covers extension-less or unknown-ext entries).
        if (p.hasExt) push(path, p.extLow == ".flac");
        else          push(path, true);

        // 2/3 per stem variant: forced .wav, then forced .flac.
        for (const auto& s : stems) {
            push(p.dir + s + ".wav",  false);
            push(p.dir + s + ".flac", true);
        }
        return out;
    };

    static thread_local std::mt19937 rng{std::random_device{}()};
    const size_t n     = entry.filePaths.size();
    const size_t start = (n == 1) ? 0 : (rng() % n);
    for (size_t i = 0; i < n; ++i) {
        for (const auto& [path, decodeFlac] : candidates(entry.filePaths[(start + i) % n])) {
            if (auto bytes = fetch(path, decodeFlac)) return bytes;
            if (attemptedOut) {
                if (!attemptedOut->empty()) attemptedOut->append(", ");
                attemptedOut->append(path);
            }
        }
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

    std::string attempted;
    auto bytes = ResolveSoundBytes(*content_, entry, &attempted);
    if (!bytes) {
        // Log on miss with the full list of CASC paths we probed.
        // Every candidate (including the .wav substitution, the
        // trailing-digit-strip fallback, and the FLAC->WAV decode)
        // failed to resolve through the content provider. Likely:
        // partial install, SLK row points at an unsupported extension,
        // or the asset is referenced but doesn't ship.
        // ASCII '--' (no em dash) so the message renders correctly on
        // a CP1252 console.
        std::fprintf(stderr,
                     "[WDEX sound] missing -- %zu source path(s); probed: %s\n",
                     entry.filePaths.size(),
                     attempted.empty() ? "<none>" : attempted.c_str());
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
