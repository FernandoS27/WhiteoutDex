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
    R8G8B8A8_UNORM,
    R8G8B8A8_UINT,
    B8G8R8A8_UNORM,
    R32_FLOAT,
    R32G32_FLOAT,
    R32G32B32_FLOAT,
    R32G32B32A32_FLOAT,
    R16_UINT,
    R32_UINT,
    D24_UNORM_S8_UINT,
    D32_FLOAT,
};

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
    Format       format    = Format::R8G8B8A8_UNORM;
    TextureUsage usage     = TextureUsage::ShaderResource;
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
