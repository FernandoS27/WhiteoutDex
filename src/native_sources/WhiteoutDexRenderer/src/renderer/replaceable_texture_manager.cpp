// ============================================================================
// ReplaceableTextureManager — implementation.
// ============================================================================

#include "replaceable_texture_manager.h"
#include "texture_asset_manager.h"
#include "team_glow_data.h"
#include "../io/content_provider.h"
#include "../io/event_data.h"
#include "../io/replaceable_paths.h"
#include "model_source_utils.h"   // DispatchTextureParser, ExtensionLower

#include <cstdio>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // OutputDebugStringA

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

void ReplaceableTextureManager::SetContentProvider(IContentProvider* p) {
    contentProvider_ = p;
    // Pull the game's TerrainArt SLK tables now so the canonical path
    // resolver in io::ReplaceableCanonicalPath can use them. Idempotent;
    // safe to call again on subsequent provider changes.
    io::LoadGameDataFiles(p);
    // Same hand-off doubles as the trigger for the EventObject SLK
    // tables (Splats/SpawnData, SplatData, UberSplatData; AnimLookups
    // chained to AnimSounds). io::Find{Spn,Spl,Ubr,Snd} is empty until
    // this fires, so EventObjects loaded before a content provider is
    // wired silently no-op until the user picks a CASC root.
    io::LoadEventDataFiles(p);
}

ReplaceableTextureManager::~ReplaceableTextureManager() {
    Shutdown();
}

void ReplaceableTextureManager::Shutdown() {
    if (hdSwatchTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(hdSwatchTex_);
        hdSwatchTex_    = gfx::TextureHandle::Invalid;
        lastSwatchRgba_ = 0xFFFFFFFFu;
    }
    if (sdTeamColorTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(sdTeamColorTex_);
        sdTeamColorTex_ = gfx::TextureHandle::Invalid;
    }
    if (sdTeamGlowTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(sdTeamGlowTex_);
        sdTeamGlowTex_ = gfx::TextureHandle::Invalid;
    }
    lastSdSwatchRgba_ = 0xFFFFFFFFu;
    slots_.clear();
}

void ReplaceableTextureManager::SetTeamColor(uint8_t r, uint8_t g, uint8_t b) {
    // BGR-packed — matches the legacy uint32 wire format the rest of the
    // renderer + UI exchanges this through.
    teamColor_ = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
    // Re-bake every registered slot with the new swatch. Tileset-driven
    // ids 11..36 are colour-independent but BakeSlot is cheap for them
    // (path-resolve + decode happens once per slot anyway), so we don't
    // bother filtering — keeps the caller simple.
    for (auto& [mi, slots] : slots_) {
        for (auto& s : slots) BakeSlot(*mi, s.textureId, (int)s.replaceableId);
    }
    // Drop cached global SD swatches so the next Get* call rebuilds
    // them with the new tint. HD swatch follows the same pattern via
    // `lastSwatchRgba_` mismatch.
    if (sdTeamColorTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(sdTeamColorTex_);
        sdTeamColorTex_ = gfx::TextureHandle::Invalid;
    }
    if (sdTeamGlowTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(sdTeamGlowTex_);
        sdTeamGlowTex_ = gfx::TextureHandle::Invalid;
    }
    lastSdSwatchRgba_ = 0xFFFFFFFFu;
    dirty_.store(true);
}

void ReplaceableTextureManager::SetTileset(io::Tileset ts) {
    io::SetCurrentTileset(ts);
    // Only the cliff replaceable (id 11) is tileset-driven; tree ids
    // 31..37 are static per-id (verified against TerrainArt/Terrain.slk
    // — no tree column exists). TeamColor / TeamGlow are
    // tileset-independent. Re-bake only the slots that can actually
    // change.
    for (auto& [mi, slots] : slots_) {
        for (auto& s : slots) {
            if (s.replaceableId >= 11 && s.replaceableId <= 14)
                BakeSlot(*mi, s.textureId, (int)s.replaceableId);
        }
    }
    dirty_.store(true);
}

void ReplaceableTextureManager::RegisterModelSlot(Actor& mi,
                                                  int    textureId,
                                                  int    replaceableId) {
    // Skip the no-replaceable case and any unmodeled id — the slot stays
    // as whatever pixels the adapter staged (or empty, if the adapter
    // declared a placeholder).
    const bool isTeamColor = (replaceableId == 1);
    const bool isTeamGlow  = (replaceableId == 2);
    const bool isCanonical = (replaceableId >= 11 && replaceableId <= 37);
    if (!isTeamColor && !isTeamGlow && !isCanonical) return;

    auto& list = slots_[&mi];
    // Idempotent on (textureId, replaceableId). A textureId shared
    // across two declared replaceables is degenerate — record both so
    // re-bakes (SetTeamColor / SetTileset) cover them, but warn that
    // the staged pixel slot is one-per-textureId and the last bake wins.
    for (auto& s : list)
        if (s.textureId == textureId && (int)s.replaceableId == replaceableId) return;
    for (auto& s : list)
        if (s.textureId == textureId && (int)s.replaceableId != replaceableId) {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                "[WDEX replaceable] textureId %d registered with replaceableId %d AND %d "
                "— second registration overwrites the first's pixels on bake.\n",
                textureId, (int)s.replaceableId, replaceableId);
            OutputDebugStringA(msg);
            break;
        }
    list.push_back({textureId, static_cast<uint8_t>(replaceableId)});
    BakeSlot(mi, textureId, replaceableId);
}

void ReplaceableTextureManager::UnregisterModel(Actor& mi) {
    slots_.erase(&mi);
}

namespace {
// Decode a CASC-loaded BLP/DDS/TGA buffer into RGBA8 pixels, picking the
// extension from the content provider's match. Returns false on miss /
// decode failure.
bool DecodeCanonicalAsset(IContentProvider& cp, const std::string& path,
                          std::vector<uint8_t>& outPixels, int& outW, int& outH) {
    std::string foundExt;
    auto data = cp.ReadFile(path, &foundExt);
    if (!data) {
        std::fprintf(stderr,
                     "[textures] ERR: ReplaceableTexture read FAIL '%s'\n",
                     path.c_str());
        return false;
    }
    if (foundExt.empty()) foundExt = ExtensionLower(std::filesystem::path(path));

    auto result = DispatchTextureParser(foundExt,
        [&](auto& parser) { return parser.parse(*data); });
    if (!result) {
        std::fprintf(stderr,
                     "[textures] ERR: ReplaceableTexture decode FAIL '%s' "
                     "ext='%s' bytes=%zu\n",
                     path.c_str(), foundExt.c_str(), data->size());
        return false;
    }

    // Force RGBA8 so we don't have to plumb format/mips through the
    // staged-texture pixel buffer for the replaceable path.
    result->format(whiteout::textures::PixelFormat::RGBA8);
    outW = (int)result->width();
    outH = (int)result->height();
    if (outW <= 0 || outH <= 0) {
        std::fprintf(stderr,
                     "[textures] ERR: ReplaceableTexture invalid size '%s' "
                     "%dx%d\n",
                     path.c_str(), outW, outH);
        return false;
    }
    auto mip0 = result->mipData(0);
    outPixels.assign(mip0.begin(), mip0.end());
    return true;
}
} // namespace

void ReplaceableTextureManager::BakeSlot(Actor& mi,
                                         int    textureId,
                                         int    replaceableId) {
    const uint8_t r = Red(teamColor_);
    const uint8_t g = Green(teamColor_);
    const uint8_t b = Blue(teamColor_);

    StagedTexture& st = mi.render.stagedTextures[textureId];
    st.replaceableId = replaceableId;
    // All baked replaceable pixels are single-mip RGBA8. Reset format
    // and mipLevels in case a previous pass left stale values from a
    // BC3/BC5 image in this slot.
    st.format    = gfx::Format::R8G8B8A8_UNORM;
    st.mipLevels = 1;

    if (replaceableId == 2) {
        // TeamGlow — embedded TGA decoded + tinted with the swatch.
        st.pixels = DecodeTeamGlow(r, g, b, st.width, st.height);
    } else if (replaceableId == 1) {
        // TeamColor — flat 4×4 RGBA in the swatch colour.
        st.width  = 4;
        st.height = 4;
        st.pixels.resize(64);
        for (int j = 0; j < 16; j++) {
            st.pixels[j*4 + 0] = r;
            st.pixels[j*4 + 1] = g;
            st.pixels[j*4 + 2] = b;
            st.pixels[j*4 + 3] = 255;
        }
    } else {
        // Tileset-driven canonical asset — resolve the path through the
        // shared `io::ReplaceableCanonicalPath` table (see
        // `io/replaceable_paths.h`) and read the BLP via the borrowed
        // IFileContentProvider. Fall back to a magenta 4×4 if the
        // provider isn't wired or the asset can't be loaded so the
        // missing data is visible at a glance instead of silently
        // showing blank.
        const char* canon = io::ReplaceableCanonicalPath(replaceableId);
        bool loaded = false;
        if (canon && contentProvider_) {
            loaded = DecodeCanonicalAsset(*contentProvider_, canon,
                                          st.pixels, st.width, st.height);
        }
        if (!loaded) {
            st.width  = 4;
            st.height = 4;
            st.pixels.assign(64, 0);
            for (int j = 0; j < 16; ++j) {
                st.pixels[j*4 + 0] = 255;  // R
                st.pixels[j*4 + 1] = 0;    // G
                st.pixels[j*4 + 2] = 255;  // B
                st.pixels[j*4 + 3] = 255;  // A
            }
        }
    }
    mi.render.stagedDirty = true;
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

gfx::TextureHandle ReplaceableTextureManager::GetSdTeamColorTexture() {
    // 4×4 RGBA solid filled with the current team colour. Used by the
    // PE2 SD draw path when an emitter declares replaceableId=1 — it
    // binds this in place of the loaded BLP so a sibling emitter that
    // shares the same textureId but DOESN'T declare a replaceable
    // still sees the unmodified BLP at slot N.
    const uint8_t r = Red(teamColor_);
    const uint8_t g = Green(teamColor_);
    const uint8_t b = Blue(teamColor_);
    const uint32_t rgba =
        (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
    if (sdTeamColorTex_ != gfx::TextureHandle::Invalid && lastSdSwatchRgba_ == rgba)
        return sdTeamColorTex_;
    if (sdTeamColorTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(sdTeamColorTex_);
        sdTeamColorTex_ = gfx::TextureHandle::Invalid;
    }
    uint32_t px[16];
    for (int i = 0; i < 16; ++i) px[i] = rgba;
    sdTeamColorTex_ = gfx_.CreateTexture({
        .width  = 4,
        .height = 4,
        .format = gfx::Format::R8G8B8A8_UNORM,
        .usage  = gfx::TextureUsage::ShaderResource,
    }, px);
    lastSdSwatchRgba_ = rgba;
    return sdTeamColorTex_;
}

gfx::TextureHandle ReplaceableTextureManager::GetSdTeamGlowTexture() {
    // Embedded TGA decoded once and tinted with the current team
    // colour. Same lazy-rebuild contract as the TeamColor swatch.
    const uint8_t r = Red(teamColor_);
    const uint8_t g = Green(teamColor_);
    const uint8_t b = Blue(teamColor_);
    const uint32_t rgba =
        (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
    if (sdTeamGlowTex_ != gfx::TextureHandle::Invalid && lastSdSwatchRgba_ == rgba)
        return sdTeamGlowTex_;
    if (sdTeamGlowTex_ != gfx::TextureHandle::Invalid) {
        gfx_.Destroy(sdTeamGlowTex_);
        sdTeamGlowTex_ = gfx::TextureHandle::Invalid;
    }
    int w = 0, h = 0;
    std::vector<uint8_t> pixels = DecodeTeamGlow(r, g, b, w, h);
    if (w <= 0 || h <= 0 || pixels.empty()) return gfx::TextureHandle::Invalid;
    sdTeamGlowTex_ = gfx_.CreateTexture({
        .width  = w,
        .height = h,
        .format = gfx::Format::R8G8B8A8_UNORM,
        .usage  = gfx::TextureUsage::ShaderResource,
    }, pixels.data());
    lastSdSwatchRgba_ = rgba;
    return sdTeamGlowTex_;
}

} // namespace WhiteoutDex
