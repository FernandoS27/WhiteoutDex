#pragma once
// ============================================================================
// WhiteoutDex Renderer — IModelSource adapter helpers
//
// Free-function utilities shared by every IModelSource implementation
// (MdxModelAdapter, MaxSceneAdapter, future adapters). They take primitive
// inputs and return primitive outputs — no adapter state needed, so
// expressing them as free functions rather than IModelSource members keeps
// the interface surface minimal while still eliminating the cross-adapter
// duplication of these same patterns.
// ============================================================================

#include "model_types.h"

#include <whiteout/textures/blp/blp.h>
#include <whiteout/textures/dds/parser.h>
#include <whiteout/textures/tga/parser.h>
#include <whiteout/textures/png/parser.h>
#include <whiteout/textures/texture.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace WhiteoutDex {

// ============================================================================
// hasFlag<Enum> — templated bit-mask test matching the idiom
//   (bits & static_cast<uint32_t>(flag)) != 0
// used pervasively when reading MDX NodeFlag / Layer::ShadingFlag / etc.
// Constrained to enum types to avoid clashing with overloaded non-template
// `hasFlag` functions that live in other namespaces (e.g. gfx::hasFlag).
// ============================================================================
template <typename Enum, std::enable_if_t<std::is_enum_v<Enum>, int> = 0>
inline bool hasFlag(uint32_t bits, Enum flag) {
    return (bits & static_cast<uint32_t>(flag)) != 0;
}

// ============================================================================
// PackBillboardFlags — condense adapter-side billboard booleans into the
// renderer's BONE_BILLBOARD_* bitmask.
//
// One-hot priority Full > LockX > LockY > LockZ mirrors Previewd's
// GetObjectFlags @0x140456dd0: when an MDX file (or a user-property on a
// Max node) sets more than one Billboard* bit, the renderer still sees a
// single chosen axis. CameraAnchored is independent and stacks on top.
//
// Callers source the bools from different inputs (MDX NodeFlag bits vs.
// INode::GetUserPropInt lookups) but produce identical renderer output
// through this one helper.
// ============================================================================
inline uint32_t PackBillboardFlags(bool fullBb, bool lockX, bool lockY, bool lockZ,
                                   bool cameraAnchored) {
    uint32_t f = 0;
    if      (fullBb) f |= BONE_BILLBOARD_FULL;
    else if (lockX)  f |= BONE_BILLBOARD_LOCK_X;
    else if (lockY)  f |= BONE_BILLBOARD_LOCK_Y;
    else if (lockZ)  f |= BONE_BILLBOARD_LOCK_Z;
    if (cameraAnchored) f |= BONE_BILLBOARD_CAMERA_ANCHORED;
    return f;
}

// ============================================================================
// FillSolidRGBA — resize `out` to w*h*4 and write (r,g,b,a) into every
// texel. Used by every adapter's "no texture found" fallback path
// (magenta missing-texture placeholder, red team-color placeholder,
// team-color composite fallback, etc.).
// ============================================================================
inline void FillSolidRGBA(std::vector<uint8_t>& out, int w, int h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    out.resize(size_t(w) * size_t(h) * 4);
    for (size_t j = 0; j < out.size(); j += 4) {
        out[j] = r; out[j + 1] = g; out[j + 2] = b; out[j + 3] = a;
    }
}

// ============================================================================
// ExtensionLower — lowercased file extension (with leading dot). Returns an
// empty string when the path has no extension. Drives the parser dispatch.
// ============================================================================
inline std::string ExtensionLower(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

// ============================================================================
// DispatchTextureParser — invoke `parse` with the WhiteoutLib parser
// matching `ext`. `parse` receives the parser by reference (e.g.
// `blp::Parser&`) and must return `std::optional<Texture>`. Returns
// nullopt for unknown extensions.
//
// Usage:
//   DispatchTextureParser(ext, [&](auto& p) { return p.parse(path); });
//   DispatchTextureParser(ext, [&](auto& p) { return p.parse(buffer); });
// ============================================================================
template <typename ParseFn>
inline std::optional<whiteout::textures::Texture> DispatchTextureParser(
    const std::string& ext, ParseFn parse) {
    if (ext == ".blp") { whiteout::textures::blp::Parser p; return parse(p); }
    if (ext == ".dds") { whiteout::textures::dds::Parser p; return parse(p); }
    if (ext == ".tga") { whiteout::textures::tga::Parser p; return parse(p); }
    if (ext == ".png") { whiteout::textures::png::Parser p; return parse(p); }
    return std::nullopt;
}

// ============================================================================
// DecodeToRGBA8 — decode an in-memory texture buffer via DispatchTextureParser
// and normalise to RGBA8. Writes `out` / `w` / `h` on success and returns
// true; returns false on unknown extensions or parse failures (outputs left
// untouched).
// ============================================================================
inline bool DecodeToRGBA8(std::span<const uint8_t> buf, const std::string& ext,
                          std::vector<uint8_t>& out, int& w, int& h) {
    auto result = DispatchTextureParser(ext,
        [&](auto& parser) { return parser.parse(buf); });
    if (!result) return false;
    result->format(whiteout::textures::PixelFormat::RGBA8);
    w = (int)result->width();
    h = (int)result->height();
    auto mip0 = result->mipData(0);
    out.assign(mip0.begin(), mip0.end());
    return true;
}

} // namespace WhiteoutDex
