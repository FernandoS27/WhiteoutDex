// ============================================================================
// SoundService — LoggingSoundService implementation.
// ============================================================================

#include "sound_service.h"

#include <cstdio>

namespace WhiteoutDex {

void LoggingSoundService::Play(const io::SndEntry& entry, const Vector3f& worldPos) {
    // First filename in the row is the canonical sample; AnimSounds rows
    // typically pack 1..4 variants the engine picks from at random — we
    // log the first one for diagnostic clarity. `filepath` is a CASC
    // directory; concatenated here to match the engine resolution format.
    const char* sample = entry.fileNames.empty() ? "<empty>" : entry.fileNames.front().c_str();
    std::fprintf(stdout,
        "[WDEX sound] Play %s\\%s @ (%.2f, %.2f, %.2f) vol=%.2f\n",
        entry.filepath.c_str(), sample,
        worldPos.x, worldPos.y, worldPos.z, entry.volume);
}

} // namespace WhiteoutDex
