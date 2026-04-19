#pragma once
// ============================================================================
// HD pipeline IBL env probes.
//
// The HD PS samples two cubemap arrays at t13/t14 (from/to, blended via
// envTransitionT). The real engine loads two DDS probes via
// EnvironmentMapCreate at Previewd 0x140166f61:
//   Environment/EnvironmentMap/LordaeronSummer/Day_IBL.dds
//   Environment/EnvironmentMap/LordaeronSummer/Night_IBL.dds
// `LoadEnvProbe` reads a single DDS through IContentProvider, decodes via
// WhiteoutLib's DDS parser (handles BC1-BC7/BC6H and uncompressed), and
// uploads it as a TextureCubeArray (NumCubes = DDS arraySize).
//
// `CreateDefaultEnvProbe` stays around as the fallback when the game-data
// DDS isn't locatable (no Wc3 install, explicit path override, etc.) --
// it's a small procedural dim-grey cubemap that keeps the HAS_IBL permute
// sampling something valid instead of reading uninitialised memory.
// ============================================================================

#include "../../gfx/gfx.h"

#include <cstdint>
#include <string>

namespace WhiteoutDex { class IContentProvider; }

namespace WhiteoutDex::ibl {

// Canonical engine paths for the Lordaeron-Summer environment maps. The
// HD pipeline loads these at init and binds day to t13, night to t14.
inline constexpr const char* kDayIblPath   =
    "Environment/EnvironmentMap/LordaeronSummer/Day_IBL.dds";
inline constexpr const char* kNightIblPath =
    "Environment/EnvironmentMap/LordaeronSummer/Night_IBL.dds";

// Portrait-tailored IBL probe — ships in war3.w3mod for the unit
// portrait UI. Soft sky-over-grass environment with no strong sun or
// directional features, authored specifically for close-range
// single-model rendering. No horizontal asymmetry → no view-centred
// reflection seam on low-roughness surfaces. Preferred probe for the
// model viewer (over LordaeronSummer/Day_IBL which has a captured sun
// that shows up as a seam at preview distances).
inline constexpr const char* kPortraitIblPath =
    "Environment/EnvironmentMap/Portraits/PortraitDefault_IBL.dds";

// Result of a successful probe load. `mipCount` lets the renderer set
// envFromMipEnd / envToMipEnd to (mipCount - 1) so the PS's roughness ->
// mip remap clamps correctly; a non-zero product of the two endMips also
// disables sampleIBL's fast-out path.
struct LoadedEnvProbe {
    gfx::TextureHandle handle   = gfx::TextureHandle::Invalid;
    int                mipCount = 0;
};

// Load a DDS cube or cube-array via the content provider, decode through
// WhiteoutLib, and upload as TextureCubeArray (NumCubes = arraySize).
// Returns { Invalid, 0 } on any failure (missing file, unsupported format,
// non-cube DDS, upload error) -- caller should fall back to
// CreateDefaultEnvProbe.
LoadedEnvProbe LoadEnvProbe(gfx::IGFXDevice&       gfx,
                            const IContentProvider& content,
                            const std::string&      relPath);

// Direct-from-disk loader for diagnostic / local test DDS files. Reads
// `absPath` via std::ifstream (no content provider), runs it through the
// same DDS parser + upload path as LoadEnvProbe. Returns {Invalid, 0}
// on any failure.
//
// `applyBlizzardFaceRemap` controls whether the Blizzard→D3D face swap
// (-X ↔ -Z) is applied. Real game DDS files need it (their on-disk
// storage puts -X and -Z in non-standard slots); DDS files we authored
// ourselves in standard D3D order (e.g. the debug-faces round-trip) do
// NOT — with the remap applied, our known-good layer 1 and layer 5 get
// swapped in the cube SRV, which is exactly the kind of off-by-one
// corruption we're trying to isolate from actual pipeline errors.
LoadedEnvProbe LoadEnvProbeFromFile(gfx::IGFXDevice&   gfx,
                                     const std::string& absPath,
                                     bool applyBlizzardFaceRemap = true);

// ---------------------------------------------------------------------------
// Procedural fallback -- small neutral-grey cubemap used when no DDS is
// available. Size/mips mirror what a typical in-game probe provides.
// ---------------------------------------------------------------------------
constexpr int kEnvProbeSize       = 16;   // per-face width/height; small is fine
constexpr int kEnvProbeMipLevels  = 5;    // log2(16) + 1

gfx::TextureHandle CreateDefaultEnvProbe(gfx::IGFXDevice& gfx);

// Diagnostic — write a debug cube-array DDS file with each of the 6
// faces filled with a distinct solid colour (standard RGB+YCM palette,
// same as CreateDebugFacesEnvProbe). Two cubes (diffuse + specular
// slices) share the palette. Use this to round-trip through the full
// pipeline (DDS writer → file → DDS parser → CreateTexture → sampled
// by shader) and see which DDS layer ends up at which cube-face axis.
// Returns true on success; writes to `outPath` (absolute filesystem
// path; content provider is NOT used).
bool WriteDebugFacesDds(const std::string& outPath);

// Debug probe — 2-cube array (diffuse + specular slices) where every
// face is filled with a distinct solid colour:
//   +X red    -X green    +Y blue    -Y yellow    +Z cyan    -Z magenta
// Both slices share the same palette so specular reflections and diffuse
// irradiance pick the same colour per direction. With this bound at
// t13/t14 you can read off WHICH cube face is projected onto any
// given fragment — invaluable for debugging cube swizzle / face-select
// conventions.
gfx::TextureHandle CreateDebugFacesEnvProbe(gfx::IGFXDevice& gfx);

// Studio probe — neutral symmetric IBL for a model viewer. Unlike the
// game's Day_IBL probe which has strong scene-direction asymmetry
// (creates a view-centered reflection split on low-roughness surfaces),
// this is horizontally isotropic: all four side faces identical,
// distinct top (bright) / bottom (dim) for a natural vertical gradient.
// Layout:
//   slice 0 (diffuse irradiance) — uniform warm mid-grey everywhere,
//           approximating the cos-weighted hemisphere integral of a
//           studio HDR's overall average.
//   slice 1 (specular prefilter) — directionally varied:
//     +Y face: bright cool-white  (key/softbox overhead)
//     -Y face: dim warm dark-grey (ground bounce)
//     ±X/±Z:   medium neutral grey (ambient surround)
//   All mips identical per face (uniform colour needs no filtering).
gfx::TextureHandle CreateStudioEnvProbe(gfx::IGFXDevice& gfx);

inline float DefaultEnvProbeEndMip() {
    return static_cast<float>(kEnvProbeMipLevels - 1);
}

} // namespace WhiteoutDex::ibl
