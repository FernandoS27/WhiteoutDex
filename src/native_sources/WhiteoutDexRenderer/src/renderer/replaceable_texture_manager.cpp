// ============================================================================
// ReplaceableTextureManager — implementation.
// ============================================================================

#include "replaceable_texture_manager.h"
#include "texture_asset_manager.h"
#include "team_glow_data.h"

namespace WhiteoutDex {

namespace {
// Unpack BGR-packed `teamColor_` into individual bytes. Same layout the
// platform colour picker hands us: low byte = red, mid = green, high = blue.
inline uint8_t Red  (uint32_t bgr) noexcept { return (uint8_t)(bgr        & 0xFF); }
inline uint8_t Green(uint32_t bgr) noexcept { return (uint8_t)((bgr >> 8) & 0xFF); }
inline uint8_t Blue (uint32_t bgr) noexcept { return (uint8_t)((bgr >>16) & 0xFF); }
} // namespace

ReplaceableTextureManager::ReplaceableTextureManager(gfx::IGFXDevice& gfx,
                                                     TextureAssetManager& textures)
    : gfx_(gfx), textures_(textures) {}

ReplaceableTextureManager::~ReplaceableTextureManager() {
    Shutdown();
}

void ReplaceableTextureManager::Shutdown() {
    if (hdSwatchTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(hdSwatchTex_);
        hdSwatchTex_    = gfx::TextureHandle::Invalid;
        lastSwatchRgba_ = 0xFFFFFFFFu;
    }
    slots_.clear();
}

void ReplaceableTextureManager::SetTeamColor(uint8_t r, uint8_t g, uint8_t b) {
    // BGR-packed — matches the legacy uint32 wire format the rest of the
    // renderer + UI exchanges this through.
    teamColor_ = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    // Re-bake every registered SD slot with the new swatch. The render-
    // thread upload pass will pick the staged pixels up via mi.stagedDirty.
    for (auto& [mi, slots] : slots_) {
        for (auto& s : slots) BakeSlot(*mi, s.textureId, s.kind);
    }
    dirty_.store(true);
}

void ReplaceableTextureManager::RegisterModelSlot(ModelInstance&  mi,
                                                  int             textureId,
                                                  ReplaceableKind kind) {
    if (kind == ReplaceableKind::None) return;
    auto& list = slots_[&mi];
    // Idempotent: skip if already registered.
    for (auto& s : list) if (s.textureId == textureId) return;
    list.push_back({textureId, kind});
    BakeSlot(mi, textureId, kind);
}

void ReplaceableTextureManager::UnregisterModel(ModelInstance& mi) {
    slots_.erase(&mi);
}

void ReplaceableTextureManager::BakeSlot(ModelInstance&  mi,
                                          int             textureId,
                                          ReplaceableKind kind) {
    const uint8_t r = Red(teamColor_);
    const uint8_t g = Green(teamColor_);
    const uint8_t b = Blue(teamColor_);

    StagedTexture& st = mi.stagedTextures[textureId];
    st.replaceableId = static_cast<int>(kind);
    // Generated TeamColor / TeamGlow are single-mip RGBA8. Reset format and
    // mipLevels in case a previous pass left stale values from a BC3/BC5
    // image in this same slot — without the resets the upload path would
    // walk off the end of `pixels`.
    st.format    = gfx::Format::R8G8B8A8_UNORM;
    st.mipLevels = 1;

    if (kind == ReplaceableKind::TeamGlow) {
        st.pixels = DecodeTeamGlow(r, g, b, st.width, st.height);
    } else {
        // Flat 4x4 swatch — the SD shader path samples this verbatim.
        st.width  = 4;
        st.height = 4;
        st.pixels.resize(64);
        for (int j = 0; j < 16; j++) {
            st.pixels[j*4 + 0] = r;
            st.pixels[j*4 + 1] = g;
            st.pixels[j*4 + 2] = b;
            st.pixels[j*4 + 3] = 255;
        }
    }
    mi.stagedDirty = true;
}

gfx::TextureHandle ReplaceableTextureManager::GetHdSwatchTexture() {
    // Pack current swatch as RGBA8 (A=255). teamColor_ is BGR-packed so
    // recombine R/G/B explicitly rather than memcpy'ing.
    const uint8_t r = Red(teamColor_);
    const uint8_t g = Green(teamColor_);
    const uint8_t b = Blue(teamColor_);
    const uint32_t rgba =
        (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;

    if (hdSwatchTex_ != gfx::TextureHandle::Invalid && lastSwatchRgba_ == rgba)
        return hdSwatchTex_;
    if (hdSwatchTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(hdSwatchTex_);
        hdSwatchTex_ = gfx::TextureHandle::Invalid;
    }
    uint32_t px = rgba;
    hdSwatchTex_ = gfx_.CreateTexture({
        .width  = 1,
        .height = 1,
        .format = gfx::Format::R8G8B8A8_UNORM,
        .usage  = gfx::TextureUsage::ShaderResource,
    }, &px);
    lastSwatchRgba_ = rgba;
    return hdSwatchTex_;
}

} // namespace WhiteoutDex
