#pragma once
// ============================================================================
// Texture image-usage policy — port of Blizzard's
// `CImageFile::DetermineImageUsage` @ Preview 0x7ff609bad260.
//
// Engine logic: filename-suffix and (fallback) directory-prefix matching
// decide whether a texture is sampled as sRGB or linear, regardless of
// the on-disk DXGI format. The runtime SRV view applies an `_SRGB`
// variant for color content and stays linear for normal / ORM data.
//
// The rule in `CreateImageTexture` @0x7ff609b04f30 is:
//     sRGBBit = ((imageUsage - 1) > 1) ? 0x80 : 0;
// i.e. imageUsage 1 (NormalMap) and 2 (ORM) get linear sampling;
// imageUsage 0 (Default), 3 (Emissive), 4 (IBL) — and any other —
// get sRGB sampling.
//
// We mirror this identically so a path that the engine would treat as
// linear stays linear here, and a path the engine treats as sRGB
// reaches our HD/SD shaders pre-decoded just like in the game.
// ============================================================================

#include "gfx/gfx_types.h"

#include <string_view>

namespace WhiteoutDex {

// Engine ImageUsage enum (recovered from `DetermineImageUsage`).
//   0 — Default        sRGB
//   1 — NormalMap      linear
//   2 — ORM            linear
//   3 — Emissive       sRGB
//   4 — IBL            sRGB
enum class ImageUsage : int {
    Default   = 0,
    NormalMap = 1,
    ORM       = 2,
    Emissive  = 3,
    IBL       = 4,
};

// Match Blizzard's filename-suffix / directory-prefix rule.
// Path is taken case-insensitively, with backslashes normalised to
// forward slashes.
ImageUsage DetermineImageUsage(std::string_view path);

// True when the engine samples this slot through a non-sRGB SRV.
// Mirrors `(imageUsage - 1) > 1` from `CreateImageTexture`.
inline bool IsLinearImageUsage(ImageUsage u) {
    return u == ImageUsage::NormalMap || u == ImageUsage::ORM;
}

// Apply the sRGB / linear policy on top of a `gfx::Format` produced by
// the file parser (which only knows the on-disk DXGI flag). Promotes
// to the matching `_SRGB`-suffixed format when the engine would sample
// this asset as sRGB; demotes back to linear when the policy says so.
// Formats with no sRGB variant (BC4/BC5/BC6H/float/single-channel) are
// returned unchanged.
gfx::Format ApplySrgbPolicy(gfx::Format raw, ImageUsage usage);

// Convenience: combine path lookup + format remap in one call.
inline gfx::Format ApplyTextureSrgbPolicy(gfx::Format raw, std::string_view path) {
    return ApplySrgbPolicy(raw, DetermineImageUsage(path));
}

} // namespace WhiteoutDex
