#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Common Types (public, platform-free)
// ============================================================================

#include <whiteout/vector_types.h>

#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>

using whiteout::Vector2f;
using whiteout::Vector3f;
using whiteout::Vector4f;
using whiteout::Matrix44f;
using whiteout::Quaternion;

namespace WhiteoutDex {

// Platform-neutral rectangle (matches RECT layout for easy conversion)
struct Rect {
    int left, top, right, bottom;
};

// Vertex format for Phase 1 (position + normal + color + uv)
struct Vertex {
    Vector3f position;
    Vector3f normal;
    Vector4f color;
    Vector2f uv;
};

// Constant buffer for vertex shader (per-frame)
struct alignas(16) CBPerFrame {
    Matrix44f world;
    Matrix44f view;
    Matrix44f projection;
    Vector4f lightDir;
    Vector4f lightColor;
    Vector4f ambientColor;   // .a = alpha test threshold
    Vector4f extraParams;    // .x = geoset alpha multiplier (1.0 = full, 0.0 = invisible)
    Vector4f texAnimParams;  // .x=uOffset, .y=vOffset, .z=uTiling, .w=vTiling (legacy path only)
    Vector4f materialFlags;  // .x=unshaded, .y=constantColor, .z=texRotation, .w=reserved (legacy path only)
};

} // namespace WhiteoutDex

// DLL export macro
#ifdef WHITEOUTDEX_RENDERER_EXPORTS
    #define WHITEOUTDEX_API extern "C" __declspec(dllexport)
#else
    #define WHITEOUTDEX_API extern "C" __declspec(dllimport)
#endif
