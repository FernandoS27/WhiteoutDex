#pragma once
// ============================================================================
// SamplerAssetManager — central owner for every gfx::SamplerHandle in the
// renderer. Replaces RenderService::samplerLinear_ + samplerWrap_[4] and
// the kWrapFlagsMask constant scattered across bind sites.
//
// Two reasons this exists:
//   1) DRY — sampler creation lived in InitDevice, sampler destruction in
//      ShutdownDevice, sampler indexing in five draw paths. Adding an
//      anisotropic / mirror / comparison sampler meant touching all three.
//   2) The wrap-flag bit encoding (bit 0 = U, bit 1 = V; both 0 = Clamp,
//      1 = Wrap) was implicit. Now the manager owns the mapping between
//      the wire-format `wrapFlags` carried on StagedTexture / GPUTexture
//      and the actual gfx::SamplerHandle bound at draw time.
// ============================================================================

#include "../gfx/gfx.h"
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace WhiteoutDex {

// MDX `Layer::flags` and `StagedTexture::wrapFlags` use a 2-bit encoding:
//   bit 0 = U axis (0 = Clamp, 1 = Wrap)
//   bit 1 = V axis (0 = Clamp, 1 = Wrap)
// All higher bits are ignored. Exposed here so the wire format that adapters
// produce is documented in one place.
inline constexpr uint32_t kSamplerWrapBitsMask = 0x3;

// Typed wrap modes — the four 2-bit combinations of the wire format above.
// The numeric values are the wire format itself, so `static_cast<WrapMode>(w)`
// works for any masked input. Present so call sites can write
// `samplers.WrapVariant(WrapMode::WrapWrap)` instead of magic numbers.
enum class WrapMode : uint32_t {
    ClampClamp = 0,
    WrapClamp  = 1,
    ClampWrap  = 2,
    WrapWrap   = 3,
};

class SamplerAssetManager {
public:
    explicit SamplerAssetManager(gfx::IGFXDevice& gfx);
    ~SamplerAssetManager();

    SamplerAssetManager(const SamplerAssetManager&)            = delete;
    SamplerAssetManager& operator=(const SamplerAssetManager&) = delete;

    // ── Generic dedup'd lookup. ─────────────────────────────────────────
    // Get-or-create a sampler matching `desc`. Hashed by descriptor so
    // repeated calls with identical descriptors return the same handle.
    // Cheap; safe to call per draw if a caller wants a non-standard mode.
    gfx::SamplerHandle Get(const gfx::SamplerDesc& desc);

    // ── Convenience accessors for the shapes the renderer uses today. ──
    // Linear filter on min/mag; W axis pinned to Clamp (no 3D textures
    // bound here). Out-of-range bits in `wrapFlags` are masked off — no UB.
    gfx::SamplerHandle WrapVariant(uint32_t wrapFlags);
    gfx::SamplerHandle WrapVariant(WrapMode mode) {
        return WrapVariant(static_cast<uint32_t>(mode));
    }

    // Linear filter, wrap on every axis. Used for HD t1..t3 (normal/ORM/
    // emissive) where the per-layer wrap bits don't apply because those
    // slots aren't authored from the per-layer MDX flags.
    gfx::SamplerHandle LinearWrap();

    // ── Debug snapshot ──────────────────────────────────────────────────
    // Number of distinct sampler descriptors we've handed out. Feeds a
    // potential F-key overlay that lists live GPU objects per frame.
    size_t DebugSamplerCount() const noexcept { return cache_.size(); }

private:
    gfx::IGFXDevice& gfx_;

    struct DescKey {
        gfx::Filter      minF;
        gfx::Filter      magF;
        gfx::AddressMode aU;
        gfx::AddressMode aV;
        gfx::AddressMode aW;
        bool operator==(const DescKey&) const noexcept = default;
    };
    struct DescKeyHash {
        size_t operator()(const DescKey& k) const noexcept {
            // Five small enums: pack into a single 64-bit scrambled value.
            uint64_t h = 0;
            h |= (uint64_t)k.minF << 0;
            h |= (uint64_t)k.magF << 4;
            h |= (uint64_t)k.aU   << 8;
            h |= (uint64_t)k.aV   << 16;
            h |= (uint64_t)k.aW   << 24;
            return std::hash<uint64_t>{}(h);
        }
    };
    std::unordered_map<DescKey, gfx::SamplerHandle, DescKeyHash> cache_;
};

} // namespace WhiteoutDex
