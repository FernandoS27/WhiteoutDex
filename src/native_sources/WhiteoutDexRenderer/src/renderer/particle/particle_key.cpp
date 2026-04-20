#include "particle_key.h"

#include <algorithm>
#include <cmath>

namespace WhiteoutDex::particle {

namespace {

inline uint8_t byte_lerp(uint8_t a, uint8_t b, float t) {
    // Previewd uses ftol_0_256_ (truncate to int with 0..256 clamp). Byte
    // colours with typical start/end pairs produce integer values, so
    // round-to-nearest vs truncate differ by at most 1/255 per channel.
    float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

// Port of CParticleKey::Interpolate's cell-index math (@0x14070b6d0) plus
// SetHeadCells/SetTailCells (@0x14070be40) layout. Both head and tail use
// the same formula:
//   effT    = (repeat == 1) ? t : fmod(t * repeat, 1)
//   cell    = (int)(initial + delta * effT)            // truncate toward 0
//   where   initial = start,     delta = end - start + 1   when end >= start
//           initial = start + 1, delta = end - start - 1   when end <  start
// The caller feeds the nudged `t` (0.995 at the key-end) so `delta * effT`
// stays strictly below `delta` and the truncation lands at `end` exactly —
// never spills over to `end + 1`.
inline int ComputeCell(int start, int end, int repeat, float t) {
    // Previewd clamps m_repeat to >= 1 in SetSegment @0x14070bb90, so a
    // file-authored repeat of 0 behaves identically to 1 (single smooth
    // sweep from start to end across the key's segment).
    float r = (repeat < 1) ? 1.0f : static_cast<float>(repeat);

    int initial = (end >= start) ? start           : (start + 1);
    int delta   = (end >= start) ? (end - start + 1) : (end - start - 1);

    float effT = (r == 1.0f) ? t : std::fmod(t * r, 1.0f);
    float val  = static_cast<float>(initial) + static_cast<float>(delta) * effT;
    if (val < 0.0f) val = 0.0f;
    return static_cast<int>(val);
}

} // namespace

void ParticleKey::Interpolate(float age,
                              float prevEndTime,
                              ImVector& outColor,
                              int& outHeadCell,
                              int& outTailCell,
                              float& outScale) const {
    float span = endTime - prevEndTime;
    float rawT = (span > 0.0f) ? (age - prevEndTime) / span : 0.0f;
    if (rawT < 0.0f) rawT = 0.0f;
    if (rawT > 1.0f) rawT = 1.0f;

    // Previewd's CParticleKey::Interpolate @0x14070b6d0 remaps t from the
    // exact [0,1] endpoints to [0.005, 0.995] (constant factors 0.99 + 0.005
    // lifted verbatim from the disasm). The 1% contraction is imperceptible
    // on colour/scale but keeps `delta * t` strictly inside `delta` for cell
    // indices — without it, repeat=1 with a 1-cell sheet overshoots to the
    // next cell at t=1 for a single frame.
    const float t = rawT * 0.99f + 0.005f;

    outColor.a = byte_lerp(startColor.a, endColor.a, t);
    outColor.r = byte_lerp(startColor.r, endColor.r, t);
    outColor.g = byte_lerp(startColor.g, endColor.g, t);
    outColor.b = byte_lerp(startColor.b, endColor.b, t);

    outScale = startScale + (endScale - startScale) * t;

    outHeadCell = ComputeCell(headCellStart, headCellEnd, headCellRepeat, t);
    outTailCell = ComputeCell(tailCellStart, tailCellEnd, tailCellRepeat, t);
}

} // namespace WhiteoutDex::particle
