// ============================================================================
// RenderTarget — Per-target GPU resources (RTV, DSV, swap chain or texture).
//
// Internal type. Each target represents either a swap-chain-backed window
// surface or an off-screen render-to-texture surface.
// ============================================================================
#pragma once

#include "dx_types.h"
#include <cstdint>

namespace WhiteoutDex {

using RenderTargetId = uint32_t;

struct DisplayFlags {
    bool showGrid       = true;
    bool showParticles  = true;
    bool showRibbons    = true;
    bool showCollisions = false;
};

struct RenderTarget {
    RenderTargetId          id       = 0;
    IDXGISwapChain*         swapChain = nullptr;   // null for off-screen targets
    ID3D11RenderTargetView* rtv       = nullptr;
    ID3D11DepthStencilView* dsv       = nullptr;
    ID3D11Texture2D*        depthBuf  = nullptr;
    ID3D11Texture2D*        colorTex  = nullptr;   // off-screen targets only
    int                     width     = 0;
    int                     height    = 0;

    /// Resize swap-chain target (recreates back-buffer RTV + depth DSV).
    bool Resize(ID3D11Device* device, int w, int h);

    /// Release all GPU resources held by this target.
    void Release();
};

} // namespace WhiteoutDex
