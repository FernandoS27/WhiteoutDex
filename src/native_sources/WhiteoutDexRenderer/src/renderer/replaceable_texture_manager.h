#pragma once
// ============================================================================
// ReplaceableTextureManager — owns all runtime team-colour / team-glow state.
//
// Replaces the constellation of:
//   RenderService::teamColor_, teamColorDirty_, teamColorTex_, teamColorTexColor_
//   RenderService::SetTeamColor / UpdateTeamColorTextures / UpdateTeamColorSwatch
//   RenderService::RegisterReplaceableEmitterTex
//   Actor::replaceableTexMap
//
// One owner means one place to fix bugs like "TeamGlow particles didn't
// retint when the swatch changed because the emitter registration ran on a
// different code path than the model registration" (commit db3cc48).
//
// Adapter-side helpers (`GenerateTeamColorTexture`, `LoadTextureWithTeamColor`,
// `GenerateTeamGlowTexture`, `EnsureHdTeamColorSentinel`) are intentionally
// out of scope for this phase — they produce the StagedTexture's initial
// pixels at adapter time, but the manager re-bakes them at register time
// using the current swatch, so adapters can leave that fallback logic in
// place. A follow-up cleanup can drain those.
// ============================================================================

#include "../gfx/gfx.h"
#include "model_instance.h"

#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex {

class TextureAssetManager;

// What the engine substitutes at runtime for textures flagged with one of
// Blizzard's "replaceable" ids. Numeric values mirror MDX's wire format
// (`replaceableId` byte) — 0 means "not replaceable", 1/2 are the only
// values the SD code path bakes per-model pixels for.
enum class ReplaceableKind : uint8_t {
    None      = 0,
    TeamColor = 1,  // SD path: 4x4 RGBA flat-fill matching the live swatch.
    TeamGlow  = 2,  // SD path: embedded TGA decoded with the swatch as tint.
};

class ReplaceableTextureManager {
public:
    ReplaceableTextureManager(gfx::IGFXDevice& gfx, TextureAssetManager& textures);
    ~ReplaceableTextureManager();

    ReplaceableTextureManager(const ReplaceableTextureManager&)            = delete;
    ReplaceableTextureManager& operator=(const ReplaceableTextureManager&) = delete;

    // ── Team-colour state (API-thread). ─────────────────────────────────
    // Updates the global swatch and re-bakes per-model SD pixels for every
    // registered slot. Render-thread `Tick` will then re-upload them via
    // the existing `mi.render.stagedDirty` path. Marks dirty for the platform UI.
    void SetTeamColor(uint8_t r, uint8_t g, uint8_t b);

    // BGR-packed (matches the legacy Windows RGB() macro layout used at
    // the platform-window-picker boundary). 0x00BBGGRR.
    uint32_t GetTeamColorRaw() const noexcept { return teamColor_; }

    // Set/exchange the dirty flag — UI polls this once per repaint.
    bool ConsumeDirty() { return dirty_.exchange(false); }

    // ── Per-model slot registry (API-thread; caller owns dataMutex). ────
    // Records that `mi.render.textures[textureId]` is a replaceable slot. Bakes
    // initial pixels into `mi.render.stagedTextures[textureId]` from the current
    // swatch and marks `mi.render.stagedDirty`. Replaces RegisterReplaceableEmitterTex.
    void RegisterModelSlot(Actor& mi, int textureId, ReplaceableKind kind);

    // Forget every slot belonging to `mi`. Called when a Actor is
    // about to be destroyed.
    void UnregisterModel(Actor& mi);

    // ── HD live swatch (render-thread). ─────────────────────────────────
    // Returns the 1x1 swatch texture bound at t4 for HD draws. Lazy:
    // re-uploads only when the colour changed since the last call.
    gfx::TextureHandle GetHdSwatchTexture();

    // ── SD live swatches (render-thread). ───────────────────────────────
    // Per-kind global swatch textures used by PE2 (and any other SD path
    // that wants to substitute a replaceable at runtime without
    // overwriting the actor's loaded BLP). Same lazy rebuild contract as
    // GetHdSwatchTexture: regenerates only when the team colour
    // changed since the last call. TeamColor is a 4×4 solid; TeamGlow
    // is the embedded TGA tinted with the current swatch.
    gfx::TextureHandle GetSdTeamColorTexture();
    gfx::TextureHandle GetSdTeamGlowTexture();

    // Called from RenderService::ShutdownDevice. Idempotent. Frees the HD
    // swatch GPU handle; the per-model slot registry empties as
    // Actors are destroyed.
    void Shutdown();

    // ── Debug snapshot ──────────────────────────────────────────────────
    // Counts for a future F-key overlay. `models` counts models with at
    // least one registered slot; `slots` is the total slot count across
    // all models.
    struct DebugCounts { size_t models = 0; size_t slots = 0; };
    DebugCounts DebugSnapshot() const noexcept {
        DebugCounts c;
        c.models = slots_.size();
        for (auto& [mi, v] : slots_) c.slots += v.size();
        return c;
    }

private:
    gfx::IGFXDevice&     gfx_;
    TextureAssetManager& textures_;

    // BGR-packed swatch (matches the wire format the platform UI hands us).
    uint32_t          teamColor_ = 0x000000FFu;   // default red
    std::atomic<bool> dirty_{false};

    // 1x1 RGBA8 swatch bound at HD t4. Recreated whenever the colour
    // changes; `lastSwatchRgba_` lets us short-circuit on no-op refreshes.
    gfx::TextureHandle hdSwatchTex_    = gfx::TextureHandle::Invalid;
    uint32_t           lastSwatchRgba_ = 0xFFFFFFFFu;

    // SD swatches — one TeamColor (flat 4×4) and one TeamGlow (full
    // tinted TGA), shared across every PE2 emitter that asks for them.
    // Keyed by the same RGBA value as `lastSwatchRgba_` so a colour
    // change rebuilds both. Allocated lazily on first request.
    gfx::TextureHandle sdTeamColorTex_     = gfx::TextureHandle::Invalid;
    gfx::TextureHandle sdTeamGlowTex_      = gfx::TextureHandle::Invalid;
    uint32_t           lastSdSwatchRgba_   = 0xFFFFFFFFu;

    // Per-model slots — map Actor* → list of (textureId, kind).
    // Erased on UnregisterModel; never persists across model unloads.
    struct Slot { int textureId; ReplaceableKind kind; };
    std::unordered_map<Actor*, std::vector<Slot>> slots_;

    // Bake current-swatch pixels into mi.render.stagedTextures[textureId] for one slot.
    // Marks mi.render.stagedDirty so the next render-thread upload picks it up.
    void BakeSlot(Actor& mi, int textureId, ReplaceableKind kind);
};

} // namespace WhiteoutDex
