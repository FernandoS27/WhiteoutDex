#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Simulation Utilities (shared by
// ParticleSystem, RibbonSystem, PE1System)
// ============================================================================

#include "constants.h"
#include <algorithm>

namespace WhiteoutDex {

/// Clamp a simulation delta-time to the engine-safe range [0, kMaxSimulationDt].
inline float ClampDeltaTime(float dt) {
    return std::clamp(dt, 0.0f, kMaxSimulationDt);
}

/// Returns true if the emitter visibility is high enough to be considered active.
inline bool IsEmitterVisible(float visibility) {
    return visibility > kMinVisibilityThreshold;
}

} // namespace WhiteoutDex
