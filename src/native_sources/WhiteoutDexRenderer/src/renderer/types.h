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
#include <DirectXMath.h>

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

using namespace DirectX;

namespace WhiteoutDex {

// Safe COM release
template<typename T>
inline void SafeRelease(T*& ptr) {
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

// Vertex format for Phase 1 (position + normal + color + uv)
struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT4 color;
    XMFLOAT2 uv;
};

// Constant buffer for vertex shader (per-frame)
struct alignas(16) CBPerFrame {
    XMMATRIX world;
    XMMATRIX view;
    XMMATRIX projection;
    XMFLOAT4 lightDir;
    XMFLOAT4 lightColor;
    XMFLOAT4 ambientColor;   // .a = alpha test threshold
    XMFLOAT4 extraParams;    // .x = geoset alpha multiplier (1.0 = full, 0.0 = invisible)
    XMFLOAT4 texAnimParams;  // .x=uOffset, .y=vOffset, .z=uTiling, .w=vTiling
    XMFLOAT4 materialFlags;  // .x=unshaded, .y=constantColor, .z=reserved, .w=reserved
};

} // namespace WhiteoutDex

// DLL export macro
#ifdef WHITEOUTDEX_RENDERER_EXPORTS
    #define WHITEOUTDEX_API extern "C" __declspec(dllexport)
#else
    #define WHITEOUTDEX_API extern "C" __declspec(dllimport)
#endif
