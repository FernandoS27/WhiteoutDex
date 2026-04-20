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

// Render pipeline choice. Mirrors Previewd's GxDevRenderMode() + MatSelect
// canonicalisation: in SD mode every mesh routes through the SD program;
// in HD mode, materials whose MDX shader ID resolves to SD/SD_on_HD pick
// the SD_on_HD program while true HD materials pick the HD program.
enum class RenderMode : uint8_t {
    SD = 0,
    HD = 1,
};

struct DisplayFlags {
    bool showGrid       = true;
    bool showParticles  = true;
    bool showRibbons    = true;
    bool showCollisions = false;
    bool showLights     = false;
    RenderMode renderMode = RenderMode::SD;
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
