// MDLXImporter — Unified texture/model path resolver.
//
// Replaces the older void*-passing `openCascStorage` / `openMpqStorage` +
// extractTextureFromCASC / extractTextureFromMPQ split with one
// resolver object that owns both archive handles for an import session.
//
// Resolution order (first match wins):
//   1. Local disk under `modelDir`, peeling subdirectory levels and
//      trying every extension alias at each tier.
//   2. CASC (Reforged): MDX-stated extension first, then aliases,
//      against the `war3.w3mod:`, `_hd.w3mod:`, `_de.w3mod:` and
//      `_deprecated.w3mod:` prefixes, in the order the scene's art tier
//      gives them (wdx_scene_art_tier.h).
//   3. MPQ (Classic v800): MDX-stated extension first, then aliases,
//      against every .mpq archive opened from `mpqDir`.
//
// When the archive copy of a file uses a different extension than the
// MDX (e.g. MDX says `.tif`, CASC has `.dds`), the bytes are extracted
// to `<modelDir>/<original-subdir>/<stem>.<actualExt>` and the returned
// path uses the actual extension — so 3ds Max's BitmapTex points at a
// real file in a format Max can read.
#pragma once

#include "wdx_scene_art_tier.h"

#include <memory>
#include <string>
#include <vector>

namespace mdx_scene {

class TextureResolver {
public:
    /// Construct + eagerly open CASC + MPQ storages. Empty `cascDir`
    /// triggers blizzard_game_finder auto-detection; empty `mpqDir`
    /// disables MPQ entirely. No on/off flags — both backends are always
    /// attempted when a path is available; failures land in the log.
    ///
    /// `mpqArchives` is the user's configured load order (absolute paths,
    /// highest priority first). When non-empty it is opened INSTEAD of
    /// scanning `mpqDir`: the order is the whole point, and a directory scan
    /// has none. Empty falls back to the scan.
    ///
    /// `artTier` orders the CASC overlays a bare texture path is tried under.
    TextureResolver(const std::wstring& modelDir,
                    const std::wstring& cascDir,
                    const std::wstring& mpqDir,
                    const std::vector<std::wstring>& mpqArchives,
                    wdx::scene::ArtTier artTier,
                    bool mpqFirst = false);
    ~TextureResolver();

    TextureResolver(const TextureResolver&) = delete;
    TextureResolver& operator=(const TextureResolver&) = delete;

    /// Resolve `relPath` (MDX-relative, forward- or backslash-separated,
    /// any case) to an absolute disk path. On a true miss returns the
    /// best-guess fallback path under `modelDir` so callers can still
    /// SetMapName with a string the artist can hand-edit.
    std::wstring Resolve(const std::wstring& relPath);

    bool HasCasc() const;
    bool HasMpq() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Local-disk-only probe with subdirectory peeling + extension aliases.
/// Exposed as a free function so callers that don't have a TextureResolver
/// handy (the legacy buildStdFallback path, the IFL writer's filename
/// reuse) can still benefit from the alias logic.
/// Which extensions a disk probe tries: the MDX-stated one, the aliases, or
/// both (MDX-stated first at each folder level).
enum class ExtProbe { Exact, Aliases, Both };
std::wstring resolveTexturePath(const std::wstring& modelDir,
                                const std::wstring& relPath,
                                ExtProbe probe = ExtProbe::Both);

/// Where an archive path with a mod chain ("_hd.w3mod:textures\x.blp") is
/// placed on disk: the overlay becomes a folder ("_hd.w3mod\textures\x.blp"),
/// `war3.w3mod:` is dropped. Paths without ':' come back unchanged.
std::wstring diskPathFromArchivePath(const std::wstring& relPath);

/// The `_XX.w3mod` overlay segment (lowercase, e.g. "_de.w3mod") the model
/// browser recorded in a model's extraction directory, or empty when the model
/// did not come out of CASC that way.
std::string detectModChain(const std::wstring& modelDir);

} // namespace mdx_scene
