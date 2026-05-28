// MDLXImporter — Shared texture path resolution
#pragma once

#include <string>

namespace mdx_scene {

/// Resolve an MDX-relative texture path to an actual file on disk.
/// Searches modelDir + relative path, then flat (filename only next to model),
/// trying alternative extensions (.dds, .blp, .tga, .png, .tif).
std::wstring resolveTexturePath(const std::wstring& modelDir, const std::wstring& relPath);

/// Resolve texture path with archive fallbacks.
/// Tries (in order): on-disk → CASC → MPQ. Either storage may be nullptr.
/// cascStorage / mpqStorage are opaque pointers from openCascStorage / openMpqStorage.
std::wstring resolveTexturePathFull(
    const std::wstring& modelDir, const std::wstring& relPath,
    void* cascStorage,
    void* mpqStorage = nullptr);

// ── CASC (Reforged) ─────────────────────────────────────────────────────────

/// Open the Warcraft III Reforged CASC archive (returns opaque pointer).
/// Uses cascDir if non-empty, otherwise auto-detects via blizzard_game_finder.
/// Returns nullptr if CASC is not available or the game is not found.
/// Caller must call closeCascStorage() when done.
void* openCascStorage(const std::wstring& cascDir);

/// Close a CASC storage opened by openCascStorage().
void closeCascStorage(void* storage);

// ── MPQ (Classic v800) ──────────────────────────────────────────────────────
//
// Backed by WhiteoutLib's whiteout::storages::mpq::Storage. The implementation
// in mpq_texture_resolver.cpp pulls in whiteout_mpq transitively through the
// MDLXImporter -> WhiteoutDexTextureBrowser_native -> whiteout_mpq link chain.

/// Open all *.mpq archives found inside mpqDir and return an opaque handle.
/// Scans the directory non-recursively for files with the .mpq extension and
/// opens each one via mpq::Storage. Returns nullptr if no archives could be
/// opened or mpqDir is invalid. Caller must call closeMpqStorage() when done.
void* openMpqStorage(const std::wstring& mpqDir);

/// Close an MPQ storage opened by openMpqStorage().
void closeMpqStorage(void* storage);

/// Extract a texture from any of the open MPQ archives to the temp directory
/// and return its file path on disk. Returns an empty string if the file is
/// not present in any archive or extraction failed. Tries the relPath as-is
/// first, then with .blp / .dds / .tga extensions.
std::wstring extractTextureFromMPQ(
    void* mpqStorage,
    const std::wstring& modelDir,
    const std::wstring& relPath);

} // namespace mdx_scene
