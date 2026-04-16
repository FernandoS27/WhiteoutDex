#include "particle_key.h"

#include <algorithm>

namespace WhiteoutDex::particle {

namespace {

inline uint8_t byte_lerp(uint8_t a, uint8_t b, float t) {
    // RE does per-channel int math at CImVector granularity; float lerp +
    // round-to-nearest is the cheap approximation that matches at the byte
    // level for all practical inputs.
    float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

inline int ComputeCell(int start, int end, int repeat, float t) {
    if (repeat == 0) return start;
    int interval = end - start + 1;
    if (interval <= 0) interval = 1;

    int frame = static_cast<int>(t * static_cast<float>(repeat));
    if (frame < 0) frame = 0;
    if (frame >= repeat) frame = repeat - 1;
    return start + (frame % interval);
}

} // namespace

void ParticleKey::Interpolate(float age,
                              float prevEndTime,
                              ImVector& outColor,
                              int& outHeadCell,
                              int& outTailCell,
                              float& outScale) const {
    float span = endTime - prevEndTime;
    float t = (span > 0.0f) ? (age - prevEndTime) / span : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    outColor.a = byte_lerp(startColor.a, endColor.a, t);
    outColor.r = byte_lerp(startColor.r, endColor.r, t);
    outColor.g = byte_lerp(startColor.g, endColor.g, t);
    outColor.b = byte_lerp(startColor.b, endColor.b, t);

    outScale = startScale + (endScale - startScale) * t;

    outHeadCell = ComputeCell(headCellStart, headCellEnd, headCellRepeat, t);
    outTailCell = ComputeCell(tailCellStart, tailCellEnd, tailCellRepeat, t);
}

} // namespace WhiteoutDex::particle
