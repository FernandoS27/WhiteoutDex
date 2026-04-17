#pragma once
// ============================================================================
// D3D12 Backend — IGFXCommandList implementation
//
// Wraps a single persistent ID3D12GraphicsCommandList. Binds are staged into
// per-stage slot arrays; descriptor tables are materialized at draw/dispatch
// time by copying staged CPU descriptors into the shader-visible ring.
// Root CBVs (b0/b1 per stage) are fed directly from BufferEntry's cached
// upload-ring GPU VA for CpuWritable buffers (zero descriptor heap traffic).
// ============================================================================

#include "gfx/gfx.h"
#include "d3d12_resources.h"

#include <array>

namespace WhiteoutDex::gfx::d3d12 {

class D3D12Device;

class D3D12CommandList final : public IGFXCommandList {
public:
    explicit D3D12CommandList(D3D12Device& device);
    ~D3D12CommandList() override = default;

    void BeginRenderPass(TextureHandle color, TextureHandle depth,
                         const float clearColor[4], float clearDepth,
                         uint8_t clearStencil) override;
    void EndRenderPass() override;

    void SetViewport(const Viewport&) override;
    void SetScissor (const Scissor&) override;

    void BindPipeline(PipelineHandle) override;

    void BindVertexBuffer  (uint32_t slot, BufferHandle, uint32_t stride, uint32_t offset) override;
    void BindIndexBuffer   (BufferHandle, Format) override;
    void BindConstantBuffer(ShaderStage, uint32_t slot, BufferHandle) override;
    void BindShaderResource(ShaderStage, uint32_t slot, TextureHandle) override;
    void BindShaderResource(ShaderStage, uint32_t slot, BufferHandle) override;
    void BindUnorderedAccess(uint32_t slot, BufferHandle) override;
    void BindSampler       (ShaderStage, uint32_t slot, SamplerHandle) override;

    void ClearDepth(TextureHandle depth, float clearDepth, uint8_t clearStencil) override;

    void CopyBuffer(BufferHandle dst, BufferHandle src) override;

    void Draw       (uint32_t vertexCount, uint32_t firstVertex) override;
    void DrawIndexed(uint32_t indexCount,  uint32_t firstIndex, int32_t baseVertex) override;
    void Dispatch   (uint32_t gx, uint32_t gy, uint32_t gz) override;

    // Called by D3D12Device::Present (resets per-frame staged state).
    void OnFrameBegin();

    // Called by D3D12Device internally to insert a resource transition on
    // the open command list, updating the entry's cached state.
    void TransitionBuffer (BufferEntry&, D3D12_RESOURCE_STATES newState);
    void TransitionTexture(TextureEntry&, D3D12_RESOURCE_STATES newState);

private:
    void EnsureDescriptorHeapsBound();
    void ApplyGraphicsBindings();
    void ApplyComputeBindings();
    void PromoteSrv(BufferHandle, ShaderStage);
    void PromoteSrv(TextureHandle, ShaderStage);

    D3D12Device& device_;

    // Current pipeline state (tracked to choose graphics vs compute binding path).
    bool inRenderPass_   = false;
    bool lastWasCompute_ = false;
    bool haveAnyPipeline_ = false;

    // Heaps currently bound via SetDescriptorHeaps — only needs to be done
    // after allocator reset at frame start.
    bool descriptorHeapsBound_ = false;

    TextureHandle currentColorRt_{};
    TextureHandle currentDepthRt_{};

    // ---- Staged bindings per stage ----

    // Root CBVs (b0/b1) — stored as BufferHandles so that the actual GPU VA
    // is re-resolved at draw time. This preserves D3D11's implicit-rebind
    // semantics: callers may call MapBuffer on a CpuWritable buffer between
    // BindConstantBuffer and Draw, and each Draw picks up the latest slice.
    struct CbvSlot { BufferHandle buffer = BufferHandle::Invalid; };
    std::array<CbvSlot, kRootCbvsPerStage> cbvVs_{};
    std::array<CbvSlot, kRootCbvsPerStage> cbvPs_{};
    std::array<CbvSlot, kRootCbvsPerStage> cbvCs_{};

    // SRV slots — we keep the CPU descriptor handle so we can copy it into
    // the shader-visible ring at draw time.
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kSrvsPerStage> srvVs_{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kSrvsPerStage> srvPs_{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kSrvsPerStage> srvCs_{};

    // UAV slots (compute only)
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kUavsForCompute> uavCs_{};

    // Sampler slots
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kSamplersPerStage> samplerPs_{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kSamplersPerStage> samplerCs_{};
};

} // namespace WhiteoutDex::gfx::d3d12
