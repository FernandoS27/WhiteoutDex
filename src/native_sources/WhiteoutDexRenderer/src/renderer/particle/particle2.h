#pragma once
// ============================================================================
// Particle2 — POD mirror of CParticle2 (32 B in RE).
//
// Fields and offsets match the RE disasm; we use the same member order so
// memory layout stays predictable if we ever need to swap in a raw engine
// dump for testing.
//
//   +0x00  position (Vector3f, 12 B)
//   +0x0C  keyFrame (uint32, 4 B)
//   +0x10  velocity (Vector3f, 12 B)
//   +0x1C  age      (float, 4 B)
// ============================================================================

#include "types.h"
#include <cstdint>

namespace WhiteoutDex::particle {

struct Particle2 {
    Vector3f position  { 0, 0, 0 };
    uint32_t keyFrame  = 0;            // 0 or 1; 2 means expired
    Vector3f velocity  { 0, 0, 0 };
    float    age       = 0.0f;         // counts up, absolute
};

static_assert(sizeof(Particle2) == 32, "Particle2 must be 32 bytes to mirror CParticle2");

} // namespace WhiteoutDex::particle
