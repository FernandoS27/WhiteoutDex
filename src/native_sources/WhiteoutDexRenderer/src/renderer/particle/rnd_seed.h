#pragma once
// ============================================================================
// Port of NTempest::CRndSeed + NTempest::CRandom for the PE2 particle system.
//
// The WC3 engine uses a 32-bit LCG; its exact update constants are not in the
// reverse-engineered pseudocode folder. We ship a portable xorshift32 that
// exposes the same four sampling functions with the same output ranges:
//
//   real_(seed)     -> [0, 1)
//   reals_(seed)    -> [-1, 1)
//   C3Vector_(seed) -> unit vector on the 2-sphere
//   dice_(n, seed)  -> [0, n)
//
// This gives per-emitter determinism (same seed -> same sequence) and matches
// the value distributions, but does NOT reproduce the exact WC3 trajectories.
// See docs/PARTICLEEMITTERS2.md §3.2.
// ============================================================================

#include "types.h"
#include <cstdint>

namespace WhiteoutDex::particle {

struct RndSeed {
    uint32_t state;     // xorshift32 state; must never be 0

    // Default-constructed state of 0 matches the ctor pattern in the RE disasm
    // (CRndSeed(0) before SetSeed). SetSeed must be called before sampling.
    RndSeed() : state(0) {}
    explicit RndSeed(uint32_t seed) { SetSeed(seed); }

    void SetSeed(uint32_t seed) {
        // xorshift32 breaks on state == 0; fold a non-zero constant in.
        state = (seed == 0) ? 0x9E3779B9u : seed;
    }
};

namespace CRandom {

// Raw 32-bit advance. Returns the new state.
inline uint32_t next_u32(RndSeed& s) {
    uint32_t x = s.state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.state = x;
    return x;
}

// Uniform float in [0, 1). 24-bit mantissa precision.
inline float real_(RndSeed& s) {
    // Take top 24 bits, divide by 2^24.
    return (next_u32(s) >> 8) * (1.0f / 16777216.0f);
}

// Uniform float in [-1, 1).
inline float reals_(RndSeed& s) {
    return real_(s) * 2.0f - 1.0f;
}

// Uniform unit vector on the sphere. Marsaglia's method: sample reals_ in
// [-1,1]^2, reject outside the unit disk, lift to sphere.
inline Vector3f C3Vector_(RndSeed& s) {
    float u1, u2, d2;
    do {
        u1 = reals_(s);
        u2 = reals_(s);
        d2 = u1*u1 + u2*u2;
    } while (d2 >= 1.0f || d2 == 0.0f);
    float factor = 2.0f * std::sqrt(1.0f - d2);
    return {u1 * factor, u2 * factor, 1.0f - 2.0f * d2};
}

// Integer in [0, n).
inline uint32_t dice_(uint32_t n, RndSeed& s) {
    if (n == 0) return 0;
    // Simple modulo; bias is negligible for n << 2^32.
    return next_u32(s) % n;
}

} // namespace CRandom

// Engine-wide seed for the compaction dice-roll. File-scope static in the
// original; exposed as an extern here so the service can own it.
extern RndSeed g_globalRnd;

// Seed helpers.
inline uint32_t MakeSeedFromTime(uint32_t counter) {
    // Mirrors the RE: (rand() << 16) | (rand() & 0xFFFF). We don't use libc
    // rand() (not thread-safe, platform-specific); we fold a monotonic counter
    // with a splitmix step instead.
    uint32_t x = counter + 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x7FEB352Du;
    x = (x ^ (x >> 15)) * 0x846CA68Bu;
    x =  x ^ (x >> 16);
    return x;
}

} // namespace WhiteoutDex::particle
