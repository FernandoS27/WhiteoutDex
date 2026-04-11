#pragma once
// ============================================================================
// TeamGlow texture utility — decodes embedded TeamGlow.tga and tints it
// ============================================================================

#include <cstdint>
#include <vector>

namespace WhiteoutDex {

// Decode the embedded TeamGlow TGA and tint with team color.
// Returns RGBA8 pixel data, sets outW/outH to the image dimensions.
// If decoding fails, returns a 4x4 solid color fallback.
std::vector<uint8_t> DecodeTeamGlow(uint8_t tcR, uint8_t tcG, uint8_t tcB,
                                     int& outW, int& outH);

} // namespace WhiteoutDex
