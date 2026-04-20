// ============================================================================
// D3D11 Backend — IGFXCommandList implementation
// ============================================================================

#include "d3d11_command_list.h"
#include "d3d11_device.h"
#include <cassert>

namespace WhiteoutDex::gfx::d3d11 {

D3D11CommandList::D3D11CommandList(D3D11Device& device)
    : device_(device) {}

// ============================================================================
// Render pass
// ============================================================================

void D3D11CommandList::BeginRenderPass(TextureHandle color, TextureHandle depth,
                                        const float clearColor[4], float clearDepth,
                                        uint8_t clearStencil) {
    assert(!inRenderPass_ && "Nested BeginRenderPass");
    inRenderPass_ = true;

    auto* ctx = device_.GetD3DContext();

    auto* colorEntry = device_.GetTexture(color);
    auto* depthEntry = device_.GetTexture(depth);

    ID3D11RenderTargetView* rtv = colorEntry ? colorEntry->rtv : nullptr;
    ID3D11DepthStencilView* dsv = depthEntry ? depthEntry->dsv : nullptr;

    ctx->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, dsv);

    if (rtv)
        ctx->ClearRenderTargetView(rtv, clearColor);
    if (dsv)
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                   clearDepth, clearStencil);
}

void D3D11CommandList::EndRenderPass() {
    assert(inRenderPass_ && "EndRenderPass without Begin");
    inRenderPass_ = false;
    // D3D11: no-op. D3D12/Vulkan would end the render pass here.
}

// ============================================================================
// Viewport / Scissor
// ============================================================================

void D3D11CommandList::SetViewport(const Viewport& vp) {
    D3D11_VIEWPORT d3dvp{};
    d3dvp.TopLeftX = vp.x;
    d3dvp.TopLeftY = vp.y;
    d3dvp.Width    = vp.width;
    d3dvp.Height   = vp.height;
    d3dvp.MinDepth = vp.minDepth;
    d3dvp.MaxDepth = vp.maxDepth;
    device_.GetD3DContext()->RSSetViewports(1, &d3dvp);
}

void D3D11CommandList::SetScissor(const Scissor& sc) {
    D3D11_RECT rect;
    rect.left   = sc.x;
    rect.top    = sc.y;
    rect.right  = sc.x + sc.width;
    rect.bottom = sc.y + sc.height;
    device_.GetD3DContext()->RSSetScissorRects(1, &rect);
}

// ============================================================================
// Pipeline binding
// ============================================================================

void D3D11CommandList::BindPipeline(PipelineHandle h) {
    auto* pso = device_.GetPipeline(h);
    if (!pso) return;

    auto* ctx = device_.GetD3DContext();

    if (pso->isCompute) {
        ctx->CSSetShader(pso->cs, nullptr, 0);
    } else {
        ctx->VSSetShader(pso->vs, nullptr, 0);
        ctx->PSSetShader(pso->ps, nullptr, 0);
        ctx->IASetInputLayout(pso->inputLayout);
        ctx->IASetPrimitiveTopology(pso->topology);
        ctx->RSSetState(pso->rasterState);

        float blendFactor[4] = {0, 0, 0, 0};
        ctx->OMSetBlendState(pso->blendState, blendFactor, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(pso->depthState, 0);
    }
}

// ============================================================================
// Resource binding
// ============================================================================

void D3D11CommandList::BindVertexBuffer(uint32_t slot, BufferHandle h,
                                         uint32_t stride, uint32_t offset) {
    auto* entry = device_.GetBuffer(h);
    ID3D11Buffer* buf = entry ? entry->buffer : nullptr;
    UINT s = stride, o = offset;
    device_.GetD3DContext()->IASetVertexBuffers(slot, 1, &buf, &s, &o);
}

void D3D11CommandList::BindIndexBuffer(BufferHandle h, Format fmt) {
    auto* entry = device_.GetBuffer(h);
    ID3D11Buffer* buf = entry ? entry->buffer : nullptr;
    device_.GetD3DContext()->IASetIndexBuffer(buf, ToDXGI(fmt), 0);
}

void D3D11CommandList::BindConstantBuffer(ShaderStage stage, uint32_t slot, BufferHandle h) {
    auto* entry = device_.GetBuffer(h);
    ID3D11Buffer* buf = entry ? entry->buffer : nullptr;
    auto* ctx = device_.GetD3DContext();
    switch (stage) {
        case ShaderStage::Vertex:  ctx->VSSetConstantBuffers(slot, 1, &buf); break;
        case ShaderStage::Pixel:   ctx->PSSetConstantBuffers(slot, 1, &buf); break;
        case ShaderStage::Compute: ctx->CSSetConstantBuffers(slot, 1, &buf); break;
    }
}

void D3D11CommandList::BindShaderResource(ShaderStage stage, uint32_t slot, TextureHandle h) {
    auto* entry = device_.GetTexture(h);
    ID3D11ShaderResourceView* srv = entry ? entry->srv : nullptr;
    auto* ctx = device_.GetD3DContext();
    switch (stage) {
        case ShaderStage::Vertex:  ctx->VSSetShaderResources(slot, 1, &srv); break;
        case ShaderStage::Pixel:   ctx->PSSetShaderResources(slot, 1, &srv); break;
        case ShaderStage::Compute: ctx->CSSetShaderResources(slot, 1, &srv); break;
    }
}

void D3D11CommandList::BindShaderResource(ShaderStage stage, uint32_t slot, BufferHandle h) {
    auto* entry = device_.GetBuffer(h);
    ID3D11ShaderResourceView* srv = entry ? entry->srv : nullptr;
    auto* ctx = device_.GetD3DContext();
    switch (stage) {
        case ShaderStage::Vertex:  ctx->VSSetShaderResources(slot, 1, &srv); break;
        case ShaderStage::Pixel:   ctx->PSSetShaderResources(slot, 1, &srv); break;
        case ShaderStage::Compute: ctx->CSSetShaderResources(slot, 1, &srv); break;
    }
}

void D3D11CommandList::BindUnorderedAccess(uint32_t slot, BufferHandle h) {
    auto* entry = device_.GetBuffer(h);
    ID3D11UnorderedAccessView* uav = entry ? entry->uav : nullptr;
    UINT initialCount = static_cast<UINT>(-1);
    device_.GetD3DContext()->CSSetUnorderedAccessViews(slot, 1, &uav, &initialCount);
}

void D3D11CommandList::BindSampler(ShaderStage stage, uint32_t slot, SamplerHandle h) {
    auto* entry = device_.GetSampler(h);
    ID3D11SamplerState* sampler = entry ? entry->sampler : nullptr;
    auto* ctx = device_.GetD3DContext();
    switch (stage) {
        case ShaderStage::Vertex:  ctx->VSSetSamplers(slot, 1, &sampler); break;
        case ShaderStage::Pixel:   ctx->PSSetSamplers(slot, 1, &sampler); break;
        case ShaderStage::Compute: ctx->CSSetSamplers(slot, 1, &sampler); break;
    }
}

// ============================================================================
// Standalone depth clear
// ============================================================================

void D3D11CommandList::ClearDepth(TextureHandle depth, float clearDepth, uint8_t clearStencil) {
    auto* depthEntry = device_.GetTexture(depth);
    if (depthEntry && depthEntry->dsv)
        device_.GetD3DContext()->ClearDepthStencilView(
            depthEntry->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
            clearDepth, clearStencil);
}

// ============================================================================
// Copy
// ============================================================================

void D3D11CommandList::CopyBuffer(BufferHandle dst, BufferHandle src) {
    auto* dstEntry = device_.GetBuffer(dst);
    auto* srcEntry = device_.GetBuffer(src);
    if (dstEntry && srcEntry && dstEntry->buffer && srcEntry->buffer)
        device_.GetD3DContext()->CopyResource(dstEntry->buffer, srcEntry->buffer);
}

// ============================================================================
// Draw / Dispatch
// ============================================================================

void D3D11CommandList::Draw(uint32_t vertexCount, uint32_t firstVertex) {
    device_.GetD3DContext()->Draw(vertexCount, firstVertex);
}

void D3D11CommandList::DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex) {
    device_.GetD3DContext()->DrawIndexed(indexCount, firstIndex, baseVertex);
}

void D3D11CommandList::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
    device_.GetD3DContext()->Dispatch(gx, gy, gz);
}

} // namespace WhiteoutDex::gfx::d3d11
