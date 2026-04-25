#pragma once
// ============================================================================
// TextureAssetManager — central owner for every gfx::TextureHandle in the
// renderer (except render-target / swap-chain handles, which live in
// RenderTarget). See docs/TEXTURE_SAMPLER_REFACTOR_DESIGN.md.
//
// Phase 2 — defaults only. The 5 fallback textures (white, black, flat
// normal, neutral ORM, magenta missing-marker) move into this class so
// adding another default is a one-line struct edit instead of a three-site
// touch in InitDevice + ShutdownDevice + every bind site that selects one.
//
// Per-model uploads, IBL probes, ViewCube atlas, and live team-colour
// swatch will land in this class in subsequent phases (3, 5, 4 respectively).
// ============================================================================

#include "../gfx/gfx.h"
#include "sampler_asset_manager.h"   // kSamplerWrapBitsMask

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex {

class TextureAssetManager {
public:
    explicit TextureAssetManager(gfx::IGFXDevice& gfx);
    ~TextureAssetManager();

    TextureAssetManager(const TextureAssetManager&)            = delete;
    TextureAssetManager& operator=(const TextureAssetManager&) = delete;

    // ── Shared (cross-model) texture cache ──────────────────────────────
    // Refcounted store of file-backed GPU textures, keyed by the opaque
    // string adapters supply via TextureData::sharedKey. Two models that
    // reference the same logical texture share one GPU upload; the
    // refcount drops to zero when the last referencing ModelScope is
    // destroyed (or releases its borrow), and the GPU handle is freed
    // then. AcquireShared / ReleaseShared are render-thread-only — they
    // touch the device through gfx_.
    //
    // Adapters that produce procedural textures (team-colour swatches,
    // magenta missing-marker, HD live sentinels, …) leave sharedKey
    // empty so their slots stay per-model in ModelScope.
    gfx::TextureHandle AcquireShared(std::string_view        key,
                                     const gfx::TextureDesc& desc,
                                     const void*             pixels);
    void               ReleaseShared(std::string_view key);

    // Thread-safe peek — adapters call this from any thread (typically the
    // model-load thread) before doing the expensive BLP/CASC decode. Result
    // is advisory: between this call and the eventual UploadStagedTextures
    // an unloaded model may evict the entry. Renderer's borrow-only path
    // handles that race by binding the magenta default.
    bool IsCachedShared(std::string_view key) const;

    // Borrow an entry that the caller already saw via IsCachedShared, with
    // no pixels supplied. Returns Invalid if the entry has since been
    // evicted (eviction race). On success the refcount is bumped and the
    // caller is expected to ReleaseShared the same key when done.
    gfx::TextureHandle TryAcquireShared(std::string_view key);

    // Diagnostic counters; reset by ResetSharedStats. UI can poll these
    // for the future debug overlay (uploads = unique GPU creations,
    // hits = times an Acquire returned a cached handle).
    struct SharedStats {
        size_t uniqueEntries = 0;   // current size of shared_
        size_t totalAcquires = 0;   // lifetime; cumulative
        size_t cacheHits     = 0;   // lifetime
    };
    SharedStats GetSharedStats() const;
    void        ResetSharedStats();

    // ── ModelScope ──────────────────────────────────────────────────────
    // Owns every GPU texture handle for one Actor's per-material
    // texture set. Replaces the previous `Actor::gpuTextures` map
    // and the inline `Release(...)` loop in `ReleaseGPU`. Destroying the
    // scope (RAII via std::unique_ptr) releases every owned handle in one
    // place — the per-model lifetime story is now expressed in the type.
    class ModelScope {
    public:
        // Upload (or replace) a per-model owned texture for `textureId`.
        // Releases the previous handle for this id if one existed.
        // Used for procedural/replaceable textures with no dedup key.
        gfx::TextureHandle Upload(int                     textureId,
                                  const gfx::TextureDesc& desc,
                                  const void*             pixels,
                                  uint32_t                wrapFlags);

        // Bind `textureId` to a shared cross-model texture identified by
        // `sharedKey`. Increments the manager's refcount on cache hit;
        // creates+caches on miss. The scope tracks the borrow so Clear()
        // releases the ref instead of destroying the handle directly.
        gfx::TextureHandle UploadShared(int                     textureId,
                                        std::string_view        sharedKey,
                                        const gfx::TextureDesc& desc,
                                        const void*             pixels,
                                        uint32_t                wrapFlags);

        // Borrow-only path — for textures the adapter saw cached at decode
        // time and skipped the BLP/CASC work for. Returns Invalid if the
        // entry was evicted between the adapter's check and now (eviction
        // race); caller must bind a fallback (the renderer's default
        // magenta) at draw time when this happens.
        gfx::TextureHandle BindShared(int              textureId,
                                      std::string_view sharedKey,
                                      uint32_t         wrapFlags);

        // Lookup; returns Invalid if `textureId` was never uploaded.
        gfx::TextureHandle Get(int textureId) const noexcept;

        // wrapFlags carried through from the upload call; returns
        // kSamplerWrapBitsMask (Wrap on both axes) for an unknown id so
        // callers can pipe the result straight into
        // SamplerAssetManager::WrapVariant without a null check.
        uint32_t WrapFlags(int textureId) const noexcept;

        // Number of uploaded textures. Diagnostic.
        size_t Size() const noexcept { return entries_.size(); }

        // Release every owned handle. Idempotent; called by the destructor.
        void Clear();

        ~ModelScope();
        ModelScope(const ModelScope&)            = delete;
        ModelScope& operator=(const ModelScope&) = delete;

    private:
        friend class TextureAssetManager;
        ModelScope(gfx::IGFXDevice& gfx, TextureAssetManager& mgr)
            : gfx_(gfx), mgr_(mgr) {}

        // Drop ownership of `entries_[textureId]` if it exists, releasing
        // the underlying handle through the right path (owned → Destroy,
        // shared → ReleaseShared to the manager).
        void DropEntry(int textureId);

        gfx::IGFXDevice&     gfx_;
        TextureAssetManager& mgr_;
        struct Entry {
            gfx::TextureHandle tex       = gfx::TextureHandle::Invalid;
            uint32_t           wrapFlags = kSamplerWrapBitsMask;
            // Empty: this scope owns the GPU handle directly (Destroy on Clear).
            // Non-empty: borrowed from the shared cache; ReleaseShared on Clear.
            std::string        sharedKey;
        };
        std::unordered_map<int, Entry> entries_;
    };

    // Allocate a fresh ModelScope. RenderService stores one per
    // Actor; the unique_ptr lifetime drives texture cleanup.
    std::unique_ptr<ModelScope> CreateModelScope();

    // ── Externally-allocated owned textures ─────────────────────────────
    // For singletons that the rest of the renderer creates itself
    // (IBL probes, split-sum LUT, ViewCube atlas, …) but wants this
    // manager to track lifetime for. Any prior handle registered under
    // `name` is destroyed first; passing Invalid is equivalent to
    // ReleaseOwned. Callers look up handles via GetOwned at bind time.
    void RegisterOwned(std::string name, gfx::TextureHandle handle);
    void ReleaseOwned(std::string_view name);
    gfx::TextureHandle GetOwned(std::string_view name) const noexcept;

    // ── Debug snapshot ──────────────────────────────────────────────────
    // Snapshot of every owned texture — enables a future F-key overlay
    // to list live handles by debug name. Cheap (linear walk of small
    // maps); safe to call once per repaint. Does NOT include defaults
    // (always the same 5 handles) or per-model ModelScope entries
    // (those live in the scope's own `Size()` accessor).
    struct DebugEntry {
        std::string        name;
        gfx::TextureHandle handle;
    };
    std::vector<DebugEntry> DebugSnapshotOwned() const;

    // ── Default textures ─────────────────────────────────────────────────
    // Created once in the constructor, destroyed in the destructor. Bound
    // by callers as the slot-specific fallback when a layer doesn't author
    // the corresponding subtexture.
    //
    //   White       — 1x1 0xFFFFFFFF.            Albedo fallback (t0).
    //   Black       — 1x1 0x00000000.            Emissive / team-colour
    //                                            fallback (t3 / t4): RGBA
    //                                            (0,0,0,0) so multiLayerBlend
    //                                            sees zero contribution.
    //   FlatNormal  — 1x1 (128,128,255,255).     decodeNormalMap reads
    //                                            r=0.5, a=1.0 → (0,0,1).
    //   NeutralOrm  — 1x1 (255,255,0,0).         occlusion=1, roughness=1
    //                                            (must — 0 turns every
    //                                            unauthored-ORM HD layer
    //                                            into a perfect mirror and
    //                                            the IBL horizon shows up
    //                                            as a sharp seam through
    //                                            the view centre), metal=0,
    //                                            teamBlend=0.
    //   Missing     — 1x1 magenta.               Surfaces a decode failure
    //                                            visually so silent
    //                                            fall-throughs aren't masked
    //                                            by a plausible-looking
    //                                            white default.
    struct Defaults {
        gfx::TextureHandle White       = gfx::TextureHandle::Invalid;
        gfx::TextureHandle Black       = gfx::TextureHandle::Invalid;
        gfx::TextureHandle FlatNormal  = gfx::TextureHandle::Invalid;
        gfx::TextureHandle NeutralOrm  = gfx::TextureHandle::Invalid;
        gfx::TextureHandle Missing     = gfx::TextureHandle::Invalid;
    };
    const Defaults& GetDefaults() const noexcept { return defaults_; }

private:
    gfx::IGFXDevice& gfx_;
    Defaults         defaults_;
    // Transparent hasher so GetOwned(std::string_view) doesn't allocate
    // a std::string for the lookup. Both the hasher and equality predicate
    // must be transparent (C++20 heterogeneous lookup rules).
    struct TransparentStringHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string& s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
        size_t operator()(const char* s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    std::unordered_map<std::string, gfx::TextureHandle,
                       TransparentStringHash, std::equal_to<>> owned_;

    // Refcounted shared-texture cache. handle = the GPU resource;
    // refCount = number of ModelScope::Entry borrows currently outstanding.
    struct SharedEntry {
        gfx::TextureHandle handle   = gfx::TextureHandle::Invalid;
        size_t             refCount = 0;
    };
    std::unordered_map<std::string, SharedEntry,
                       TransparentStringHash, std::equal_to<>> shared_;
    // Guards every read/write of shared_ + the stats counters. Adapter
    // threads only call IsCachedShared (read); the render thread is the
    // only writer (Acquire/Release/TryAcquire). std::mutex over a 1-2µs
    // critical section is plenty — shared_mutex would be overkill.
    mutable std::mutex sharedMutex_;
    // Lifetime totals — never reset on cache evictions.
    size_t sharedTotalAcquires_ = 0;
    size_t sharedCacheHits_     = 0;
};

} // namespace WhiteoutDex
