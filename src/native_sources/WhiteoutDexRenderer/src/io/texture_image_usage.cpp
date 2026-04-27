// ============================================================================
// texture_image_usage — implementation
//
// 1:1 port of Blizzard's `CImageFile::DetermineImageUsage` (Preview RE
// @0x7ff609bad260). Tested suffix list and directory fallbacks were
// recovered from the function's literal-string xrefs:
//   "_Diffuse"         → ImageUsage::Default   (sRGB)
//   "_Normal"          → ImageUsage::NormalMap (linear)
//   "_ORM"             → ImageUsage::ORM       (linear)
//   "_Emissive"        → ImageUsage::Emissive  (sRGB)
//   "_IBL"             → ImageUsage::IBL       (sRGB)
//   "Textures/Normal"  → ImageUsage::NormalMap (linear, fallback dir)
//   "Textures/ORM"     → ImageUsage::ORM       (linear, fallback dir)
// Default for unmatched names is `ImageUsage::Default` (sRGB).
// ============================================================================

#include "texture_image_usage.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace WhiteoutDex {

namespace {

// Normalise: backslash → forward, lowercase, strip the file extension.
// The engine `bcRemoveExtension` + `bcNormalizePath` chain in
// `DetermineImageUsage` produces equivalent output.
std::string NormaliseStem(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    // Drop trailing extension (last '.' after the last separator).
    const size_t lastSep = path.find_last_of("/\\");
    const size_t scanFrom = (lastSep == std::string_view::npos) ? 0u : lastSep + 1u;
    const size_t lastDot  = path.find_last_of('.');
    const size_t end      = (lastDot != std::string_view::npos && lastDot >= scanFrom)
                                ? lastDot : path.size();
    for (size_t i = 0; i < end; ++i) {
        char c = path[i];
        if (c == '\\') c = '/';
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool EndsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size()
        && std::equal(suffix.begin(), suffix.end(), s.end() - suffix.size());
}

bool Contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

} // namespace

ImageUsage DetermineImageUsage(std::string_view path) {
    const std::string stem = NormaliseStem(path);

    // Suffix matches first — these mirror the four equivalent
    // StringSuffixMatch calls in the engine, in the same priority
    // order ('_Diffuse' is checked but maps to Default so it's a
    // benign no-op against the catch-all return).
    if (EndsWith(stem, "_diffuse"))  return ImageUsage::Default;
    if (EndsWith(stem, "_normal"))   return ImageUsage::NormalMap;
    if (EndsWith(stem, "_orm"))      return ImageUsage::ORM;
    if (EndsWith(stem, "_emissive")) return ImageUsage::Emissive;
    if (EndsWith(stem, "_ibl"))      return ImageUsage::IBL;

    // Fallback: directory prefix check. Engine code does an
    // anywhere-in-string scan; we use Contains to match.
    if (Contains(stem, "textures/normal")) return ImageUsage::NormalMap;
    if (Contains(stem, "textures/orm"))    return ImageUsage::ORM;

    return ImageUsage::Default;
}

gfx::Format ApplySrgbPolicy(gfx::Format raw, ImageUsage usage) {
    const bool wantLinear = IsLinearImageUsage(usage);

    auto stripSrgb = [](gfx::Format f) {
        switch (f) {
            case gfx::Format::R8G8B8A8_UNORM_SRGB: return gfx::Format::R8G8B8A8_UNORM;
            case gfx::Format::BC1_UNORM_SRGB:      return gfx::Format::BC1_UNORM;
            case gfx::Format::BC2_UNORM_SRGB:      return gfx::Format::BC2_UNORM;
            case gfx::Format::BC3_UNORM_SRGB:      return gfx::Format::BC3_UNORM;
            case gfx::Format::BC7_UNORM_SRGB:      return gfx::Format::BC7_UNORM;
            default:                                return f;
        }
    };
    auto promoteSrgb = [](gfx::Format f) {
        switch (f) {
            case gfx::Format::R8G8B8A8_UNORM:      return gfx::Format::R8G8B8A8_UNORM_SRGB;
            case gfx::Format::BC1_UNORM:           return gfx::Format::BC1_UNORM_SRGB;
            case gfx::Format::BC2_UNORM:           return gfx::Format::BC2_UNORM_SRGB;
            case gfx::Format::BC3_UNORM:           return gfx::Format::BC3_UNORM_SRGB;
            case gfx::Format::BC7_UNORM:           return gfx::Format::BC7_UNORM_SRGB;
            default:                                return f;     // BC4/BC5/BC6H/float
        }
    };
    return wantLinear ? stripSrgb(raw) : promoteSrgb(raw);
}

} // namespace WhiteoutDex
