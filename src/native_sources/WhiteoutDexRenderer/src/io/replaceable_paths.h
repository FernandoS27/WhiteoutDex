#pragma once
// ============================================================================
// Replaceable-id → CASC path resolution.
//
// MDX texture entries with a non-zero `replaceableId` are placeholders the
// engine substitutes with a real BLP at runtime. preview.exe's
// `ProcessTextures @0x7ff609b6a260` shortcuts these to TextureCreateSolid
// (white) and only the real game (Warcraft III.exe) registers the higher
// ids 11..36 at map-load time, with paths drawn from the active tileset.
//
// We mirror the game's behaviour for the previewer: a tileset selector
// drives which ground/cliff/edge BLPs come up for ids 11..14 and 31..36.
// Tree ids 21..26 are intentionally tileset-independent — the game maps
// each id to a fixed asset and lets the unit MDX pick one explicitly.
//
// Shared between MdxModelAdapter and MaxSceneAdapter so both texture
// pipelines resolve replaceable ids the same way; the renderer's UI
// publishes the active tileset via SetCurrentTileset().
// ============================================================================

#include <cstdint>

namespace WhiteoutDex {
class IContentProvider;
}

namespace WhiteoutDex::io {

// Tileset codes mirror the single-letter wc3 internal ids ("L" = Lordaeron
// Summer, etc.). Index order matches the UI combobox so a CB_GETCURSEL
// cast lands on the correct enumerator.
enum class Tileset : uint8_t {
    LordaeronSummer = 0,
    Ashenvale,
    Barrens,
    Northrend,
    Felwood,
    Dungeon,
    Cityscape,
    LordaeronFall,
    LordaeronWinter,
    Outland,
    SunkenRuins,
    IcecrownGlacier,
    DalaranRuins,
    BlackCitadel,
    Underground,
    Village,
    Count,
};

// Display label for the UI combobox.
const char* TilesetName(Tileset ts);

// Global tileset selector. Atomic so the UI thread (set) and texture
// load workers (read) don't race. Default = LordaeronSummer.
void     SetCurrentTileset(Tileset ts);
Tileset  GetCurrentTileset();

// Canonical CASC path for the given replaceable id under the supplied
// tileset. Returns nullptr for ids the previewer can't resolve (the
// caller falls back to the white-substitute / no-texture behaviour).
//
// Coverage:
//   11..14 (Cliffs)         — tileset-aware cliff variants.
//   21..26 (Trees/mushrooms) — fixed paths regardless of tileset.
//   31..36 (Tileset edges)   — tileset-aware fallback to cliff blp.
const char* ReplaceableCanonicalPath(int replaceableId, Tileset ts);

// Convenience: read the current tileset and resolve in one call.
const char* ReplaceableCanonicalPath(int replaceableId);

// One-shot SLK load — pulls TerrainArt/CliffTypes.slk and TerrainArt/Terrain.slk
// from CASC via the supplied content provider and indexes them by tileset code.
// The path resolver consults the parsed tables before falling back to the
// hardcoded mdx-m3-viewer mapping. Idempotent: subsequent calls re-parse if
// `force` is true, otherwise no-op once the tables are loaded. Safe to call
// before a content provider exists — it's a no-op until one is wired.
void LoadGameDataFiles(IContentProvider* cp, bool force = false);

} // namespace WhiteoutDex::io
