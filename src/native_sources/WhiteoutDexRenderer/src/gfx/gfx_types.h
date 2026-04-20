#pragma once
// ============================================================================
// GFX Service — Backend-Neutral Types (enums, descriptors, POD structs)
//
// No API-specific headers allowed. Included by gfx.h.
// ============================================================================

#include <cstdint>
#include <span>

namespace WhiteoutDex::gfx {

// ============================================================================
// Enums
// ============================================================================

enum class GfxApi { D3D11, D3D12, Vulkan };

enum class Format : uint16_t {
    Unknown,

    // ---- Uncompressed ----
    R8_UNORM,
    R8G8_UNORM,
    R8G8B8A8_UNORM,
    R8G8B8A8_UNORM_SRGB,
    R8G8B8A8_UINT,
    B8G8R8A8_UNORM,

    R16_UNORM,
    R16G16_UNORM,
    R16G16B16A16_UNORM,
    R16G16B16A16_FLOAT,

    R16_UINT,
    R32_UINT,

    R32_FLOAT,
    R32G32_FLOAT,
    R32G32B32_FLOAT,
    R32G32B32A32_FLOAT,

    // ---- Depth / stencil ----
    D24_UNORM_S8_UINT,
    D32_FLOAT,

    // ---- Block-compressed (BCn). The UNORM / UNORM_SRGB split mirrors
    //      DXGI -- sampler reads return linear UNORM unless the SRV is
    //      created with the _SRGB variant, in which case the hardware
    //      linearises the color channels (alpha stays linear). BC4 / BC5
    //      only have UNORM forms in DXGI; BC6H is the HDR float variant.
    BC1_UNORM,
    BC1_UNORM_SRGB,
    BC2_UNORM,
    BC2_UNORM_SRGB,
    BC3_UNORM,
    BC3_UNORM_SRGB,
    BC4_UNORM,
    BC5_UNORM,
    BC6H_UF16,
    BC7_UNORM,
    BC7_UNORM_SRGB,
};

// Returns true for BCn block-compressed formats (4x4 block layout, non-power-of-2 pitch).
inline bool IsBlockCompressed(Format f) {
    switch (f) {
        case Format::BC1_UNORM: case Format::BC1_UNORM_SRGB:
        case Format::BC2_UNORM: case Format::BC2_UNORM_SRGB:
        case Format::BC3_UNORM: case Format::BC3_UNORM_SRGB:
        case Format::BC4_UNORM:
        case Format::BC5_UNORM:
        case Format::BC6H_UF16:
        case Format::BC7_UNORM: case Format::BC7_UNORM_SRGB:
            return true;
        default:
            return false;
    }
}

// Bytes per block (BCn) or bytes per pixel (uncompressed). Matches DXGI
// bit-count tables: BC1/BC4 = 8 B/block, all other BCn = 16 B/block.
inline uint32_t FormatBytesPerBlock(Format f) {
    switch (f) {
        case Format::R8_UNORM:                return 1;
        case Format::R8G8_UNORM: case Format::R16_UNORM: case Format::R16_UINT: return 2;
        case Format::R8G8B8A8_UNORM: case Format::R8G8B8A8_UNORM_SRGB:
        case Format::R8G8B8A8_UINT:  case Format::B8G8R8A8_UNORM:
        case Format::R16G16_UNORM:   case Format::R32_UINT:
        case Format::R32_FLOAT:      case Format::D24_UNORM_S8_UINT:
        case Format::D32_FLOAT:      return 4;
        case Format::R16G16B16A16_UNORM: case Format::R16G16B16A16_FLOAT:
        case Format::R32G32_FLOAT:   return 8;
        case Format::R32G32B32_FLOAT: return 12;
        case Format::R32G32B32A32_FLOAT: return 16;
        case Format::BC1_UNORM: case Format::BC1_UNORM_SRGB:
        case Format::BC4_UNORM: return 8;
        case Format::BC2_UNORM: case Format::BC2_UNORM_SRGB:
        case Format::BC3_UNORM: case Format::BC3_UNORM_SRGB:
        case Format::BC5_UNORM:
        case Format::BC6H_UF16:
        case Format::BC7_UNORM: case Format::BC7_UNORM_SRGB:
            return 16;
        case Format::Unknown:
        default: return 0;
    }
}

enum class BufferUsage : uint32_t {
    None            = 0,
    Vertex          = 1 << 0,
    Index           = 1 << 1,
    Constant        = 1 << 2,
    ShaderResource  = 1 << 3,
    UnorderedAccess = 1 << 4,
    CpuWritable     = 1 << 5,
    GpuWritable     = 1 << 6,   // DEFAULT usage even with initial data (CopyBuffer dest)
};

inline BufferUsage  operator|(BufferUsage  a, BufferUsage  b) { return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
inline BufferUsage  operator&(BufferUsage  a, BufferUsage  b) { return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }
inline BufferUsage& operator|=(BufferUsage& a, BufferUsage b) { a = a | b; return a; }
inline bool         hasFlag(BufferUsage v, BufferUsage f)     { return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0; }

enum class TextureUsage : uint32_t {
    None           = 0,
    ShaderResource = 1 << 0,
    RenderTarget   = 1 << 1,
    DepthStencil   = 1 << 2,
};

inline TextureUsage  operator|(TextureUsage  a, TextureUsage  b) { return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
inline TextureUsage  operator&(TextureUsage  a, TextureUsage  b) { return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }
inline TextureUsage& operator|=(TextureUsage& a, TextureUsage b) { a = a | b; return a; }
inline bool          hasFlag(TextureUsage v, TextureUsage f)     { return (static_cast<uint32_t>(v) & static_cast<uint32_t>(f)) != 0; }

enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList };

enum class CullMode  { None, Back, Front };
enum class FillMode  { Solid, Wireframe };
enum class CompareOp { Never, Less, LessEqual, Equal, Greater, GreaterEqual, Always };

enum class BlendFactor { Zero, One, SrcAlpha, InvSrcAlpha, SrcColor, DstColor,
                         InvSrcColor, InvDstColor };
enum class BlendOp     { Add, Subtract };

enum class Filter      { Point, Linear };
enum class AddressMode { Wrap, Clamp, Mirror };

enum class ShaderStage { Vertex, Pixel, Compute };

// ============================================================================
// Resource descriptors
// ============================================================================

struct BufferDesc {
    uint64_t    size          = 0;
    uint32_t    elementStride = 0;   // >0 for structured buffers
    BufferUsage usage         = BufferUsage::None;
};

struct TextureDesc {
    int          width     = 0;
    int          height    = 0;
    int          mipLevels = 1;      // 0 = full chain
    // Number of 2D array slices. For `isCube = true` this must be a multiple
    // of 6 (each face is a slice). For plain 2D textures leave at 1.
    int          arraySize = 1;
    Format       format    = Format::R8G8B8A8_UNORM;
    TextureUsage usage     = TextureUsage::ShaderResource;
    // When true, the resource is created as a TextureCube (arraySize == 6) or
    // TextureCubeArray (arraySize == 6 * N). The SRV dimension is chosen
    // automatically: Texture2D / Texture2DArray / TextureCube / TextureCubeArray.
    bool         isCube    = false;
};

struct SamplerDesc {
    Filter      minFilter = Filter::Linear;
    Filter      magFilter = Filter::Linear;
    AddressMode addressU  = AddressMode::Wrap;
    AddressMode addressV  = AddressMode::Wrap;
    AddressMode addressW  = AddressMode::Wrap;
};

// ============================================================================
// PSO descriptors
// ============================================================================

// Forward-declare handle types used by pipeline descs (defined in gfx.h).
// These are the same enum types — we just need the name here.
enum class ShaderHandle : uint64_t;

struct InputElement {
    const char* semantic      = nullptr;
    uint32_t    semanticIndex = 0;
    Format      format        = Format::Unknown;
    uint32_t    offset        = 0;
    uint32_t    inputSlot     = 0;   // vertex-buffer slot this attribute pulls from
};

struct BlendDesc {
    bool        enable          = false;
    BlendFactor srcColor        = BlendFactor::One;
    BlendFactor dstColor        = BlendFactor::Zero;
    BlendOp     opColor         = BlendOp::Add;
    BlendFactor srcAlpha        = BlendFactor::One;
    BlendFactor dstAlpha        = BlendFactor::Zero;
    BlendOp     opAlpha         = BlendOp::Add;
    bool        alphaToCoverage = false;
};

struct DepthStencilDesc {
    bool      depthTest    = true;
    bool      depthWrite   = true;
    CompareOp depthCompare = CompareOp::LessEqual;
};

struct RasterizerDesc {
    CullMode cull          = CullMode::Back;
    FillMode fill          = FillMode::Solid;
    bool     frontCCW      = false;
    bool     scissorEnable = false;
};

struct GraphicsPipelineDesc {
    ShaderHandle                  vs       = ShaderHandle{0};
    ShaderHandle                  ps       = ShaderHandle{0};
    std::span<const InputElement> inputLayout;
    PrimitiveTopology             topology     = PrimitiveTopology::TriangleList;
    BlendDesc                     blend;
    DepthStencilDesc              depthStencil;
    RasterizerDesc                rasterizer;
    Format                        rtvFormat    = Format::R8G8B8A8_UNORM;
    Format                        dsvFormat    = Format::D24_UNORM_S8_UINT;
};

struct ComputePipelineDesc {
    ShaderHandle cs = ShaderHandle{0};
};

} // namespace WhiteoutDex::gfx
