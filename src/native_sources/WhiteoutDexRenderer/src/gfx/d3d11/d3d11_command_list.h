#pragma once
// ============================================================================
// D3D11 Backend — IGFXCommandList wrapping ID3D11DeviceContext
// ============================================================================

#include "gfx/gfx.h"

namespace WhiteoutDex::gfx::d3d11 {

class D3D11Device;

class D3D11CommandList final : public IGFXCommandList {
public:
    explicit D3D11CommandList(D3D11Device& device);
    ~D3D11CommandList() override = default;

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

private:
    D3D11Device& device_;
    bool         inRenderPass_ = false;
};

} // namespace WhiteoutDex::gfx::d3d11
