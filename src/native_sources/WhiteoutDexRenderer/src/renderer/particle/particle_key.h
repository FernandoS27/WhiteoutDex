#pragma once
// ============================================================================
// CParticleKey port — 2-key colour / scale / sprite-cell keyframe for PE2.
//
// Each emitter holds exactly two keys. Timing semantics:
//   keys[0] covers [0, keys[0].endTime]              (life phase)
//   keys[1] covers [keys[0].endTime, keys[1].endTime] (decay phase)
// Both endTimes are ABSOLUTE particle ages. Particle expires when
//   p.age >= emitter.lifeSpan || keyFrame >= 2.
//
// Interpolate() uses the per-key t = (p.age - prevEndTime) / (endTime - prevEndTime)
// interpretation (authoring-intent; disasm not yet transcribed, see
// docs/PARTICLEEMITTERS2.md §8.8).
// ============================================================================

#include "types.h"
#include <cstdint>

namespace WhiteoutDex::particle {

// 8-bit BGRA colour. Byte order in memory: [a, r, g, b] (alpha at byte 0).
// Matches the RE CImVector layout verified via CombineColors.
struct ImVector {
    uint8_t a = 0, r = 0, g = 0, b = 0;

    // Construct from floats in [0,1]. Matches the MDX loader's truncation:
    // byte = (int)(255.0 * f). Values outside [0,1] are clamped.
    static ImVector FromFloat(float rf, float gf, float bf, float af) {
        auto clamp8 = [](float v) -> uint8_t {
            if (v <= 0.0f) return 0;
            if (v >= 1.0f) return 255;
            return static_cast<uint8_t>(v * 255.0f);
        };
        return { clamp8(af), clamp8(rf), clamp8(gf), clamp8(bf) };
    }

    Vector4f ToVec4() const {
        return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    }
};

struct ParticleKey {
    float    endTime        = 0.0f;     // absolute particle age at end of this key
    ImVector startColor;
    ImVector endColor;
    float    startScale     = 1.0f;
    float    endScale       = 1.0f;
    int      headCellStart  = 0;
    int      headCellEnd    = 0;
    int      headCellRepeat = 0;        // 0 = hold on headCellStart
    int      tailCellStart  = 0;
    int      tailCellEnd    = 0;
    int      tailCellRepeat = 0;

    // age is absolute particle age. prevEndTime is keys[kf-1].endTime (0 when
    // kf == 0). Writes into the out-parameters; all callers want all four.
    void Interpolate(float age,
                     float prevEndTime,
                     ImVector& outColor,
                     int& outHeadCell,
                     int& outTailCell,
                     float& outScale) const;
};

} // namespace WhiteoutDex::particle
