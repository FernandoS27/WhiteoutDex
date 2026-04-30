// ============================================================================
// Replaceable path resolution — implementation.
// ============================================================================

#include "replaceable_paths.h"
#include "content_provider.h"
#include "slk.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace WhiteoutDex::io {

namespace {

// Backing store for the global selector. Reads + writes are both lock-
// free uint8 atomics; the enum is small enough that any ABI provides
// native atomicity here.
std::atomic<uint8_t> g_currentTileset{static_cast<uint8_t>(Tileset::LordaeronSummer)};

// One-letter game-side tileset code per Tileset enumerator. Matches
// the codes shipped in TerrainArt/CliffTypes.slk's first column.
char TilesetCode(Tileset ts) {
    switch (ts) {
        case Tileset::LordaeronSummer: return 'L';
        case Tileset::Ashenvale:       return 'A';
        case Tileset::Barrens:         return 'B';
        case Tileset::Northrend:       return 'N';
        case Tileset::Felwood:         return 'F';
        case Tileset::Dungeon:         return 'D';
        case Tileset::Cityscape:       return 'C';
        case Tileset::LordaeronFall:   return 'Q';
        case Tileset::LordaeronWinter: return 'W';
        case Tileset::Outland:         return 'O';
        case Tileset::SunkenRuins:     return 'Z';
        case Tileset::IcecrownGlacier: return 'I';
        case Tileset::DalaranRuins:    return 'X';
        case Tileset::BlackCitadel:    return 'K';
        case Tileset::Underground:     return 'G';
        case Tileset::Village:         return 'V';
        default:                       return 'L';
    }
}

// Cached tileset-code → cliff-blp map, populated by LoadGameDataFiles
// from TerrainArt/CliffTypes.slk. Empty until the SLKs are parsed.
struct DataCache {
    std::mutex                                  mu;
    bool                                        loaded = false;
    // Key = tileset single-char code (first letter of cliffID),
    // value = full CASC-relative blp path. Used by replaceableId 11
    // (cliff). Populated from CliffTypes.slk. Tree ids 31..37 are
    // verified to be static per-id (Terrain.slk has no tree column),
    // so they don't need a per-tileset map.
    std::unordered_map<char, std::string>       cliffByTileset;
    // Per-id-storage for explicit replaceable-id rows (currently
    // unused but reserved for SLKs that may carry direct id columns).
    std::unordered_map<int, std::string>        pathById;
};
DataCache& Cache() { static DataCache c; return c; }

// Utility — turn a SLK row pair (texDir, texFile) into a canonical
// CASC path. Game-side, the dir lives under either ReplaceableTextures
// or Doodads/Terrain depending on context; CliffTypes.slk uses the
// latter. We return whatever the SLK has and let the content provider
// canonicalise separators / case.
std::string JoinDirFile(std::string_view dir, std::string_view file, std::string_view ext) {
    std::string out;
    out.reserve(dir.size() + 1 + file.size() + ext.size());
    out.append(dir);
    if (!out.empty() && out.back() != '\\' && out.back() != '/')
        out += '\\';
    out.append(file);
    out.append(ext);
    return out;
}

void LogHeaders(const char* /*label*/, const SlkTable& /*t*/) {
    // Header dump suppressed — re-enable for SLK schema diagnosis.
}

// Try a small set of plausible column names so a Reforged update that
// renames a header doesn't silently bork the lookup.
int FindAny(const SlkTable& t, std::initializer_list<const char*> names) {
    for (auto* n : names) { int c = t.FindColumn(n); if (c >= 0) return c; }
    return -1;
}

void LoadCliffTypes(IContentProvider& cp, DataCache& cache) {
    auto data = cp.ReadFile("TerrainArt\\CliffTypes.slk", nullptr);
    if (!data) return;
    SlkTable t = ParseSlk(*data);
    LogHeaders("CliffTypes.slk", t);
    const int colId   = FindAny(t, {"cliffID"});
    const int colDir  = FindAny(t, {"texDir", "dir"});
    const int colFile = FindAny(t, {"texFile", "file"});
    if (colId < 0 || colDir < 0 || colFile < 0) {
        std::fprintf(stderr,
            "[WDEX replaceable] CliffTypes.slk missing expected columns "
            "(cliffID=%d texDir=%d texFile=%d). Falling back to hardcoded path.\n",
            colId, colDir, colFile);
        return;
    }
    for (size_t r = 1; r < t.RowCount(); ++r) {
        std::string_view id   = t.Cell(r, colId);
        std::string_view dir  = t.Cell(r, colDir);
        std::string_view file = t.Cell(r, colFile);
        if (id.empty() || dir.empty() || file.empty()) continue;
        cache.cliffByTileset[id[0]] = JoinDirFile(dir, file, "0.blp");
    }
}

} // namespace

const char* TilesetName(Tileset ts) {
    switch (ts) {
        case Tileset::LordaeronSummer: return "Lordaeron Summer";
        case Tileset::Ashenvale:       return "Ashenvale";
        case Tileset::Barrens:         return "Barrens";
        case Tileset::Northrend:       return "Northrend";
        case Tileset::Felwood:         return "Felwood";
        case Tileset::Dungeon:         return "Dungeon";
        case Tileset::Cityscape:       return "Cityscape";
        case Tileset::LordaeronFall:   return "Lordaeron Fall";
        case Tileset::LordaeronWinter: return "Lordaeron Winter";
        case Tileset::Outland:         return "Outland";
        case Tileset::SunkenRuins:     return "Sunken Ruins";
        case Tileset::IcecrownGlacier: return "Icecrown Glacier";
        case Tileset::DalaranRuins:    return "Dalaran Ruins";
        case Tileset::BlackCitadel:    return "Black Citadel";
        case Tileset::Underground:     return "Underground";
        case Tileset::Village:         return "Village";
        default:                       return "Unknown";
    }
}

void SetCurrentTileset(Tileset ts) {
    if (static_cast<uint8_t>(ts) >= static_cast<uint8_t>(Tileset::Count)) return;
    g_currentTileset.store(static_cast<uint8_t>(ts));
}

Tileset GetCurrentTileset() {
    return static_cast<Tileset>(g_currentTileset.load());
}

void LoadGameDataFiles(IContentProvider* cp, bool force) {
    if (!cp) return;
    auto& c = Cache();
    std::lock_guard<std::mutex> lk(c.mu);
    if (c.loaded && !force) return;
    c.cliffByTileset.clear();
    c.pathById.clear();
    LoadCliffTypes(*cp, c);
    c.loaded = true;
}

const char* ReplaceableCanonicalPath(int replaceableId, Tileset ts) {
    // Authoritative mapping is the one used by mdx-m3-viewer
    // (handlers/mdx/replaceableids.ts in flowtsohg/mdx-m3-viewer):
    //
    //    1  → TeamColor/TeamColor00          (handled by SD swatch)
    //    2  → TeamGlow/TeamGlow00             (handled by SD swatch)
    //   11  → Cliff/Cliff0
    //   21  → ""           (cursor sentinel — no asset)
    //   31  → LordaeronTree/LordaeronSummerTree
    //   32  → AshenvaleTree/AshenTree
    //   33  → BarrensTree/BarrensTree
    //   34  → NorthrendTree/NorthTree
    //   35  → Mushroom/MushroomTree
    //   36  → RuinsTree/RuinsTree
    //   37  → OutlandMushroomTree/MushroomTree
    //
    // Tree paths (31..37) and TeamColor / TeamGlow (1, 2) are
    // tileset-independent. Cliff (id 11) is tileset-aware: if SLK
    // data is loaded we look the path up by the active tileset's
    // single-char code (`L` = LordaeronSummer, `A` = Ashenvale, …)
    // — see TilesetCode. Falls back to ReplaceableTextures\Cliff\Cliff0.blp
    // if SLK ingestion hasn't run or the row is missing.
    switch (replaceableId) {
        case 11: {
            auto& cache = Cache();
            std::lock_guard<std::mutex> lk(cache.mu);
            // Per-id explicit override (covers any future cases where
            // an SLK directly assigns paths by replaceable id).
            if (auto it = cache.pathById.find(11); it != cache.pathById.end())
                return it->second.c_str();
            const char code = TilesetCode(ts);
            if (auto it = cache.cliffByTileset.find(code); it != cache.cliffByTileset.end())
                return it->second.c_str();
            return "ReplaceableTextures\\Cliff\\Cliff0.blp";
        }

        // 21 is a deliberate empty in mdx-m3-viewer (cursor models).
        // We surface that as nullptr so BakeSlot leaves the slot alone.
        case 21: return nullptr;

        // Trees / mushrooms — verified static per-id (mdx-m3-viewer
        // table). Terrain.slk has no tree column, so these don't vary
        // with tileset; each id is wedded to a specific tree BLP and
        // the unit MDX picks which tree by id.
        case 31: return "ReplaceableTextures\\LordaeronTree\\LordaeronSummerTree.blp";
        case 32: return "ReplaceableTextures\\AshenvaleTree\\AshenTree.blp";
        case 33: return "ReplaceableTextures\\BarrensTree\\BarrensTree.blp";
        case 34: return "ReplaceableTextures\\NorthrendTree\\NorthTree.blp";
        case 35: return "ReplaceableTextures\\Mushroom\\MushroomTree.blp";
        case 36: return "ReplaceableTextures\\RuinsTree\\RuinsTree.blp";
        case 37: return "ReplaceableTextures\\OutlandMushroomTree\\MushroomTree.blp";

        default: return nullptr;
    }
}

const char* ReplaceableCanonicalPath(int replaceableId) {
    return ReplaceableCanonicalPath(replaceableId, GetCurrentTileset());
}

} // namespace WhiteoutDex::io
