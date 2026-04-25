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
    defaults_.FlatNormal  = Make1x1(gfx_, 0xFF808080u);   // (128,128,128,255) — see note in header
    defaults_.NeutralOrm  = Make1x1(gfx_, 0x0000FFFFu);   // (255,255,0,0)
    defaults_.Missing     = Make1x1(gfx_, 0xFFFF00FFu);   // magenta (255,0,255,255)
}

TextureAssetManager::~TextureAssetManager() {
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
// ModelScope
// ============================================================================

std::unique_ptr<TextureAssetManager::ModelScope>
TextureAssetManager::CreateModelScope() {
    // make_unique can't reach the private constructor; pass the device by
    // hand. The scope's lifetime is independent of the manager — the
    // destructor handles its own cleanup, the manager just hands them out.
    return std::unique_ptr<ModelScope>(new ModelScope(gfx_));
}

TextureAssetManager::ModelScope::~ModelScope() {
    Clear();
}

void TextureAssetManager::ModelScope::Clear() {
    for (auto& [id, e] : entries_) {
        if (e.tex != gfx::TextureHandle::Invalid) gfx_.Destroy(e.tex);
    }
    entries_.clear();
}

gfx::TextureHandle
TextureAssetManager::ModelScope::Upload(int                     textureId,
                                        const gfx::TextureDesc& desc,
                                        const void*             pixels,
                                        uint32_t                wrapFlags) {
    // Replace-in-place semantics: if the slot already holds a texture,
    // destroy it first so the GPU resource doesn't leak. Matches the
    // previous `if (count(id)) gpuTextures[id].Release()` behaviour in
    // RenderService::UploadStagedTextures.
    auto& e = entries_[textureId];
    if (e.tex != gfx::TextureHandle::Invalid) gfx_.Destroy(e.tex);
    e.tex       = gfx_.CreateTexture(desc, pixels);
    e.wrapFlags = wrapFlags;
    return e.tex;
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
