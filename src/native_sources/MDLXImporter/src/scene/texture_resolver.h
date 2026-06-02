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
//      against `war3.w3mod:` and `war3.w3mod:_hd.w3mod:` prefixes.
//   3. MPQ (Classic v800): MDX-stated extension first, then aliases,
//      against every .mpq archive opened from `mpqDir`.
//
// When the archive copy of a file uses a different extension than the
// MDX (e.g. MDX says `.tif`, CASC has `.dds`), the bytes are extracted
// to `<modelDir>/<original-subdir>/<stem>.<actualExt>` and the returned
// path uses the actual extension — so 3ds Max's BitmapTex points at a
// real file in a format Max can read.
#pragma once

#include <memory>
#include <string>

namespace mdx_scene {

class TextureResolver {
public:
    /// Construct + eagerly open CASC + MPQ storages. Empty `cascDir`
    /// triggers blizzard_game_finder auto-detection; empty `mpqDir`
    /// disables MPQ entirely. `searchCASC` / `searchMPQ` are master
    /// switches that override the dirs when false.
    TextureResolver(const std::wstring& modelDir,
                    const std::wstring& cascDir,
                    const std::wstring& mpqDir,
                    bool searchCASC,
                    bool searchMPQ);
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
std::wstring resolveTexturePath(const std::wstring& modelDir,
                                const std::wstring& relPath);

} // namespace mdx_scene
