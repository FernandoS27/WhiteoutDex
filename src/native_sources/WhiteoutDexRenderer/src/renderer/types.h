#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Common Types
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <whiteout/vector_types.h>

#include <cmath>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using whiteout::Vector2f;
using whiteout::Vector3f;
using whiteout::Vector4f;
using whiteout::Matrix44f;
using whiteout::Quaternion;

namespace WhiteoutDex {

// Safe COM release
template<typename T>
inline void SafeRelease(T*& ptr) {
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

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
