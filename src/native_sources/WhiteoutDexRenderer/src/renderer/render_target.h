// ============================================================================
// RenderTarget — Per-target GPU resources (swap chain or off-screen pair).
//
// Internal type. Each target represents either a swap-chain-backed window
// surface or an off-screen render-to-texture surface.
// All resources are owned by the GFX device's slot-maps; RenderTarget only
// holds opaque handles.
// ============================================================================
#pragma once

#include "gfx/gfx.h"
#include <cstdint>

namespace WhiteoutDex {

using RenderTargetId = uint32_t;

struct DisplayFlags {
    bool showGrid       = true;
    bool showParticles  = true;
    bool showRibbons    = true;
    bool showCollisions = false;
    bool showLights     = false;
};

struct RenderTarget {
    RenderTargetId        id     = 0;
    gfx::SwapChainHandle  swap   = gfx::SwapChainHandle::Invalid;   // Invalid = offscreen
    gfx::TextureHandle    color  = gfx::TextureHandle::Invalid;
    gfx::TextureHandle    depth  = gfx::TextureHandle::Invalid;
    int                   width  = 0;
    int                   height = 0;
};

} // namespace WhiteoutDex
