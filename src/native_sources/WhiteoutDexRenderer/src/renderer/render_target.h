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
    // LDR final target with the sRGB-encoding RTV view. HD's tonemap
    // pass writes linear values here; hardware encodes linear → sRGB
    // on store. Aliases the back-buffer for swap-chain targets.
    gfx::TextureHandle    color  = gfx::TextureHandle::Invalid;
    // Linear (non-sRGB) RTV view of the SAME physical resource as
    // `color`. SD rendering writes display-ready sRGB-byte outputs
    // here so they're stored verbatim — no hardware re-encode, no
    // double gamma. Both RTVs sit on the same back-buffer; only the
    // view format differs. See gfx::IGFXDevice::GetSwapChainBackBuffer
    // / GetSwapChainBackBufferLinear.
    gfx::TextureHandle    colorLinear = gfx::TextureHandle::Invalid;
    // Linear-HDR scene target. All HD-mode 3D draws render here; the
    // tonemap pass samples it at t0 and writes ACES-encoded results
    // to `color`. RGBA16F gives the HD lighting pass real headroom.
    gfx::TextureHandle    hdrColor = gfx::TextureHandle::Invalid;
    gfx::TextureHandle    depth  = gfx::TextureHandle::Invalid;
    int                   width  = 0;
    int                   height = 0;
};

} // namespace WhiteoutDex
