// ============================================================================
// WindowsSoundEmitter — implementation.
// ============================================================================

#include "windows_sound_emitter.h"

#include "../io/content_provider.h"
#include "../io/event_data.h"

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

// Compose `<filepath>\<sample>` and resolve through the content
// provider. AnimSounds.slk carries the studio source extension —
// usually `.flac` in Reforged, occasionally `.mp3` — but the only
// format `PlaySoundW` understands is `.wav`, and the playback-ready
// `.wav` ships alongside the source under the same stem. So the
// resolution chain is:
//   1. Try the path as-authored (handles old maps that already store
//      .wav stems and any future codec we add).
//   2. Strip whatever extension is there and try `.wav` —
//      Reforged's per-sample stems all have `.wav` siblings.
//   3. Append `.wav` when the SLK had no extension at all.
// Returns the bytes from the first hit or std::nullopt on miss.
std::optional<std::vector<uint8_t>> ResolveSoundBytes(
    const IContentProvider& cp,
    const io::SndEntry&     entry) {

    if (entry.fileNames.empty()) return std::nullopt;

    // Pick a random sample from the row when there are several.
    // AnimSounds.FileNames is comma-split; the engine picks among
    // them at random per fire to vary the audio.
    static thread_local std::mt19937 rng{std::random_device{}()};
    const size_t idx = (entry.fileNames.size() == 1) ? 0
        : (rng() % entry.fileNames.size());

    const std::string& sample = entry.fileNames[idx];
    std::string base = entry.filepath;
    if (!base.empty() && base.back() != '\\' && base.back() != '/') base += '\\';
    const std::string fullPath = base + sample;

    auto try_path = [&](const std::string& p)
        -> std::optional<std::vector<uint8_t>> {
        return cp.ReadFile(p, nullptr);
    };

    // 1. As-authored (covers .wav stems already in the SLK).
    if (auto bytes = try_path(fullPath)) return bytes;

    // Locate the extension — only count a `.` that lands after the
    // last directory separator so paths like `dir.subdir/file` don't
    // get truncated.
    const auto sepPos = fullPath.find_last_of("/\\");
    const auto dotPos = fullPath.rfind('.');
    const bool hasExt =
        dotPos != std::string::npos &&
        (sepPos == std::string::npos || dotPos > sepPos);

    // 2. Strip studio-source extension (.flac / .mp3 / …), substitute .wav.
    if (hasExt) {
        if (auto bytes = try_path(fullPath.substr(0, dotPos) + ".wav"))
            return bytes;
    } else {
        // 3. SLK had no extension at all — try plain .wav.
        if (auto bytes = try_path(fullPath + ".wav")) return bytes;
    }

    return std::nullopt;
}

} // namespace

WindowsSoundEmitter::WindowsSoundEmitter(const IContentProvider* content)
    : content_(content) {}

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
        // Log once per fire on miss. We tried the path as-authored
        // and a .wav substitution — neither resolved through the
        // content provider. Either the user has a partial install,
        // the path needs another extension we don't probe yet, or
        // the SLK row is malformed.
        const char* sample =
            entry.fileNames.empty() ? "<empty>" : entry.fileNames.front().c_str();
        const char* dir =
            entry.filepath.empty() ? "<root>" : entry.filepath.c_str();
        std::fprintf(stderr, "[WDEX sound] missing — dir='%s' sample='%s' "
                             "(also tried .wav substitution)\n",
                     dir, sample);
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
    PlaySoundW(reinterpret_cast<LPCWSTR>(currentBuffer_.data()),
               nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace WhiteoutDex
