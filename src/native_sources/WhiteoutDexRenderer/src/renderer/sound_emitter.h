#pragma once
// ============================================================================
// ISoundEmitter — host-supplied 3D audio backend for MDX SND EventObjects.
//
// The renderer needs to play one-shot WAV/MP3 samples when an animation
// fires a SND track, but it deliberately doesn't ship its own audio
// implementation. The host (standalone exe, Max plugin, …) supplies a
// concrete ISoundEmitter via RenderService::SetSoundEmitter; until one
// is set, RenderService keeps a NullSoundEmitter that drops every fire
// silently. That keeps the renderer library free of platform-specific
// audio dependencies (winmm, XAudio2, etc.) and lets each host pick its
// own backend.
//
// Threading: Play() is invoked from the render thread inside
// EventEmitterPool::Tick. Implementations must be safe to call from
// there — typically by posting to a dedicated audio thread or using
// an async API like PlaySoundW(SND_ASYNC). They must also tolerate
// being destroyed while sounds may still be playing (cancel on dtor).
// ============================================================================

#include "types.h"   // Vector3f

#include <memory>

namespace WhiteoutDex {

namespace io { struct SndEntry; }

class ISoundEmitter {
public:
    virtual ~ISoundEmitter() = default;

    // Trigger one shot of the given sound row at the world position of
    // the firing EventObject node. Implementations are responsible for
    // picking one of `entry.filePaths` and reading it through their
    // content provider, applying volume and distance attenuation
    // against entry.minDistance / maxDistance / distanceCutoff, and
    // managing voice lifetimes.
    virtual void Play(const io::SndEntry& entry, const Vector3f& worldPos) = 0;

    // Per-emitter master gain in [0, 1]. Default is 1.0 (full volume).
    // Backends are expected to multiply this against any per-sample /
    // per-row gain. NullSoundEmitter ignores the value (it never plays);
    // backends that can't honour gain at all may also no-op. Get/Set
    // are virtual so RenderService can route a UI slider through here
    // without leaking the concrete backend type.
    virtual void  SetVolume(float /*v*/) {}
    virtual float GetVolume() const { return 1.0f; }
};

// Default no-op implementation. Used by RenderService when no host has
// supplied a concrete emitter via SetSoundEmitter. Drops every fire.
class NullSoundEmitter final : public ISoundEmitter {
public:
    void Play(const io::SndEntry&, const Vector3f&) override {}
};

// Construct the renderer-default emitter. RenderService calls this in
// its constructor so soundEmitter_ is always non-null — the dispatch
// path in EventEmitterPool can then unconditionally invoke
// soundEmitter_->Play() without a null check.
inline std::unique_ptr<ISoundEmitter> MakeNullSoundEmitter() {
    return std::make_unique<NullSoundEmitter>();
}

} // namespace WhiteoutDex
