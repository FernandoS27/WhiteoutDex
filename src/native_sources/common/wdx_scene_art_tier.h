#pragma once

// ============================================================================
// The scene's Warcraft III art tier.
//
// Warcraft III 3.0.0 ships three complete art sets behind the same logical
// path - `war3.w3mod:` (Classic), `_hd.w3mod:` (Reforged) and `_de.w3mod:`
// (Definitive) - and their atlases are not interchangeable: a Definitive model
// dressed in Reforged's copy of the same texture path comes out wearing the
// wrong unwrap. The tier says which overlay a bare archive path resolves
// through first, and it is a property of the model being authored, so it is
// saved with the scene rather than in an INI:
//
//   rootNode custom attribute "WhiteoutDexSceneData"  (WhiteoutDexGlobals.ms)
//     artTier : integer    0 Auto, 1 Classic, 2 Reforged, 3 Definitive
//
// Its own block rather than a parameter on the sequence CA, because the
// Sequence Manager full-replaces that one every time it saves.
//
// Written by the importer (the overlay the model came out of, else inferred
// from its MDX version and shaders), by the Settings dialog and by the
// preview's View menu - the last two edit the same value. Read by the
// importer's texture resolver, WdxExtractAsset and the preview. Auto, which is
// also what a scene saved before the attribute existed reads as, keeps every
// reader's historical order.
//
// Header-only: the importer and the renderer are separate plug-ins and both
// need the same parameter name, the same parse and the same prefix order. This
// half needs no Max SDK; reading the value off a scene is in
// wdx_scene_art_tier_max.h.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace wdx::scene {

// The stored integer. Classic..Definitive are one above
// whiteout::flakes::Wc3ArtTier, leaving 0 free to mean "not set".
enum class ArtTier : int {
    Auto = 0,
    Classic = 1,
    Reforged = 2,
    Definitive = 3,
};

inline constexpr const wchar_t* kArtTierParam = L"artTier";

inline ArtTier ArtTierFromStored(int v) {
    return (v >= static_cast<int>(ArtTier::Classic) && v <= static_cast<int>(ArtTier::Definitive))
               ? static_cast<ArtTier>(v)
               : ArtTier::Auto;
}

inline const char* ArtTierName(ArtTier tier) {
    switch (tier) {
    case ArtTier::Classic:
        return "Classic";
    case ArtTier::Reforged:
        return "Reforged";
    case ArtTier::Definitive:
        return "Definitive";
    default:
        return "Auto";
    }
}

// Which tier an `_XX.w3mod` overlay segment names ("_de.w3mod" or
// "war3.w3mod:_de.w3mod:units\..."). Auto for anything else, including
// `_deprecated.w3mod`, which is a bucket rather than a tier.
inline ArtTier ArtTierFromOverlay(std::string_view text) {
    auto contains = [&](std::string_view needle) {
        if (needle.size() > text.size())
            return false;
        for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size() && match; ++j) {
                char c = text[i + j];
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
                match = c == needle[j];
            }
            if (match)
                return true;
        }
        return false;
    };
    if (contains("_de.w3mod"))
        return ArtTier::Definitive;
    if (contains("_hd.w3mod"))
        return ArtTier::Reforged;
    return ArtTier::Auto;
}

// The tier an imported model implies when nothing better (an overlay in its
// path) says. The MDX version bounds what the model can be: a v800 file
// predates Reforged, and nothing before 3.0.0 wrote v1300 or later. Inside
// those bounds the shaders decide. A v1300+ HD model is called Definitive
// because 3.0.0 re-saved every model, `_hd` ones included, at v1800, so the
// file cannot tell the two apart - and Definitive's chain still reaches every
// path that only Reforged ships, while Reforged's misses the thousands only
// Definitive has.
inline ArtTier InferArtTier(uint32_t mdxVersion, bool hasHdLayers) {
    if (mdxVersion < 900 || !hasHdLayers)
        return ArtTier::Classic;
    return mdxVersion < 1300 ? ArtTier::Reforged : ArtTier::Definitive;
}

// The `war3.w3mod:` mod chains a bare path is tried under, first hit wins.
//
// Auto and Classic keep the order the toolkit always used. Definitive stays
// reachable from every tier, behind the older overlays, because 3.0.0 ships
// thousands of paths only it has and a path that exists nowhere else should
// still resolve; the deprecated bucket is last everywhere. Unlike
// WhiteoutFlakes' Wc3ModChain this has no bare "" entry - callers that also
// want the resolved namespace append it themselves.
inline std::span<const char* const> CascPrefixes(ArtTier tier) {
    static constexpr const char* kClassic[] = {
        "war3.w3mod:",
        "war3.w3mod:_hd.w3mod:",
        "war3.w3mod:_de.w3mod:",
        "war3.w3mod:_deprecated.w3mod:",
    };
    static constexpr const char* kReforged[] = {
        "war3.w3mod:_hd.w3mod:",
        "war3.w3mod:",
        "war3.w3mod:_de.w3mod:",
        "war3.w3mod:_deprecated.w3mod:",
    };
    static constexpr const char* kDefinitive[] = {
        "war3.w3mod:_de.w3mod:",
        "war3.w3mod:_hd.w3mod:",
        "war3.w3mod:",
        "war3.w3mod:_deprecated.w3mod:",
    };
    switch (tier) {
    case ArtTier::Reforged:
        return kReforged;
    case ArtTier::Definitive:
        return kDefinitive;
    default:
        return kClassic;
    }
}

} // namespace wdx::scene
