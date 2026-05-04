// ============================================================================
// TextureAssetManager — implementation (Phase 2: defaults only)
// ============================================================================

#include "texture_asset_manager.h"

namespace WhiteoutDex {

namespace {
// Helper to keep the constructor body tidy. Every default is a 1x1
// R8G8B8A8_UNORM ShaderResource — only the pixel value differs.
gfx::TextureHandle Make1x1(gfx::IGFXDevice& gfx, uint32_t rgba) {
    return gfx.CreateTexture({
        .width  = 1,
        .height = 1,
        .format = gfx::Format::R8G8B8A8_UNORM,
        .usage  = gfx::TextureUsage::ShaderResource,
    }, &rgba);
}
} // namespace

TextureAssetManager::TextureAssetManager(gfx::IGFXDevice& gfx) : gfx_(gfx) {
    // Pixel layouts are little-endian R8G8B8A8 — least-significant byte is R.
    defaults_.White       = Make1x1(gfx_, 0xFFFFFFFFu);
    defaults_.Black       = Make1x1(gfx_, 0x00000000u);
    // (0.5, 0.5, 0, 1) — decodeNormalMap reads R, G, A only; B is unused.
    // R=A=0.5 → nx = 2·R·A − 1 = 0; G=0.5 → ny = −(2·G − 1) = 0;
    // nz = √(1 − 0 − 0) = 1 → flat (0, 0, 1) tangent-space normal.
    defaults_.FlatNormal  = Make1x1(gfx_, 0xFF008080u);   // (128,128,0,255)
    // (1, 1, 0, 0) — full AO, full roughness, no metal, no team-colour
    // blend. Critical for the team-colour blend: t_orm.w == 0 turns
    // off team tint when a layer ships no ORM map. Roughness=1 keeps
    // the IBL horizon from showing up as a sharp seam (mirror-finish
    // unauthored materials).
    defaults_.NeutralOrm  = Make1x1(gfx_, 0x0000FFFFu);   // (255,255,0,0)
    defaults_.Missing     = Make1x1(gfx_, 0xFFFF00FFu);   // magenta (255,0,255,255)
}

TextureAssetManager::~TextureAssetManager() {
    // Shared cache should be empty by the time we get here — every
    // ModelScope's destructor drops its borrows before this runs. If
    // anything's still alive (paranoia / leak path), free it.
    for (auto& [k, e] : shared_) {
        if (e.handle != gfx::TextureHandle::Invalid) gfx_.Destroy(e.handle);
    }
    shared_.clear();
    for (auto& [name, h] : owned_) {
        if (h != gfx::TextureHandle::Invalid) gfx_.Destroy(h);
    }
    owned_.clear();
    gfx_.Destroy(defaults_.White);
    gfx_.Destroy(defaults_.Black);
    gfx_.Destroy(defaults_.FlatNormal);
    gfx_.Destroy(defaults_.NeutralOrm);
    gfx_.Destroy(defaults_.Missing);
    defaults_ = {};
}

// ============================================================================
// Externally-allocated owned textures
// ============================================================================

void TextureAssetManager::RegisterOwned(std::string name, gfx::TextureHandle handle) {
    if (auto it = owned_.find(name); it != owned_.end()) {
        if (it->second != gfx::TextureHandle::Invalid && it->second != handle)
            gfx_.Destroy(it->second);
        if (handle == gfx::TextureHandle::Invalid) {
            owned_.erase(it);
            return;
        }
        it->second = handle;
        return;
    }
    if (handle != gfx::TextureHandle::Invalid)
        owned_.emplace(std::move(name), handle);
}

void TextureAssetManager::ReleaseOwned(std::string_view name) {
    auto it = owned_.find(name);
    if (it == owned_.end()) return;
    if (it->second != gfx::TextureHandle::Invalid) gfx_.Destroy(it->second);
    owned_.erase(it);
}

gfx::TextureHandle TextureAssetManager::GetOwned(std::string_view name) const noexcept {
    auto it = owned_.find(name);
    return (it != owned_.end()) ? it->second : gfx::TextureHandle::Invalid;
}

std::vector<TextureAssetManager::DebugEntry>
TextureAssetManager::DebugSnapshotOwned() const {
    std::vector<DebugEntry> out;
    out.reserve(owned_.size());
    for (const auto& [name, handle] : owned_) {
        out.push_back({name, handle});
    }
    return out;
}

// ============================================================================
// Shared cross-model texture cache
// ============================================================================

gfx::TextureHandle
TextureAssetManager::AcquireShared(std::string_view        key,
                                   const gfx::TextureDesc& desc,
                                   const void*             pixels) {
    // Two-phase: check under lock, but do the GPU CreateTexture *outside*
    // the lock so adapter threads polling IsCachedShared don't block on
    // the upload. We accept that two render-thread acquires of the same
    // brand-new key could race here; the manager only mutates from the
    // render thread today, so that's not actually a concern, but the
    // defensive split is essentially free.
    {
        std::lock_guard<std::mutex> lock(sharedMutex_);
        sharedTotalAcquires_++;
        if (auto it = shared_.find(key); it != shared_.end()) {
            it->second.refCount++;
            sharedCacheHits_++;
            return it->second.handle;
        }
    }
    // Miss — create GPU resource (no lock held), then insert.
    gfx::TextureHandle h = gfx_.CreateTexture(desc, pixels);
    {
        std::lock_guard<std::mutex> lock(sharedMutex_);
        SharedEntry e;
        e.handle   = h;
        e.refCount = 1;
        auto [it, inserted] = shared_.emplace(std::string(key), e);
        if (!inserted) {
            // Lost a brand-new-key race against another caller. Drop the
            // texture we just created and use the existing one. Defensive
            // — single-render-thread invariant means this shouldn't fire.
            gfx_.Destroy(h);
            it->second.refCount++;
            sharedCacheHits_++;
        }
        return it->second.handle;
    }
}

void TextureAssetManager::ReleaseShared(std::string_view key) {
    gfx::TextureHandle toDestroy = gfx::TextureHandle::Invalid;
    {
        std::lock_guard<std::mutex> lock(sharedMutex_);
        auto it = shared_.find(key);
        if (it == shared_.end()) return;
        if (it->second.refCount > 0) it->second.refCount--;
        if (it->second.refCount == 0) {
            toDestroy = it->second.handle;
            shared_.erase(it);
        }
    }
    if (toDestroy != gfx::TextureHandle::Invalid) gfx_.Destroy(toDestroy);
}

bool TextureAssetManager::IsCachedShared(std::string_view key) const {
    std::lock_guard<std::mutex> lock(sharedMutex_);
    return shared_.find(key) != shared_.end();
}

gfx::TextureHandle
TextureAssetManager::TryAcquireShared(std::string_view key) {
    std::lock_guard<std::mutex> lock(sharedMutex_);
    sharedTotalAcquires_++;
    auto it = shared_.find(key);
    if (it == shared_.end()) return gfx::TextureHandle::Invalid;
    it->second.refCount++;
    sharedCacheHits_++;
    return it->second.handle;
}

TextureAssetManager::SharedStats
TextureAssetManager::GetSharedStats() const {
    std::lock_guard<std::mutex> lock(sharedMutex_);
    SharedStats s;
    s.uniqueEntries = shared_.size();
    s.totalAcquires = sharedTotalAcquires_;
    s.cacheHits     = sharedCacheHits_;
    return s;
}

void TextureAssetManager::ResetSharedStats() {
    std::lock_guard<std::mutex> lock(sharedMutex_);
    sharedTotalAcquires_ = 0;
    sharedCacheHits_     = 0;
}

// ============================================================================
// ModelScope
// ============================================================================

std::unique_ptr<TextureAssetManager::ModelScope>
TextureAssetManager::CreateModelScope() {
    // make_unique can't reach the private constructor; pass the device + the
    // owning manager by hand. ModelScope needs the manager handle to release
    // its shared borrows (refcount decrements happen on Clear/destroy).
    return std::unique_ptr<ModelScope>(new ModelScope(gfx_, *this));
}

TextureAssetManager::ModelScope::~ModelScope() {
    Clear();
}

void TextureAssetManager::ModelScope::Clear() {
    for (auto& [id, e] : entries_) {
        if (e.tex == gfx::TextureHandle::Invalid) continue;
        if (e.sharedKey.empty())
            gfx_.Destroy(e.tex);                      // owned: free directly
        else
            mgr_.ReleaseShared(e.sharedKey);          // shared: drop the borrow
    }
    entries_.clear();
}

void TextureAssetManager::ModelScope::DropEntry(int textureId) {
    auto it = entries_.find(textureId);
    if (it == entries_.end()) return;
    if (it->second.tex != gfx::TextureHandle::Invalid) {
        if (it->second.sharedKey.empty())
            gfx_.Destroy(it->second.tex);
        else
            mgr_.ReleaseShared(it->second.sharedKey);
    }
    entries_.erase(it);
}

gfx::TextureHandle
TextureAssetManager::ModelScope::Upload(int                     textureId,
                                        const gfx::TextureDesc& desc,
                                        const void*             pixels,
                                        uint32_t                wrapFlags) {
    // Replace-in-place semantics: if the slot already holds a texture
    // (owned or shared), drop it through the right path first so the GPU
    // resource doesn't leak / shared refcount stays accurate.
    DropEntry(textureId);
    auto& e = entries_[textureId];
    e.tex       = gfx_.CreateTexture(desc, pixels);
    e.wrapFlags = wrapFlags;
    e.sharedKey.clear();
    return e.tex;
}

gfx::TextureHandle
TextureAssetManager::ModelScope::UploadShared(int                     textureId,
                                              std::string_view        sharedKey,
                                              const gfx::TextureDesc& desc,
                                              const void*             pixels,
                                              uint32_t                wrapFlags) {
    // Borrow from the cross-model cache. Manager owns the GPU lifetime;
    // we just track the borrow so Clear() releases it correctly.
    DropEntry(textureId);
    auto& e = entries_[textureId];
    e.sharedKey.assign(sharedKey);
    e.tex       = mgr_.AcquireShared(e.sharedKey, desc, pixels);
    e.wrapFlags = wrapFlags;
    return e.tex;
}

gfx::TextureHandle
TextureAssetManager::ModelScope::BindShared(int              textureId,
                                            std::string_view sharedKey,
                                            uint32_t         wrapFlags) {
    // Borrow-only: the adapter saw this key cached and skipped the BLP/
    // CASC decode, so we have no pixels here. TryAcquireShared returns
    // Invalid if the entry was evicted between the adapter's check and
    // now. In that case we leave the slot empty — Get() returns Invalid
    // and the bind sites fall back to the manager's defaults.White
    // (or magenta-missing if it surfaces visibly).
    DropEntry(textureId);
    gfx::TextureHandle h = mgr_.TryAcquireShared(sharedKey);
    if (h == gfx::TextureHandle::Invalid) {
        // Eviction race lost. Don't insert an entry; let the bind path
        // pick up the default fallback for `Get(textureId) == Invalid`.
        return gfx::TextureHandle::Invalid;
    }
    auto& e = entries_[textureId];
    e.sharedKey.assign(sharedKey);
    e.tex       = h;
    e.wrapFlags = wrapFlags;
    return h;
}

gfx::TextureHandle
TextureAssetManager::ModelScope::Get(int textureId) const noexcept {
    auto it = entries_.find(textureId);
    return (it != entries_.end()) ? it->second.tex : gfx::TextureHandle::Invalid;
}

uint32_t TextureAssetManager::ModelScope::WrapFlags(int textureId) const noexcept {
    auto it = entries_.find(textureId);
    return (it != entries_.end()) ? it->second.wrapFlags : kSamplerWrapBitsMask;
}

} // namespace WhiteoutDex
