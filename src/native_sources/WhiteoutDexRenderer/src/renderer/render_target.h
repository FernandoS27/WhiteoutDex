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

// Driver for how the renderer's baseline (camera-attached headlight) mixes
// with the model's authored MDX lights. Read by BuildLightPalette.
//   InGame  — baseline always present, authored lights stack on top of it.
//   Glue    — only authored lights; no baseline. Models with no lights
//             render flat (matches Blizzard's glue-screen viewer).
//   Dynamic — baseline only when no authored light is enabled; otherwise
//             authored lights take over (the previous behaviour).
enum class LightingMode : uint8_t {
    InGame  = 0,
    Glue    = 1,
    Dynamic = 2,
};

struct DisplayFlags {
    bool showGrid       = true;
    bool showParticles  = true;
    bool showRibbons    = true;
    bool showCollisions = false;
    bool showLights     = false;
    bool showEvents     = true;
    RenderMode renderMode = RenderMode::SD;
};

struct RenderTarget {
    RenderTargetId        id     = 0;
    gfx::SwapChainHandle  swap   = gfx::SwapChainHandle::Invalid;   // Invalid = offscreen
    // LDR final target. For swap-chain targets this aliases the back-buffer
    // (recreated by ResizeRenderTarget after each ResizeSwapChain); for
    // off-screen targets it's owned R8G8B8A8_UNORM. The tonemap pass writes
    // here at the end of every RenderFrame.
    gfx::TextureHandle    color  = gfx::TextureHandle::Invalid;
    // Linear-HDR scene target. All 3D mesh / particle / ribbon / debug
    // draws render into this; the tonemap pass then samples it at t0
    // and writes the LDR result to `color`. RGBA16F gives the HD lighting
    // pass real headroom (Blizzard's CGxDevRenderer::CreateMainTarget
    // does the same — without this the HD pixel shader's filmic exposure
    // saturates against the [0,1] cap of the 8-bit back-buffer).
    gfx::TextureHandle    hdrColor = gfx::TextureHandle::Invalid;
    gfx::TextureHandle    depth  = gfx::TextureHandle::Invalid;
    int                   width  = 0;
    int                   height = 0;
};

} // namespace WhiteoutDex
