#include "env_probe.h"

#include "../../io/content_provider.h"

#include <whiteout/textures/dds/parser.h>
#include <whiteout/textures/dds/writer.h>
#include <whiteout/textures/texture.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#if defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* s);
#else
inline void OutputDebugStringA(const char*) {}
#endif

namespace WhiteoutDex::ibl {

namespace {

void DbgLogf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

const char* TypeName(whiteout::textures::TextureType t) {
    using T = whiteout::textures::TextureType;
    switch (t) {
        case T::Texture2D:        return "Texture2D";
        case T::Texture3D:        return "Texture3D";
        case T::TextureCube:      return "TextureCube";
        case T::Texture2DArray:   return "Texture2DArray";
        case T::TextureCubeArray: return "TextureCubeArray";
    }
    return "???";
}

const char* FormatName(whiteout::textures::PixelFormat f) {
    using F = whiteout::textures::PixelFormat;
    switch (f) {
        case F::R8:      return "R8";
        case F::R16:     return "R16";
        case F::R32F:    return "R32F";
        case F::RG8:     return "RG8";
        case F::RG16:    return "RG16";
        case F::RG32F:   return "RG32F";
        case F::RGBA8:   return "RGBA8";
        case F::RGBA16:  return "RGBA16";
        case F::RGBA32F: return "RGBA32F";
        case F::BC1:     return "BC1";
        case F::BC2:     return "BC2";
        case F::BC3:     return "BC3";
        case F::BC4:     return "BC4";
        case F::BC5:     return "BC5";
        case F::BC6H:    return "BC6H";
        case F::BC7:     return "BC7";
    }
    return "???";
}

} // namespace

// ---------------------------------------------------------------------------
// Real DDS loader
// ---------------------------------------------------------------------------
//
// The HD PS samples the probe as TextureCubeArray<float4>. Our GFX backend
// only exposes R8G8B8A8_UNORM for shader resources, so HDR formats (BC6H,
// R16G16B16A16_FLOAT, R32G32B32A32_FLOAT) are range-clamped during the
// `copyAsFormat(RGBA8)` conversion. Good enough for pre-integrated diffuse
// probes; the specular side loses some punch but shader routing is
// preserved.
namespace {
// Shared DDS-bytes → TextureCubeArray upload. Called by both
// LoadEnvProbe (content-provider path) and LoadEnvProbeFromFile
// (direct-from-disk path).
LoadedEnvProbe LoadEnvProbeFromBytes(gfx::IGFXDevice& gfx,
                                      std::span<const uint8_t> bytes,
                                      const char* sourceLabel,
                                      bool applyBlizzardFaceRemap);
}  // namespace

LoadedEnvProbe LoadEnvProbe(gfx::IGFXDevice&       gfx,
                            const IContentProvider& content,
                            const std::string&      relPath) {
    LoadedEnvProbe failed{};

    auto bytes = content.ReadFile(relPath);
    if (!bytes) {
        DbgLogf("[WDEX IBL] ReadFile FAILED for %s\n", relPath.c_str());
        return failed;
    }
    DbgLogf("[WDEX IBL] ReadFile OK for %s (%zu bytes)\n",
            relPath.c_str(), bytes->size());

    return LoadEnvProbeFromBytes(
        gfx, std::span<const uint8_t>(bytes->data(), bytes->size()),
        relPath.c_str(), /*applyBlizzardFaceRemap=*/true);
}

LoadedEnvProbe LoadEnvProbeFromFile(gfx::IGFXDevice& gfx,
                                     const std::string& absPath,
                                     bool applyBlizzardFaceRemap) {
    LoadedEnvProbe failed{};
    std::ifstream f(absPath, std::ios::binary | std::ios::ate);
    if (!f) {
        DbgLogf("[WDEX IBL] LoadEnvProbeFromFile open FAILED %s\n", absPath.c_str());
        return failed;
    }
    const std::streamsize size = f.tellg();
    if (size <= 0) {
        DbgLogf("[WDEX IBL] LoadEnvProbeFromFile empty file %s\n", absPath.c_str());
        return failed;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!f.read(reinterpret_cast<char*>(buf.data()), size)) {
        DbgLogf("[WDEX IBL] LoadEnvProbeFromFile read FAILED %s\n", absPath.c_str());
        return failed;
    }
    DbgLogf("[WDEX IBL] LoadEnvProbeFromFile OK %s (%zd bytes)\n",
            absPath.c_str(), static_cast<ptrdiff_t>(size));
    return LoadEnvProbeFromBytes(
        gfx, std::span<const uint8_t>(buf.data(), buf.size()), absPath.c_str(),
        applyBlizzardFaceRemap);
}

namespace {
LoadedEnvProbe LoadEnvProbeFromBytes(gfx::IGFXDevice& gfx,
                                      std::span<const uint8_t> bytes,
                                      const char* sourceLabel,
                                      bool applyBlizzardFaceRemap) {
    LoadedEnvProbe failed{};
    whiteout::textures::dds::Parser parser(
        whiteout::textures::dds::Parser::ParseMode::Lenient);
    auto parsedOpt = parser.parse(bytes);
    if (!parsedOpt) {
        DbgLogf("[WDEX IBL] DDS parse FAILED\n");
        if (parser.hasIssues()) {
            for (const auto& issue : parser.getIssues()) {
                DbgLogf("[WDEX IBL]   issue: %s\n", issue.c_str());
            }
        }
        return failed;
    }
    whiteout::textures::Texture tex = std::move(*parsedOpt);

    DbgLogf("[WDEX IBL] parsed: type=%s fmt=%s %ux%u arraySize=%u layerCount=%u mipCount=%u srgb=%d\n",
            TypeName(tex.type()), FormatName(tex.format()),
            tex.width(), tex.height(), tex.arraySize(), tex.layerCount(),
            tex.mipCount(), tex.isSrgb() ? 1 : 0);

    // Must be cube or cube-array. Non-cube DDS would bind to a
    // TextureCubeArray SRV with a dimension mismatch -> GPU hang, same as
    // the TDR we fought in phase 6. Reject up-front.
    const auto type = tex.type();
    if (type != whiteout::textures::TextureType::TextureCube &&
        type != whiteout::textures::TextureType::TextureCubeArray) {
        DbgLogf("[WDEX IBL] rejecting: not cube or cube-array\n");
        return failed;
    }

    // Translate the WhiteoutLib pixel format into a gfx::Format so we
    // can upload in the source format directly (BC1/BC6H probes stay
    // block-compressed, uncompressed probes stay uncompressed). Decoding
    // to RGBA8 on CPU is pure waste + potential precision loss.
    auto mapFmt = [](whiteout::textures::PixelFormat pf, bool srgb) -> gfx::Format {
        using PF = whiteout::textures::PixelFormat;
        switch (pf) {
            case PF::R8:      return gfx::Format::R8_UNORM;
            case PF::R16:     return gfx::Format::R16_UNORM;
            case PF::R32F:    return gfx::Format::R32_FLOAT;
            case PF::RG8:     return gfx::Format::R8G8_UNORM;
            case PF::RG16:    return gfx::Format::R16G16_UNORM;
            case PF::RG32F:   return gfx::Format::R32G32_FLOAT;
            case PF::RGBA8:   return srgb ? gfx::Format::R8G8B8A8_UNORM_SRGB
                                          : gfx::Format::R8G8B8A8_UNORM;
            case PF::RGBA16:  return gfx::Format::R16G16B16A16_UNORM;
            case PF::RGBA32F: return gfx::Format::R32G32B32A32_FLOAT;
            case PF::BC1:     return srgb ? gfx::Format::BC1_UNORM_SRGB : gfx::Format::BC1_UNORM;
            case PF::BC2:     return srgb ? gfx::Format::BC2_UNORM_SRGB : gfx::Format::BC2_UNORM;
            case PF::BC3:     return srgb ? gfx::Format::BC3_UNORM_SRGB : gfx::Format::BC3_UNORM;
            case PF::BC4:     return gfx::Format::BC4_UNORM;
            case PF::BC5:     return gfx::Format::BC5_UNORM;
            case PF::BC6H:    return gfx::Format::BC6H_UF16;
            case PF::BC7:     return srgb ? gfx::Format::BC7_UNORM_SRGB : gfx::Format::BC7_UNORM;
        }
        return gfx::Format::Unknown;
    };
    gfx::Format gpuFormat = mapFmt(tex.format(), tex.isSrgb());
    if (gpuFormat == gfx::Format::Unknown) {
        // Unsupported source format -- fall back to RGBA8 decode. Normal
        // maps should never hit this path (BC3/BC5/BC7 are all covered).
        DbgLogf("[WDEX IBL] no direct gfx mapping for source fmt -- decoding to RGBA8\n");
        tex.format(whiteout::textures::PixelFormat::RGBA8);
        if (tex.format() != whiteout::textures::PixelFormat::RGBA8) {
            DbgLogf("[WDEX IBL] fallback decode FAILED\n");
            return failed;
        }
        gpuFormat = tex.isSrgb() ? gfx::Format::R8G8B8A8_UNORM_SRGB
                                 : gfx::Format::R8G8B8A8_UNORM;
    }

    const uint32_t mipCount  = tex.mipCount();
    const uint32_t cubeCount = tex.arraySize();  // Number of cubes (layer=6*this)
    const uint32_t layers    = tex.layerCount(); // Flattened 2D slice count
    if (mipCount == 0 || cubeCount == 0 || layers == 0) {
        DbgLogf("[WDEX IBL] rejecting: zero mip/cube/layer count\n");
        return failed;
    }
    if (layers != 6u * cubeCount) {
        DbgLogf("[WDEX IBL] rejecting: layers (%u) != 6*cubes (%u)\n",
                layers, 6u * cubeCount);
        return failed;
    }

    // D3D12 subresource order for Texture2DArray / cube-array is
    // mipSlice + arraySlice * mipCount -- array-major / mip-minor. Our GFX
    // backend expects initialPixels packed the same way. WhiteoutLib
    // exposes `mipData(mip, layer)` where `layer` flattens to the same
    // outer index, so we walk layers * mips in that order. Size math
    // uses whatever per-subresource byte count the Texture reports, so
    // block-compressed probes (BC1/BC6H) and uncompressed probes share
    // the same packing loop.
    const uint32_t faceSize = tex.width();
    uint64_t totalBytes = 0;
    for (uint32_t layer = 0; layer < layers; ++layer) {
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            totalBytes += tex.mipData(mip, layer).size();
        }
    }

    // WC3 cubemaps store the +X face at DDS layer 4 and the +Z face at
    // DDS layer 0 — i.e. DDS layers 0 and 4 are swapped relative to the
    // D3D / DDS-spec convention `{ +X, -X, +Y, -Y, +Z, -Z }`. Other
    // faces sit where D3D expects them. Remap only those two on upload
    // so a cube sample in world +X direction pulls the right content.
    //
    // Mapping (destination D3D layer ← source DDS layer):
    //     D3D +X (0) ← src 4   ← Blizzard stored +X here
    //     D3D -X (1) ← src 1
    //     D3D +Y (2) ← src 2
    //     D3D -Y (3) ← src 3
    //     D3D +Z (4) ← src 0   ← Blizzard stored +Z here
    //     D3D -Z (5) ← src 5
    static constexpr uint32_t kBlzToD3dFaceRemap[6] = { 0, 5, 2, 3, 4, 1 };
    static constexpr uint32_t kIdentityFaceMap[6]   = { 0, 1, 2, 3, 4, 5 };
    const uint32_t* faceMap = applyBlizzardFaceRemap ? kBlzToD3dFaceRemap
                                                     : kIdentityFaceMap;

    std::vector<uint8_t> packed(static_cast<size_t>(totalBytes));
    uint8_t* cursor = packed.data();
    for (uint32_t layer = 0; layer < layers; ++layer) {
        const uint32_t cubeIdx = layer / 6u;
        const uint32_t dstFace = layer % 6u;
        const uint32_t srcLayer = cubeIdx * 6u + faceMap[dstFace];
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            auto src = tex.mipData(mip, srcLayer);
            std::memcpy(cursor, src.data(), src.size());
            cursor += src.size();
        }
    }

    gfx::TextureDesc desc;
    desc.width     = static_cast<int>(faceSize);
    desc.height    = static_cast<int>(faceSize);
    desc.mipLevels = static_cast<int>(mipCount);
    desc.arraySize = static_cast<int>(layers);  // 6 * numCubes
    desc.isCube    = true;
    desc.format    = gpuFormat;
    desc.usage     = gfx::TextureUsage::ShaderResource;

    DbgLogf("[WDEX IBL] uploading cube-array: %dx%d arraySize=%d mips=%d totalBytes=%llu\n",
            desc.width, desc.height, desc.arraySize, desc.mipLevels,
            static_cast<unsigned long long>(totalBytes));
    gfx::TextureHandle handle = gfx.CreateTexture(desc, packed.data());
    if (handle == gfx::TextureHandle::Invalid) {
        DbgLogf("[WDEX IBL] CreateTexture FAILED\n");
        return failed;
    }
    DbgLogf("[WDEX IBL] CreateTexture OK\n");
    return { handle, static_cast<int>(mipCount) };
}
}  // namespace (LoadEnvProbeFromBytes)

gfx::TextureHandle CreateDefaultEnvProbe(gfx::IGFXDevice& gfx) {
    // Pack all 6 faces x kEnvProbeMipLevels mips contiguously in
    // array-major / mip-minor order (matches D3D12 GetCopyableFootprints
    // subresource numbering and D3D11 CreateTexture2D subresource array).
    //
    // Each texel = 0x80 grey, full alpha. The neutral value approximates
    // a dim overcast-sky probe; keeps HD materials from looking flat-black
    // when there's no real scene probe.
    size_t totalBytes = 0;
    for (int mip = 0; mip < kEnvProbeMipLevels; ++mip) {
        int w = std::max(1, kEnvProbeSize >> mip);
        int h = std::max(1, kEnvProbeSize >> mip);
        totalBytes += static_cast<size_t>(w) * h * 4;
    }
    totalBytes *= 6; // six faces

    // Dim neutral-grey fill. The HD/SD_on_HD IBL body treats probe samples
    // as linear irradiance values (no sRGB decode), so 0x80 (= 0.5 linear)
    // reads as a very bright ambient fill -- combined with our baseline
    // light it pushes `ambientResult * albedo + specular` well past 1.0
    // and the UNORM target clamps to saturated white. Use a darker grey
    // (~0.08 linear) so the probe acts as subtle fill rather than a
    // dominant light source, leaving the direct Cook-Torrance lobe to
    // shape the final look. Real probes baked from a scene will replace
    // this whenever we wire scene-probe authoring.
    constexpr uint8_t kNeutralGrey = 0x14;  // ~0.08 linear
    std::vector<uint8_t> pixels(totalBytes, 0);
    uint8_t* cursor = pixels.data();
    for (int face = 0; face < 6; ++face) {
        for (int mip = 0; mip < kEnvProbeMipLevels; ++mip) {
            int w = std::max(1, kEnvProbeSize >> mip);
            int h = std::max(1, kEnvProbeSize >> mip);
            for (int i = 0; i < w * h; ++i) {
                cursor[i * 4 + 0] = kNeutralGrey;
                cursor[i * 4 + 1] = kNeutralGrey;
                cursor[i * 4 + 2] = kNeutralGrey;
                cursor[i * 4 + 3] = 0xFF;
            }
            cursor += static_cast<size_t>(w) * h * 4;
        }
    }

    gfx::TextureDesc desc;
    desc.width     = kEnvProbeSize;
    desc.height    = kEnvProbeSize;
    desc.mipLevels = kEnvProbeMipLevels;
    desc.arraySize = 6;
    desc.isCube    = true;
    desc.format    = gfx::Format::R8G8B8A8_UNORM;
    desc.usage     = gfx::TextureUsage::ShaderResource;
    return gfx.CreateTexture(desc, pixels.data());
}

gfx::TextureHandle CreateDebugFacesEnvProbe(gfx::IGFXDevice& gfx) {
    // Six distinct solid colours, one per cube face. Matches the classic
    // RGB+YCM debug cube every graphics programmer has seen:
    //   +X red    -X green    +Y blue    -Y yellow    +Z cyan    -Z magenta
    // Two cubes (slice 0 = diffuse, slice 1 = specular) share the palette
    // so whatever direction the shader samples from, the output colour
    // tells you which cube face it hit regardless of diffuse-vs-specular
    // path.
    constexpr uint8_t kFaceColors[6][3] = {
        { 0xFF, 0x00, 0x00 },  // +X RED
        { 0x00, 0xFF, 0x00 },  // -X GREEN
        { 0x00, 0x00, 0xFF },  // +Y BLUE
        { 0xFF, 0xFF, 0x00 },  // -Y YELLOW
        { 0x00, 0xFF, 0xFF },  // +Z CYAN
        { 0xFF, 0x00, 0xFF },  // -Z MAGENTA
    };

    size_t bytesPerFace = 0;
    for (int mip = 0; mip < kEnvProbeMipLevels; ++mip) {
        int w = std::max(1, kEnvProbeSize >> mip);
        int h = std::max(1, kEnvProbeSize >> mip);
        bytesPerFace += static_cast<size_t>(w) * h * 4;
    }
    const int kNumCubes = 2; // diffuse + specular
    const int kNumLayers = 6 * kNumCubes;
    size_t totalBytes = bytesPerFace * static_cast<size_t>(kNumLayers);

    std::vector<uint8_t> pixels(totalBytes, 0);
    uint8_t* cursor = pixels.data();
    for (int layer = 0; layer < kNumLayers; ++layer) {
        const int face = layer % 6;
        const uint8_t r = kFaceColors[face][0];
        const uint8_t g = kFaceColors[face][1];
        const uint8_t b = kFaceColors[face][2];
        for (int mip = 0; mip < kEnvProbeMipLevels; ++mip) {
            int w = std::max(1, kEnvProbeSize >> mip);
            int h = std::max(1, kEnvProbeSize >> mip);
            for (int i = 0; i < w * h; ++i) {
                cursor[i * 4 + 0] = r;
                cursor[i * 4 + 1] = g;
                cursor[i * 4 + 2] = b;
                cursor[i * 4 + 3] = 0xFF;
            }
            cursor += static_cast<size_t>(w) * h * 4;
        }
    }

    gfx::TextureDesc desc;
    desc.width     = kEnvProbeSize;
    desc.height    = kEnvProbeSize;
    desc.mipLevels = kEnvProbeMipLevels;
    desc.arraySize = kNumLayers;
    desc.isCube    = true;
    desc.format    = gfx::Format::R8G8B8A8_UNORM;
    desc.usage     = gfx::TextureUsage::ShaderResource;
    return gfx.CreateTexture(desc, pixels.data());
}

gfx::TextureHandle CreateStudioEnvProbe(gfx::IGFXDevice& gfx) {
    // Studio probe intensities. The HD PS treats probe samples as raw
    // linear irradiance and combines them with the analytic light
    // (kGeosetLightColor ≈ 0.95 diffuse + 0.22 ambient) + direct
    // Cook-Torrance. Pushing probe values too high saturates to near-
    // white; too low and metallic surfaces lose their cubemap highlight.
    // These values are tuned so non-metallic surfaces stay in the
    // mid-tones while polished metals pick up visible sparkle from the
    // +Y "sky" highlight.

    // Slice 0 diffuse irradiance — modest neutral fill. Sampled via
    // t.Sample at mip 0 with surface normal direction: uniform → flat
    // contribution that scales with albedo for a subtle base tone.
    constexpr uint8_t kDiffR = 0x3C, kDiffG = 0x3C, kDiffB = 0x40;  // ~0.22 linear neutral

    // Slice 1 specular prefilter — the face that gets sampled depends
    // on reflection direction, so brighter faces produce the visible
    // "cube reflection" sparkle on low-roughness pixels. Strong sky
    // highlight so overhead-facing metals shine, dim warm ground so
    // under-lighting stays subtle, medium sides for neutral surround.
    constexpr uint8_t kSideR = 0x45, kSideG = 0x48, kSideB = 0x4C;  // ~0.27 linear cool-neutral
    constexpr uint8_t kTopR  = 0xA8, kTopG  = 0xB0, kTopB  = 0xC0;  // ~0.62 linear cool sky
    constexpr uint8_t kBotR  = 0x14, kBotG  = 0x12, kBotB  = 0x10;  // ~0.08 linear warm ground

    struct FaceRGB { uint8_t r, g, b; };
    const FaceRGB diffColors[6] = {
        {kDiffR, kDiffG, kDiffB}, {kDiffR, kDiffG, kDiffB},
        {kDiffR, kDiffG, kDiffB}, {kDiffR, kDiffG, kDiffB},
        {kDiffR, kDiffG, kDiffB}, {kDiffR, kDiffG, kDiffB},
    };
    const FaceRGB specColors[6] = {
        {kSideR, kSideG, kSideB},  // +X (forward) = side
        {kSideR, kSideG, kSideB},  // -X (backward) = side
        {kTopR,  kTopG,  kTopB },  // +Y (world up via swizzle = world +Z)
        {kBotR,  kBotG,  kBotB },  // -Y (world down)
        {kSideR, kSideG, kSideB},  // +Z (world left) = side
        {kSideR, kSideG, kSideB},  // -Z (world right) = side
    };

    size_t bytesPerFace = 0;
    for (int mip = 0; mip < kEnvProbeMipLevels; ++mip) {
        int w = std::max(1, kEnvProbeSize >> mip);
        int h = std::max(1, kEnvProbeSize >> mip);
        bytesPerFace += static_cast<size_t>(w) * h * 4;
    }
    const int kNumCubes = 2;
    const int kNumLayers = 6 * kNumCubes;
    size_t totalBytes = bytesPerFace * static_cast<size_t>(kNumLayers);

    std::vector<uint8_t> pixels(totalBytes, 0);
    uint8_t* cursor = pixels.data();
    for (int layer = 0; layer < kNumLayers; ++layer) {
        const int cube = layer / 6;
        const int face = layer % 6;
        const FaceRGB& c = (cube == 0) ? diffColors[face] : specColors[face];
        for (int mip = 0; mip < kEnvProbeMipLevels; ++mip) {
            int w = std::max(1, kEnvProbeSize >> mip);
            int h = std::max(1, kEnvProbeSize >> mip);
            for (int i = 0; i < w * h; ++i) {
                cursor[i * 4 + 0] = c.r;
                cursor[i * 4 + 1] = c.g;
                cursor[i * 4 + 2] = c.b;
                cursor[i * 4 + 3] = 0xFF;
            }
            cursor += static_cast<size_t>(w) * h * 4;
        }
    }

    gfx::TextureDesc desc;
    desc.width     = kEnvProbeSize;
    desc.height    = kEnvProbeSize;
    desc.mipLevels = kEnvProbeMipLevels;
    desc.arraySize = kNumLayers;
    desc.isCube    = true;
    desc.format    = gfx::Format::R8G8B8A8_UNORM;
    desc.usage     = gfx::TextureUsage::ShaderResource;
    return gfx.CreateTexture(desc, pixels.data());
}

bool WriteDebugFacesDds(const std::string& outPath) {
    // Build a 128x128 cube-array Texture with 2 cubes (mirrors real
    // probe layout: slice 0 = diffuse, slice 1 = specular) and 8 mips.
    // Fill every mip of every face of both cubes with one of six
    // distinct solid colours, keyed to the DDS layer index we write
    // to. Match the in-memory debug-faces probe so rendering results
    // between the two can be compared directly.
    //
    // Layer (DDS storage order used by our writer) -> colour:
    //     0 → RED        (standard DDS layer 0)
    //     1 → GREEN
    //     2 → BLUE
    //     3 → YELLOW
    //     4 → CYAN
    //     5 → MAGENTA
    // Cube 1 shares the palette so specular arraySlice=1 samples and
    // diffuse arraySlice=0 samples give identical direction-keyed
    // colours.
    using whiteout::textures::Texture;
    using whiteout::textures::PixelFormat;

    constexpr uint32_t kSize     = 128;
    constexpr uint32_t kMips     = 8;
    constexpr uint32_t kNumCubes = 2;

    // Edge-matching debug pattern. Each face has a solid centre colour
    // identifying the face, and four coloured edge stripes — each
    // stripe's colour is unique to the cube edge it represents (one of
    // 12 cube edges). BOTH faces sharing a cube edge use the SAME
    // colour on their corresponding edge stripe, so if the pipeline
    // (DDS writer → DDS parser → cube SRV → shader sample) preserves
    // face orientation end-to-end, every seam in a cube-cross unwrap
    // will show colour continuity across the fold. Any mismatch tells
    // us which face is incorrectly oriented or indexed.
    //
    // Face centres (solid-fill interior):
    //   +X red, -X green, +Y blue, -Y yellow, +Z cyan, -Z magenta.
    //
    // Edge stripes (one unique colour per cube edge). Per D3D cube
    // convention the (u,v) orientation of each face determines which
    // of its 4 edges is adjacent to which neighbour face:
    //   +X: top→+Y  bottom→-Y  left→+Z  right→-Z
    //   -X: top→+Y  bottom→-Y  left→-Z  right→+Z
    //   +Y: top→-Z  bottom→+Z  left→-X  right→+X
    //   -Y: top→+Z  bottom→-Z  left→-X  right→+X
    //   +Z: top→+Y  bottom→-Y  left→-X  right→+X
    //   -Z: top→+Y  bottom→-Y  left→+X  right→-X

    // Face-centre colours (also used to ID the face from the solid fill).
    constexpr uint8_t kFaceCenter[6][4] = {
        { 0xFF, 0x00, 0x00, 0xFF },  // 0 +X RED
        { 0x00, 0xFF, 0x00, 0xFF },  // 1 -X GREEN
        { 0x00, 0x00, 0xFF, 0xFF },  // 2 +Y BLUE
        { 0xFF, 0xFF, 0x00, 0xFF },  // 3 -Y YELLOW
        { 0x00, 0xFF, 0xFF, 0xFF },  // 4 +Z CYAN
        { 0xFF, 0x00, 0xFF, 0xFF },  // 5 -Z MAGENTA
    };

    // 12 cube edges indexed by the pair of faces that share them. Any
    // edge colour chosen so that its two 4-bit RGB components are
    // distinct — corners where three edges meet will still be
    // visually unambiguous.
    enum EdgeIdx : int {
        E_XY_pp = 0, E_XY_pn, E_XY_np, E_XY_nn,  // +X/+Y, +X/-Y, -X/+Y, -X/-Y
        E_XZ_pp, E_XZ_pn, E_XZ_np, E_XZ_nn,
        E_YZ_pp, E_YZ_pn, E_YZ_np, E_YZ_nn,
    };
    constexpr uint8_t kEdgeColors[12][4] = {
        { 0xFF, 0xA0, 0x40, 0xFF },  // +X/+Y  orange
        { 0xA0, 0x40, 0xFF, 0xFF },  // +X/-Y  purple
        { 0x40, 0xC0, 0xFF, 0xFF },  // -X/+Y  teal
        { 0x80, 0x60, 0x40, 0xFF },  // -X/-Y  brown
        { 0xFF, 0x80, 0xC0, 0xFF },  // +X/+Z  pink
        { 0x80, 0xFF, 0x40, 0xFF },  // +X/-Z  lime
        { 0xC0, 0x80, 0x40, 0xFF },  // -X/+Z  tan
        { 0x40, 0x60, 0x80, 0xFF },  // -X/-Z  blue-grey
        { 0x80, 0xFF, 0xC0, 0xFF },  // +Y/+Z  mint
        { 0xC0, 0x80, 0xFF, 0xFF },  // +Y/-Z  lavender
        { 0x80, 0x00, 0x20, 0xFF },  // -Y/+Z  dark red
        { 0x00, 0x60, 0x20, 0xFF },  // -Y/-Z  dark green
    };

    // Per-face edge assignments — [face][t/b/l/r] = index into kEdgeColors.
    // Order within each row: top, bottom, left, right.
    constexpr int kFaceEdges[6][4] = {
        /* +X */ { E_XY_pp, E_XY_pn, E_XZ_pp, E_XZ_pn },  // top→+Y, bot→-Y, left→+Z, right→-Z
        /* -X */ { E_XY_np, E_XY_nn, E_XZ_nn, E_XZ_np },  // top→+Y, bot→-Y, left→-Z, right→+Z
        /* +Y */ { E_YZ_pn, E_YZ_pp, E_XY_np, E_XY_pp },  // top→-Z, bot→+Z, left→-X, right→+X
        /* -Y */ { E_YZ_np, E_YZ_nn, E_XY_nn, E_XY_pn },  // top→+Z, bot→-Z, left→-X, right→+X
        /* +Z */ { E_YZ_pp, E_YZ_np, E_XZ_np, E_XZ_pp },  // top→+Y, bot→-Y, left→-X, right→+X
        /* -Z */ { E_YZ_pn, E_YZ_nn, E_XZ_pn, E_XZ_nn },  // top→+Y, bot→-Y, left→+X, right→-X
    };

    Texture tex = Texture::createCubeArray(PixelFormat::RGBA8, kSize,
                                           kNumCubes, kMips);

    const uint32_t layers = tex.layerCount();  // 6 * kNumCubes
    for (uint32_t layer = 0; layer < layers; ++layer) {
        const uint32_t face    = layer % 6u;
        const uint8_t* center  = kFaceCenter[face];
        const uint8_t* edgeTop = kEdgeColors[kFaceEdges[face][0]];
        const uint8_t* edgeBot = kEdgeColors[kFaceEdges[face][1]];
        const uint8_t* edgeLf  = kEdgeColors[kFaceEdges[face][2]];
        const uint8_t* edgeRt  = kEdgeColors[kFaceEdges[face][3]];

        for (uint32_t mip = 0; mip < kMips; ++mip) {
            auto dst = tex.mipData(mip, layer);
            const uint32_t w = std::max(1u, kSize >> mip);
            const uint32_t h = std::max(1u, kSize >> mip);
            // Border width scales with mip size so the stripes remain
            // visible at every mip in the chain.
            const uint32_t border = std::max(1u, w / 8);

            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    const uint32_t distTop   = y;
                    const uint32_t distBot   = (h - 1) - y;
                    const uint32_t distLf    = x;
                    const uint32_t distRt    = (w - 1) - x;
                    const uint32_t distMin   = std::min({distTop, distBot, distLf, distRt});
                    const uint8_t* c = center;
                    if (distMin < border) {
                        // Whichever edge is nearest wins (resolves corners).
                        if      (distMin == distTop) c = edgeTop;
                        else if (distMin == distBot) c = edgeBot;
                        else if (distMin == distLf)  c = edgeLf;
                        else                         c = edgeRt;
                    }
                    const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
                    dst[idx + 0] = c[0];
                    dst[idx + 1] = c[1];
                    dst[idx + 2] = c[2];
                    dst[idx + 3] = c[3];
                }
            }
        }
    }

    whiteout::textures::dds::Writer writer(
        whiteout::textures::dds::Writer::WriteMode::Lenient);
    try {
        writer.write(outPath, tex);
    } catch (...) {
        DbgLogf("[WDEX IBL] WriteDebugFacesDds FAILED to write %s\n", outPath.c_str());
        return false;
    }
    if (writer.hasIssues()) {
        DbgLogf("[WDEX IBL] WriteDebugFacesDds wrote with issues:\n");
        for (const auto& issue : writer.getIssues()) {
            DbgLogf("[WDEX IBL]   %s\n", issue.c_str());
        }
    }
    DbgLogf("[WDEX IBL] WriteDebugFacesDds OK -> %s\n", outPath.c_str());
    return true;
}

} // namespace WhiteoutDex::ibl
