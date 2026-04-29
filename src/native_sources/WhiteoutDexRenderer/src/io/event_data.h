#pragma once
// ============================================================================
// EventObject SLK lookup tables.
//
// MDX EventObject nodes carry a 4-char prefix in their `name` field:
//   SPN  → spawn a sub-MDX (Splats/SpawnData.slk → Model column)
//   SPL  → world-space splat decal (Splats/SplatData.slk)
//   UBR  → 3-stop "uber splat" decal (Splats/UberSplatData.slk)
//   FPT  → footprint — aliased to SPL (same SLK row)
//   SND  → 3D sound (UI/SoundInfo/AnimLookups.slk → SoundLabel → AnimSounds.slk)
// The remaining 4 chars after the dash select a specific row within each SLK.
//
// One per-type table is parsed once and cached; the per-type Find*
// helpers are then read-only and lock-free. Loading happens via the
// SetContentProvider hand-off (see ReplaceableTextureManager).
// ============================================================================

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteoutDex {
class IContentProvider;
}

namespace WhiteoutDex::io {

// Splats/SpawnData.slk — Model column rewritten .mdl→.mdx at parse time.
struct SpnEntry {
    std::string modelPath;
};

// Splats/SplatData.slk — world-space decal with 2 color stops over
// (Lifespan, Decay) and per-segment animated UV cells over a Columns×Rows
// sheet. FPT shares this table.
struct SplEntry {
    std::string file;
    float       scale          = 1.0f;
    float       startC[4]      = {1, 1, 1, 1};
    float       midC[4]        = {1, 1, 1, 1};
    float       endC[4]        = {1, 1, 1, 1};
    int         columns        = 1;
    int         rows           = 1;
    float       lifespan       = 0.0f;
    float       decay          = 0.0f;
    int         uvLifeStart    = 0;
    int         uvLifeEnd      = 0;
    int         lifespanRepeat = 1;
    int         uvDecayStart   = 0;
    int         uvDecayEnd     = 0;
    int         decayRepeat    = 1;
    int         blendMode      = 0;
};

// Splats/UberSplatData.slk — 3 color stops (Birth, Pause, Decay) on a
// single non-tiled sheet. cols=rows=1 implicit.
struct UbrEntry {
    std::string file;
    float       scale     = 1.0f;
    float       c[3][4]   = { {1,1,1,1}, {1,1,1,1}, {1,1,1,1} };
    float       birthTime = 0.0f;
    float       pauseTime = 0.0f;
    float       decay     = 0.0f;
    int         blendMode = 0;
};

// AnimSounds row (chained from AnimLookups.SoundLabel for the SND id).
struct SndEntry {
    std::vector<std::string> fileNames;
    std::string              filepath;
    float                    volume         = 1.0f;
    float                    minDistance    = 0.0f;
    float                    maxDistance    = 0.0f;
    float                    distanceCutoff = 0.0f;
};

// One-shot SLK load — pulls SpawnData / SplatData / UberSplatData /
// AnimLookups / AnimSounds from CASC via the supplied content provider.
// Idempotent: subsequent calls re-parse only when `force` is true. Safe
// to call before a content provider exists (no-op until one is wired).
void LoadEventDataFiles(IContentProvider* cp, bool force = false);

// Per-prefix lookup. `id` is the post-dash suffix from the EventObject
// name (chars 4..7). Returns nullptr if the SLK row is missing or the
// SLKs haven't been loaded yet.
const SpnEntry* FindSpn(std::string_view id);
const SplEntry* FindSpl(std::string_view id);   // FPT routes here too
const UbrEntry* FindUbr(std::string_view id);
const SndEntry* FindSnd(std::string_view id);

} // namespace WhiteoutDex::io
