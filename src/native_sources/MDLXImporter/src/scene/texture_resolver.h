// MDLXImporter — Shared texture path resolution
#pragma once

#include <string>

namespace mdx_scene {

/// Resolve an MDX-relative texture path to an actual file on disk.
/// Searches modelDir + relative path, then flat (filename only next to model),
/// trying alternative extensions (.dds, .blp, .tga, .png, .tif).
std::wstring resolveTexturePath(const std::wstring& modelDir, const std::wstring& relPath);

/// Resolve texture path, falling back to CASC extraction if enabled.
/// cascStorage is an opaque pointer to whiteout::storages::casc::Storage (or nullptr).
std::wstring resolveTexturePathFull(
    const std::wstring& modelDir, const std::wstring& relPath,
    void* cascStorage);

/// Open the Warcraft III Reforged CASC archive (returns opaque pointer).
/// Uses cascDir if non-empty, otherwise auto-detects via blizzard_game_finder.
/// Returns nullptr if CASC is not available or the game is not found.
/// Caller must call closeCascStorage() when done.
void* openCascStorage(const std::wstring& cascDir);

/// Close a CASC storage opened by openCascStorage().
void closeCascStorage(void* storage);

} // namespace mdx_scene
