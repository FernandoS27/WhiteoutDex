#pragma once
// ============================================================================
// WindowsSoundEmitter — winmm-backed ISoundEmitter for Windows hosts.
//
// Uses Win32 PlaySoundW with SND_MEMORY | SND_ASYNC: each Play() call
// reads the SLK row's first WAV via the supplied content provider,
// caches the bytes (because PlaySound borrows the buffer for the
// duration of playback), and asks the OS to play it. The previous
// pending sound is implicitly stopped — Win32's single-buffer mixer
// only ever plays one sound at a time per process when invoked this
// way. That's a deliberate first-cut: WC3 model previews fire at most
// one or two SND events per second for a single model, so overlap is
// rare. A multi-voice XAudio2 backend is the documented next step.
//
// Limitations: WAV-only (no MP3/OGG decode), single voice, no 3D
// positioning, no distance attenuation. Volume is honoured by
// pre-scaling the sample buffer would be cleanest but PlaySound
// doesn't support per-call gain — the SLK row's volume field is
// currently advisory.
//
// Threading: Play() can be called from the render thread (matches the
// EventEmitterPool dispatch site). PlaySoundW is async + thread-safe.
// The content provider must outlive the emitter (the host scene's
// FileContentProvider satisfies this).
// ============================================================================

#include "sound_emitter.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace WhiteoutDex {

class IContentProvider;

class WindowsSoundEmitter : public ISoundEmitter {
public:
    explicit WindowsSoundEmitter(const IContentProvider* content);
    ~WindowsSoundEmitter() override;

    WindowsSoundEmitter(const WindowsSoundEmitter&)            = delete;
    WindowsSoundEmitter& operator=(const WindowsSoundEmitter&) = delete;

    void Play(const io::SndEntry& entry, const Vector3f& worldPos) override;

private:
    const IContentProvider* content_ = nullptr;

    // PlaySound with SND_MEMORY borrows the buffer pointer until the
    // sound finishes (or until the next PlaySound call cancels it).
    // We hold one buffer alive across calls; a fresh Play() either
    // overwrites it or replaces it. Mutex serialises swaps so the
    // previous sound's borrow is safely cancelled before the next
    // one starts.
    std::mutex           mu_;
    std::vector<uint8_t> currentBuffer_;
};

} // namespace WhiteoutDex
