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

// Vertex format for Phase 1 (position + normal + color + uv). Retained
// for the legacy Slang path and for PE2 particle geometry (matches the
// BLS SD VS PNCT0 layout exactly -- ATTR0 pos, ATTR1 normal, ATTR2 color,
// ATTR3 tc0).
struct Vertex {
    Vector3f position;  // ATTR0
    Vector3f normal;    // ATTR1
    Vector4f color;     // ATTR2 (float4, matches SD PNCT0)
    Vector2f uv;        // ATTR3
};
static_assert(sizeof(Vertex) == 48);

// Mesh vertex stream for BLS SD meshes (PNT0 / PNT0T1). No per-vertex
// color on the geometry stream -- color is fed via CB.
struct MeshVertexSD {
    Vector3f position;  // ATTR0
    Vector3f normal;    // ATTR1
    Vector2f uv0;       // ATTR3
    Vector2f uv1;       // ATTR4 (zero for single-layer geosets)
};
static_assert(sizeof(MeshVertexSD) == 40);

// Skinning side-stream for BLS SD skinned geosets (GxVBF_B, 8 B/vertex,
// bound at slot 1). ATTR5 is R8G8B8A8_UNORM (float4 weights) and ATTR6
// is R8G8B8A8_UINT (uint4 indices) -- see docs/BLS_ShaderABI.md.
struct BoneVertex {
    uint8_t weights[4]; // ATTR5 -- normalized float4
    uint8_t indices[4]; // ATTR6 -- uint4
};
static_assert(sizeof(BoneVertex) == 8);

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
