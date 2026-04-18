#pragma once
// ============================================================================
// D3D11 Backend — Enum Translation Tables
// Maps gfx:: enums to DXGI/D3D11 equivalents. Pure constexpr, no allocations.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>
#include <cassert>

#include "gfx/gfx_types.h"

namespace WhiteoutDex::gfx::d3d11 {

// Safe COM release (local to d3d11 backend)
template<typename T>
inline void SafeRelease(T*& ptr) {
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

// ---- Format ----
inline DXGI_FORMAT ToDXGI(Format f) {
    switch (f) {
        case Format::R8G8B8A8_UNORM:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case Format::R8G8B8A8_UINT:      return DXGI_FORMAT_R8G8B8A8_UINT;
        case Format::B8G8R8A8_UNORM:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case Format::R32_FLOAT:          return DXGI_FORMAT_R32_FLOAT;
        case Format::R32G32_FLOAT:       return DXGI_FORMAT_R32G32_FLOAT;
        case Format::R32G32B32_FLOAT:    return DXGI_FORMAT_R32G32B32_FLOAT;
        case Format::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case Format::R16_UINT:           return DXGI_FORMAT_R16_UINT;
        case Format::R32_UINT:           return DXGI_FORMAT_R32_UINT;
        case Format::D24_UNORM_S8_UINT:  return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_FLOAT:          return DXGI_FORMAT_D32_FLOAT;
        default:                         return DXGI_FORMAT_UNKNOWN;
    }
}

// ---- PrimitiveTopology ----
inline D3D11_PRIMITIVE_TOPOLOGY ToD3D11(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case PrimitiveTopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        default:                               return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

// ---- CullMode ----
inline D3D11_CULL_MODE ToD3D11(CullMode c) {
    switch (c) {
        case CullMode::None:  return D3D11_CULL_NONE;
        case CullMode::Back:  return D3D11_CULL_BACK;
        case CullMode::Front: return D3D11_CULL_FRONT;
        default:              return D3D11_CULL_BACK;
    }
}

// ---- FillMode ----
inline D3D11_FILL_MODE ToD3D11(FillMode f) {
    switch (f) {
        case FillMode::Solid:     return D3D11_FILL_SOLID;
        case FillMode::Wireframe: return D3D11_FILL_WIREFRAME;
        default:                  return D3D11_FILL_SOLID;
    }
}

// ---- CompareOp ----
inline D3D11_COMPARISON_FUNC ToD3D11(CompareOp op) {
    switch (op) {
        case CompareOp::Never:        return D3D11_COMPARISON_NEVER;
        case CompareOp::Less:         return D3D11_COMPARISON_LESS;
        case CompareOp::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
        case CompareOp::Equal:        return D3D11_COMPARISON_EQUAL;
        case CompareOp::Greater:      return D3D11_COMPARISON_GREATER;
        case CompareOp::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
        case CompareOp::Always:       return D3D11_COMPARISON_ALWAYS;
        default:                      return D3D11_COMPARISON_LESS_EQUAL;
    }
}

// ---- BlendFactor ----
inline D3D11_BLEND ToD3D11(BlendFactor bf) {
    switch (bf) {
        case BlendFactor::Zero:        return D3D11_BLEND_ZERO;
        case BlendFactor::One:         return D3D11_BLEND_ONE;
        case BlendFactor::SrcAlpha:    return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::SrcColor:    return D3D11_BLEND_SRC_COLOR;
        case BlendFactor::DstColor:    return D3D11_BLEND_DEST_COLOR;
        case BlendFactor::InvSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
        case BlendFactor::InvDstColor: return D3D11_BLEND_INV_DEST_COLOR;
        default:                       return D3D11_BLEND_ONE;
    }
}

// ---- BlendOp ----
inline D3D11_BLEND_OP ToD3D11(BlendOp op) {
    switch (op) {
        case BlendOp::Add:      return D3D11_BLEND_OP_ADD;
        case BlendOp::Subtract: return D3D11_BLEND_OP_SUBTRACT;
        default:                return D3D11_BLEND_OP_ADD;
    }
}

// ---- Filter ----
inline D3D11_FILTER ToD3D11Filter(Filter minF, Filter magF) {
    if (minF == Filter::Point && magF == Filter::Point)
        return D3D11_FILTER_MIN_MAG_MIP_POINT;
    if (minF == Filter::Linear && magF == Filter::Linear)
        return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (minF == Filter::Point && magF == Filter::Linear)
        return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    // minF == Linear, magF == Point
    return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
}

// ---- AddressMode ----
inline D3D11_TEXTURE_ADDRESS_MODE ToD3D11(AddressMode a) {
    switch (a) {
        case AddressMode::Wrap:   return D3D11_TEXTURE_ADDRESS_WRAP;
        case AddressMode::Clamp:  return D3D11_TEXTURE_ADDRESS_CLAMP;
        case AddressMode::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
        default:                  return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}

// ---- DXGI_FORMAT byte size (for index buffers, etc.) ----
inline UINT FormatByteSize(Format f) {
    switch (f) {
        case Format::R16_UINT:           return 2;
        case Format::R32_UINT:           return 4;
        case Format::R32_FLOAT:          return 4;
        case Format::R32G32_FLOAT:       return 8;
        case Format::R32G32B32_FLOAT:    return 12;
        case Format::R32G32B32A32_FLOAT: return 16;
        case Format::R8G8B8A8_UNORM:     return 4;
        case Format::R8G8B8A8_UINT:      return 4;
        case Format::B8G8R8A8_UNORM:     return 4;
        default:                         return 0;
    }
}

} // namespace WhiteoutDex::gfx::d3d11
