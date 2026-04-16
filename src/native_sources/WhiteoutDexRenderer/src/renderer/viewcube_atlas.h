#pragma once
#include <cstdint>
#include <vector>

namespace WhiteoutDex {

// Generates the ViewCube face-label atlas: 6 cells of 64x64 RGBA laid out
// horizontally (total 384x64). Pure C++, no OS dependencies.
// Cell order: FRONT, BACK, LEFT, RIGHT, TOP, BOT.
std::vector<uint8_t> GenerateViewCubeAtlas(int& outW, int& outH);

} // namespace WhiteoutDex
