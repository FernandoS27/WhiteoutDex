// ============================================================================
// D3D12 Backend — IGFXCommandList implementation
// ============================================================================

#include "d3d12_command_list.h"
#include "d3d12_device.h"
#include <cassert>

namespace WhiteoutDex::gfx::d3d12 {

D3D12CommandList::D3D12CommandList(D3D12Device& device) : device_(device) {
    OnFrameBegin();
}

// ============================================================================
// Frame lifecycle
// ============================================================================

void D3D12CommandList::OnFrameBegin() {
    descriptorHeapsBound_ = false;
    haveAnyPipeline_      = false;
    lastWasCompute_       = false;
    inRenderPass_         = false;
    currentColorRt_       = TextureHandle::Invalid;
    currentDepthRt_       = TextureHandle::Invalid;

    // Reset staged bindings so stale pointers don't leak across frames.
    for (auto& s : cbvVs_)    s.buffer = BufferHandle::Invalid;
    for (auto& s : cbvPs_)    s.buffer = BufferHandle::Invalid;
    for (auto& s : cbvCs_)    s.buffer = BufferHandle::Invalid;
    for (auto& s : srvVs_)    s.ptr = 0;
    for (auto& s : srvPs_)    s.ptr = 0;
    for (auto& s : srvCs_)    s.ptr = 0;
    for (auto& s : uavCs_)    s.ptr = 0;
    for (auto& s : samplerPs_) s.ptr = 0;
    for (auto& s : samplerCs_) s.ptr = 0;
}

void D3D12CommandList::EnsureDescriptorHeapsBound() {
    if (descriptorHeapsBound_) return;
    ID3D12DescriptorHeap* heaps[] = {
        device_.CbvSrvUavRing().Heap(),
        device_.SamplerRing().Heap(),
    };
    device_.GetCmdList()->SetDescriptorHeaps(2, heaps);
    descriptorHeapsBound_ = true;
}

// ============================================================================
// Resource-state helpers
// ============================================================================

void D3D12CommandList::TransitionBuffer(BufferEntry& e, D3D12_RESOURCE_STATES newState) {
    if (!e.resource) return;
    // UPLOAD-heap buffers (CpuWritable) must remain in GENERIC_READ at all
    // times — any transition is a validation error. GENERIC_READ is a superset
    // of the read states we'd ask for (VB/IB/CB/SRV), so binds still work.
    if (hasFlag(e.desc.usage, BufferUsage::CpuWritable)) return;
    if (e.currentState == newState) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = e.resource;
    b.Transition.StateBefore = e.currentState;
    b.Transition.StateAfter  = newState;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    device_.GetCmdList()->ResourceBarrier(1, &b);
    e.currentState = newState;
}

void D3D12CommandList::TransitionTexture(TextureEntry& e, D3D12_RESOURCE_STATES newState) {
    if (!e.resource) return;
    if (e.currentState == newState) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = e.resource;
    b.Transition.StateBefore = e.currentState;
    b.Transition.StateAfter  = newState;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    device_.GetCmdList()->ResourceBarrier(1, &b);
    e.currentState = newState;
}

// ============================================================================
// Render pass
// ============================================================================

void D3D12CommandList::BeginRenderPass(TextureHandle color, TextureHandle depth,
                                       const float clearColor[4], float clearDepth,
                                       uint8_t clearStencil) {
    assert(!inRenderPass_ && "Nested BeginRenderPass");
    inRenderPass_ = true;
    currentColorRt_ = color;
    currentDepthRt_ = depth;

    auto* cmd = device_.GetCmdList();

    auto* colorEntry = device_.GetTexture(color);
    auto* depthEntry = device_.GetTexture(depth);

    if (colorEntry) TransitionTexture(*colorEntry, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (depthEntry) TransitionTexture(*depthEntry, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = colorEntry && colorEntry->hasRtv
                                       ? colorEntry->rtvCpu
                                       : D3D12_CPU_DESCRIPTOR_HANDLE{0};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = depthEntry && depthEntry->hasDsv
                                       ? depthEntry->dsvCpu
                                       : D3D12_CPU_DESCRIPTOR_HANDLE{0};

    cmd->OMSetRenderTargets(rtv.ptr ? 1 : 0, rtv.ptr ? &rtv : nullptr,
                             FALSE, dsv.ptr ? &dsv : nullptr);

    if (rtv.ptr)
        cmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    if (dsv.ptr)
        cmd->ClearDepthStencilView(dsv,
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            clearDepth, clearStencil, 0, nullptr);
}

void D3D12CommandList::EndRenderPass() {
    assert(inRenderPass_ && "EndRenderPass without Begin");
    inRenderPass_ = false;
}

// ============================================================================
// Viewport / Scissor
// ============================================================================

void D3D12CommandList::SetViewport(const Viewport& vp) {
    D3D12_VIEWPORT v{};
    v.TopLeftX = vp.x;
    v.TopLeftY = vp.y;
    v.Width    = vp.width;
    v.Height   = vp.height;
    v.MinDepth = vp.minDepth;
    v.MaxDepth = vp.maxDepth;
    device_.GetCmdList()->RSSetViewports(1, &v);

    // D3D12 REQUIRES a scissor rect for all draws (unlike D3D11, which only
    // applies scissor when RasterizerDesc::scissorEnable is true). Default
    // the scissor to match the viewport so the renderer doesn't need to track
    // it explicitly; an explicit SetScissor call after this overrides it.
    D3D12_RECT r{};
    r.left   = static_cast<LONG>(vp.x);
    r.top    = static_cast<LONG>(vp.y);
    r.right  = static_cast<LONG>(vp.x + vp.width);
    r.bottom = static_cast<LONG>(vp.y + vp.height);
    device_.GetCmdList()->RSSetScissorRects(1, &r);
}

void D3D12CommandList::SetScissor(const Scissor& sc) {
    D3D12_RECT r{};
    r.left   = sc.x;
    r.top    = sc.y;
    r.right  = sc.x + sc.width;
    r.bottom = sc.y + sc.height;
    device_.GetCmdList()->RSSetScissorRects(1, &r);
}

// ============================================================================
// Pipeline binding
// ============================================================================

void D3D12CommandList::BindPipeline(PipelineHandle h) {
    auto* pso = device_.GetPipeline(h);
    if (!pso || !pso->pso) return;

    auto* cmd = device_.GetCmdList();
    EnsureDescriptorHeapsBound();

    cmd->SetPipelineState(pso->pso);
    if (pso->isCompute) {
        cmd->SetComputeRootSignature(device_.GetComputeRS());
    } else {
        cmd->SetGraphicsRootSignature(device_.GetGraphicsRS());
        cmd->IASetPrimitiveTopology(pso->topology);
    }
    lastWasCompute_  = pso->isCompute;
    haveAnyPipeline_ = true;
}

// ============================================================================
// Resource binding — stage only, actual table copies happen at Draw time.
// ============================================================================

void D3D12CommandList::BindVertexBuffer(uint32_t slot, BufferHandle h,
                                        uint32_t stride, uint32_t offset) {
    auto* e = device_.GetBuffer(h);
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    if (e && e->resource) {
        TransitionBuffer(*e, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        D3D12_GPU_VIRTUAL_ADDRESS base =
            hasFlag(e->desc.usage, BufferUsage::CpuWritable) && e->cpuWritableVA
                ? e->cpuWritableVA
                : e->resource->GetGPUVirtualAddress();
        vbv.BufferLocation = base + offset;
        vbv.SizeInBytes    = static_cast<UINT>(e->desc.size) - offset;
        vbv.StrideInBytes  = stride;
    }
    device_.GetCmdList()->IASetVertexBuffers(slot, 1, &vbv);
}

void D3D12CommandList::BindIndexBuffer(BufferHandle h, Format fmt) {
    auto* e = device_.GetBuffer(h);
    D3D12_INDEX_BUFFER_VIEW ibv{};
    if (e && e->resource) {
        TransitionBuffer(*e, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        D3D12_GPU_VIRTUAL_ADDRESS base =
            hasFlag(e->desc.usage, BufferUsage::CpuWritable) && e->cpuWritableVA
                ? e->cpuWritableVA
                : e->resource->GetGPUVirtualAddress();
        ibv.BufferLocation = base;
        ibv.SizeInBytes    = static_cast<UINT>(e->desc.size);
        ibv.Format         = ToDXGI(fmt);
    }
    device_.GetCmdList()->IASetIndexBuffer(&ibv);
}

void D3D12CommandList::BindConstantBuffer(ShaderStage stage, uint32_t slot, BufferHandle h) {
    if (slot >= kRootCbvsPerStage) return;
    // Stash the handle; VA is resolved at draw time so that later MapBuffer
    // rename calls on the same buffer are picked up transparently.
    switch (stage) {
        case ShaderStage::Vertex:  cbvVs_[slot].buffer = h; break;
        case ShaderStage::Pixel:   cbvPs_[slot].buffer = h; break;
        case ShaderStage::Compute: cbvCs_[slot].buffer = h; break;
    }
}

void D3D12CommandList::PromoteSrv(BufferHandle h, ShaderStage stage) {
    auto* e = device_.GetBuffer(h);
    if (!e || !e->resource) return;
    D3D12_RESOURCE_STATES s = (stage == ShaderStage::Pixel)
        ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    TransitionBuffer(*e, s);
}

void D3D12CommandList::PromoteSrv(TextureHandle h, ShaderStage stage) {
    auto* e = device_.GetTexture(h);
    if (!e || !e->resource) return;
    D3D12_RESOURCE_STATES s = (stage == ShaderStage::Pixel)
        ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    TransitionTexture(*e, s);
}

void D3D12CommandList::BindShaderResource(ShaderStage stage, uint32_t slot, TextureHandle h) {
    if (slot >= kSrvsPerStage) return;
    auto* e = device_.GetTexture(h);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{0};
    if (e && e->hasSrv) {
        PromoteSrv(h, stage);
        cpu = e->srvCpu;
    }
    switch (stage) {
        case ShaderStage::Vertex:  srvVs_[slot] = cpu; break;
        case ShaderStage::Pixel:   srvPs_[slot] = cpu; break;
        case ShaderStage::Compute: srvCs_[slot] = cpu; break;
    }
}

void D3D12CommandList::BindShaderResource(ShaderStage stage, uint32_t slot, BufferHandle h) {
    if (slot >= kSrvsPerStage) return;
    auto* e = device_.GetBuffer(h);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{0};
    if (e && e->hasSrv) {
        PromoteSrv(h, stage);
        cpu = e->srvCpu;
    }
    switch (stage) {
        case ShaderStage::Vertex:  srvVs_[slot] = cpu; break;
        case ShaderStage::Pixel:   srvPs_[slot] = cpu; break;
        case ShaderStage::Compute: srvCs_[slot] = cpu; break;
    }
}

void D3D12CommandList::BindUnorderedAccess(uint32_t slot, BufferHandle h) {
    if (slot >= kUavsForCompute) return;
    auto* e = device_.GetBuffer(h);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{0};
    if (e && e->hasUav) {
        TransitionBuffer(*e, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cpu = e->uavCpu;
    }
    uavCs_[slot] = cpu;
}

void D3D12CommandList::BindSampler(ShaderStage stage, uint32_t slot, SamplerHandle h) {
    if (slot >= kSamplersPerStage) return;
    auto* e = device_.GetSampler(h);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{0};
    if (e && e->valid) cpu = e->samplerCpu;
    switch (stage) {
        case ShaderStage::Pixel:   samplerPs_[slot] = cpu; break;
        case ShaderStage::Compute: samplerCs_[slot] = cpu; break;
        default: break;
    }
}

// ============================================================================
// Descriptor materialization helpers
// ============================================================================

static void CopyDescriptorsOrNull(ID3D12Device* device,
                                   D3D12_CPU_DESCRIPTOR_HANDLE dstBase,
                                   uint32_t stride,
                                   const D3D12_CPU_DESCRIPTOR_HANDLE* src,
                                   uint32_t count,
                                   D3D12_CPU_DESCRIPTOR_HANDLE nullHandle,
                                   D3D12_DESCRIPTOR_HEAP_TYPE type) {
    for (uint32_t i = 0; i < count; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dst{};
        dst.ptr = dstBase.ptr + static_cast<SIZE_T>(i) * stride;
        D3D12_CPU_DESCRIPTOR_HANDLE s = src[i].ptr ? src[i] : nullHandle;
        device->CopyDescriptorsSimple(1, dst, s, type);
    }
}

// Resolve a CbvSlot's buffer handle to its current GPU VA. For CpuWritable
// buffers this re-reads the latest map-renamed upload-ring slice, so each
// Draw picks up the CB data the caller wrote just before it.
static D3D12_GPU_VIRTUAL_ADDRESS ResolveCbvVA(D3D12Device& device, BufferHandle h) {
    if (h == BufferHandle::Invalid) return 0;
    auto* e = device.GetBuffer(h);
    if (!e || !e->resource) return 0;
    if (hasFlag(e->desc.usage, BufferUsage::CpuWritable) && e->cpuWritableVA)
        return e->cpuWritableVA;
    return e->resource->GetGPUVirtualAddress();
}

void D3D12CommandList::ApplyGraphicsBindings() {
    auto* cmd = device_.GetCmdList();
    auto* dev = device_.GetDevice();

    // Root CBVs — resolve VA from handle each draw so renamed slices stick.
    for (uint32_t i = 0; i < kRootCbvsPerStage; ++i) {
        D3D12_GPU_VIRTUAL_ADDRESS va = ResolveCbvVA(device_, cbvVs_[i].buffer);
        if (va)
            cmd->SetGraphicsRootConstantBufferView(static_cast<UINT>(GraphicsRP::CBV_VS_0) + i, va);
        va = ResolveCbvVA(device_, cbvPs_[i].buffer);
        if (va)
            cmd->SetGraphicsRootConstantBufferView(static_cast<UINT>(GraphicsRP::CBV_PS_0) + i, va);
    }

    // SRV table VS
    {
        auto slice = device_.CbvSrvUavRing().Allocate(kSrvsPerStage);
        CopyDescriptorsOrNull(dev, slice.cpu, device_.CbvSrvUavRing().Stride(),
                               srvVs_.data(), kSrvsPerStage,
                               device_.GetNullSrv(),
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cmd->SetGraphicsRootDescriptorTable(static_cast<UINT>(GraphicsRP::SRV_TABLE_VS), slice.gpu);
    }
    // SRV table PS
    {
        auto slice = device_.CbvSrvUavRing().Allocate(kSrvsPerStage);
        CopyDescriptorsOrNull(dev, slice.cpu, device_.CbvSrvUavRing().Stride(),
                               srvPs_.data(), kSrvsPerStage,
                               device_.GetNullSrv(),
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cmd->SetGraphicsRootDescriptorTable(static_cast<UINT>(GraphicsRP::SRV_TABLE_PS), slice.gpu);
    }
    // Sampler table PS
    {
        auto slice = device_.SamplerRing().Allocate(kSamplersPerStage);
        CopyDescriptorsOrNull(dev, slice.cpu, device_.SamplerRing().Stride(),
                               samplerPs_.data(), kSamplersPerStage,
                               device_.GetNullSampler(),
                               D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        cmd->SetGraphicsRootDescriptorTable(static_cast<UINT>(GraphicsRP::SAMPLER_TABLE_PS), slice.gpu);
    }
}

void D3D12CommandList::ApplyComputeBindings() {
    auto* cmd = device_.GetCmdList();
    auto* dev = device_.GetDevice();

    for (uint32_t i = 0; i < kRootCbvsPerStage; ++i) {
        D3D12_GPU_VIRTUAL_ADDRESS va = ResolveCbvVA(device_, cbvCs_[i].buffer);
        if (va)
            cmd->SetComputeRootConstantBufferView(static_cast<UINT>(ComputeRP::CBV_0) + i, va);
    }

    // SRV table
    {
        auto slice = device_.CbvSrvUavRing().Allocate(kSrvsPerStage);
        CopyDescriptorsOrNull(dev, slice.cpu, device_.CbvSrvUavRing().Stride(),
                               srvCs_.data(), kSrvsPerStage,
                               device_.GetNullSrv(),
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cmd->SetComputeRootDescriptorTable(static_cast<UINT>(ComputeRP::SRV_TABLE), slice.gpu);
    }
    // UAV table
    {
        auto slice = device_.CbvSrvUavRing().Allocate(kUavsForCompute);
        CopyDescriptorsOrNull(dev, slice.cpu, device_.CbvSrvUavRing().Stride(),
                               uavCs_.data(), kUavsForCompute,
                               device_.GetNullUav(),
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cmd->SetComputeRootDescriptorTable(static_cast<UINT>(ComputeRP::UAV_TABLE), slice.gpu);
    }
    // Sampler table
    {
        auto slice = device_.SamplerRing().Allocate(kSamplersPerStage);
        CopyDescriptorsOrNull(dev, slice.cpu, device_.SamplerRing().Stride(),
                               samplerCs_.data(), kSamplersPerStage,
                               device_.GetNullSampler(),
                               D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        cmd->SetComputeRootDescriptorTable(static_cast<UINT>(ComputeRP::SAMPLER_TABLE), slice.gpu);
    }
}

// ============================================================================
// Standalone depth clear
// ============================================================================

void D3D12CommandList::ClearDepth(TextureHandle depth, float clearDepth, uint8_t clearStencil) {
    auto* e = device_.GetTexture(depth);
    if (!e || !e->hasDsv) return;
    TransitionTexture(*e, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    device_.GetCmdList()->ClearDepthStencilView(
        e->dsvCpu, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        clearDepth, clearStencil, 0, nullptr);
}

// ============================================================================
// Copy
// ============================================================================

void D3D12CommandList::CopyBuffer(BufferHandle dst, BufferHandle src) {
    auto* d = device_.GetBuffer(dst);
    auto* s = device_.GetBuffer(src);
    if (!d || !s || !d->resource || !s->resource) return;

    TransitionBuffer(*d, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionBuffer(*s, D3D12_RESOURCE_STATE_COPY_SOURCE);
    device_.GetCmdList()->CopyBufferRegion(d->resource, 0, s->resource, 0, s->desc.size);
}

// ============================================================================
// Draw / Dispatch
// ============================================================================

void D3D12CommandList::Draw(uint32_t vertexCount, uint32_t firstVertex) {
    if (!haveAnyPipeline_ || lastWasCompute_) return;
    ApplyGraphicsBindings();
    device_.GetCmdList()->DrawInstanced(vertexCount, 1, firstVertex, 0);
}

void D3D12CommandList::DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex) {
    if (!haveAnyPipeline_ || lastWasCompute_) return;
    ApplyGraphicsBindings();
    device_.GetCmdList()->DrawIndexedInstanced(indexCount, 1, firstIndex, baseVertex, 0);
}

void D3D12CommandList::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz) {
    if (!haveAnyPipeline_ || !lastWasCompute_) return;
    ApplyComputeBindings();
    device_.GetCmdList()->Dispatch(gx, gy, gz);
}

} // namespace WhiteoutDex::gfx::d3d12
