#pragma once
// ============================================================================
// GFX Service — Backend-Agnostic Graphics Abstraction
//
// This is the ONLY header that renderer/ code may include from gfx/.
// It must NOT include <d3d11.h>, <dxgi.h>, <d3d12.h>, or <vulkan.h>.
//
// See docs/GFX.md for the full design rationale.
// ============================================================================

#include "gfx/gfx_types.h"
#include <cstdint>
#include <memory>

namespace WhiteoutDex::gfx {

// ============================================================================
// Opaque typed handles — all resources are identified by these, not pointers.
// Backends store the real objects in slot-maps indexed by handle.
// Upper 16 bits = generation counter, lower 48 bits = slot index.
// ============================================================================

enum class BufferHandle    : uint64_t { Invalid = 0 };
enum class TextureHandle   : uint64_t { Invalid = 0 };
enum class ShaderHandle    : uint64_t { Invalid = 0 };
enum class PipelineHandle  : uint64_t { Invalid = 0 };
enum class SamplerHandle   : uint64_t { Invalid = 0 };
enum class SwapChainHandle : uint64_t { Invalid = 0 };

// ============================================================================
// Viewport / Scissor
// ============================================================================

struct Viewport {
    float x        = 0;
    float y        = 0;
    float width    = 0;
    float height   = 0;
    float minDepth = 0;
    float maxDepth = 1;
};

struct Scissor {
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;
};

// ============================================================================
// IGFXCommandList — draw / dispatch / state binding
// ============================================================================

class IGFXCommandList {
public:
    virtual ~IGFXCommandList() = default;

    // Render pass — only one at a time.
    virtual void BeginRenderPass(TextureHandle color, TextureHandle depth,
                                 const float clearColor[4], float clearDepth,
                                 uint8_t clearStencil) = 0;
    virtual void EndRenderPass() = 0;

    virtual void SetViewport(const Viewport&) = 0;
    virtual void SetScissor (const Scissor&) = 0;

    virtual void BindPipeline(PipelineHandle) = 0;

    // Resource binding — flat slot model (D3D11 register slots / D3D12 root tables / Vulkan descriptor sets)
    virtual void BindVertexBuffer  (uint32_t slot, BufferHandle, uint32_t stride, uint32_t offset = 0) = 0;
    virtual void BindIndexBuffer   (BufferHandle, Format /* R16_UINT or R32_UINT */) = 0;
    virtual void BindConstantBuffer(ShaderStage, uint32_t slot, BufferHandle) = 0;
    virtual void BindShaderResource(ShaderStage, uint32_t slot, TextureHandle) = 0;
    virtual void BindShaderResource(ShaderStage, uint32_t slot, BufferHandle /* structured */) = 0;
    virtual void BindUnorderedAccess(uint32_t slot, BufferHandle) = 0;
    virtual void BindSampler       (ShaderStage, uint32_t slot, SamplerHandle) = 0;

    // Standalone depth clear (for sub-viewport rendering, e.g. ViewCube)
    virtual void ClearDepth(TextureHandle depth, float clearDepth, uint8_t clearStencil) = 0;

    // Resource copy (compute skinning: UAV output → VB)
    virtual void CopyBuffer(BufferHandle dst, BufferHandle src) = 0;

    // Draw / dispatch
    virtual void Draw       (uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount,  uint32_t firstIndex = 0, int32_t baseVertex = 0) = 0;
    virtual void Dispatch   (uint32_t gx, uint32_t gy, uint32_t gz) = 0;
};

// ============================================================================
// IGFXDevice — resource creation, swap chains, immediate context
// ============================================================================

class IGFXDevice {
public:
    virtual ~IGFXDevice() = default;

    // Resource creation
    virtual BufferHandle   CreateBuffer (const BufferDesc&,  const void* initial = nullptr) = 0;
    virtual TextureHandle  CreateTexture(const TextureDesc&, const void* initialPixels = nullptr) = 0;
    virtual ShaderHandle   CreateShader (ShaderStage, const void* bytecode, size_t size) = 0;
    virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual PipelineHandle CreateComputePipeline (const ComputePipelineDesc&) = 0;
    virtual SamplerHandle  CreateSampler(const SamplerDesc&) = 0;

    // Destruction (idempotent — Invalid is a no-op)
    virtual void Destroy(BufferHandle)   = 0;
    virtual void Destroy(TextureHandle)  = 0;
    virtual void Destroy(ShaderHandle)   = 0;
    virtual void Destroy(PipelineHandle) = 0;
    virtual void Destroy(SamplerHandle)  = 0;

    // CPU → GPU upload for dynamic buffers (map-discard semantics)
    virtual void  UpdateBuffer(BufferHandle, const void* data, size_t size) = 0;
    virtual void* MapBuffer   (BufferHandle) = 0;
    virtual void  UnmapBuffer (BufferHandle) = 0;

    // Swap chains. Default colorFormat is the *sRGB-encoded* RTV view —
    // every 3D draw goes through HDR + tonemap and the tonemap PS writes
    // linear LDR. The sRGB RTV gamma-encodes that linear output on write
    // so the display sees properly gamma-encoded pixels. Mirrors the
    // engine's pmChooseSwapChainFormat preferring RGBA8Unorm_sRGB
    // (pmFormat 32) — Preview RE @0x7ff609ab1db0. Texture-side sRGB
    // policy is now correct (mdx + max adapters call
    // ApplyTextureSrgbPolicy on every TextureData), so the linear ACES
    // input is in real linear space and a single sRGB encode at the
    // RTV completes the gamma chain.
    //
    // Flip-model swap chains forbid `_SRGB` resource formats, so the
    // backend strips the suffix for the swap chain create call and
    // re-applies it on the per-buffer RTV view (see
    // d3d12_device.cpp::CreateSwapChain).
    virtual SwapChainHandle CreateSwapChain(void* nativeWindowHandle,
                                            int width, int height,
                                            Format colorFormat = Format::R8G8B8A8_UNORM_SRGB) = 0;
    virtual void          ResizeSwapChain (SwapChainHandle, int width, int height) = 0;
    virtual void          DestroySwapChain(SwapChainHandle) = 0;
    virtual void          Present         (SwapChainHandle) = 0;
    virtual TextureHandle GetSwapChainBackBuffer(SwapChainHandle) = 0;

    // Off-screen render targets
    virtual TextureHandle CreateColorTarget(int w, int h, Format f) = 0;
    virtual TextureHandle CreateDepthTarget(int w, int h, Format f) = 0;

    // Command list
    virtual IGFXCommandList* GetImmediateContext() = 0;

    // Diagnostics
    virtual GfxApi      GetApi() const = 0;
    virtual const char* GetDeviceName() const = 0;
};

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<IGFXDevice> CreateDevice(GfxApi api);

} // namespace WhiteoutDex::gfx
