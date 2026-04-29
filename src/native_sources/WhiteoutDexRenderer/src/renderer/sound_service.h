#pragma once
// ============================================================================
// SoundService — abstract 3D-audio backend for MDX EventObject (SND) firings.
//
// First slice ships only the LoggingSoundService implementation, which prints
// the chosen WAV path and source position to stdout. A real XAudio2-backed
// implementation can be slotted in later by swapping the unique_ptr held on
// RenderService — call sites in EventEmitterPool stay identical.
// ============================================================================

#include "../io/event_data.h"   // SndEntry
#include "types.h"              // Vector3f

#include <memory>

namespace WhiteoutDex {

class SoundService {
public:
    virtual ~SoundService() = default;

    // Emit one shot of the given AnimSounds row at the world position of
    // the firing EventObject node. Implementations are responsible for
    // picking a random buffer from `entry.fileNames`, applying volume /
    // distance attenuation, and managing voice lifetimes.
    virtual void Play(const io::SndEntry& entry, const Vector3f& worldPos) = 0;
};

class LoggingSoundService : public SoundService {
public:
    void Play(const io::SndEntry& entry, const Vector3f& worldPos) override;
};

// Convenience factory — keeps RenderService construction trivial.
inline std::unique_ptr<SoundService> MakeDefaultSoundService() {
    return std::make_unique<LoggingSoundService>();
}

} // namespace WhiteoutDex
