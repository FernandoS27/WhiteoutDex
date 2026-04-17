// ============================================================================
// D3D12 Backend — IGFXDevice implementation
// ============================================================================

#include "d3d12_device.h"
#include "d3d12_command_list.h"

#include <cstring>
#include <algorithm>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace WhiteoutDex::gfx::d3d12 {

// ============================================================================
// Lifecycle
// ============================================================================

D3D12Device::D3D12Device() = default;

D3D12Device::~D3D12Device() {
    // Make sure the GPU has stopped touching anything before we tear down.
    if (queue_ && fence_ && fenceEvent_) FlushGpu();

    immediateCtx_.reset();

    swapChains_.ForEach([](SwapChainEntry& e) { e.Release(); });
    swapChains_.Clear();
    samplers_.ForEach  ([](SamplerEntry& e)  { e.Release(); });
    samplers_.Clear();
    pipelines_.ForEach ([](PipelineEntry& e) { e.Release(); });
    pipelines_.Clear();
    shaders_.ForEach   ([](ShaderEntry& e)   { e.Release(); });
    shaders_.Clear();
    textures_.ForEach  ([](TextureEntry& e)  { e.Release(); });
    textures_.Clear();
    buffers_.ForEach   ([](BufferEntry& e)   { e.Release(); });
    buffers_.Clear();

    uploadRing_.Release();
    cbvSrvUavRing_.Release();
    samplerRing_.Release();
    cbvSrvUavPool_.Release();
    samplerPool_.Release();
    rtvPool_.Release();
    dsvPool_.Release();

    SafeRelease(graphicsRS_);
    SafeRelease(computeRS_);

    SafeRelease(cmdList_);
    for (auto& a : allocators_) SafeRelease(a);

    if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
    SafeRelease(fence_);
    SafeRelease(queue_);
    SafeRelease(device_);
    SafeRelease(factory_);
}

bool D3D12Device::Init() {
    if (!CreateDeviceAndQueue())   return false;
    if (!CreateCommandInfra())     return false;
    if (!CreateDescriptorPools())  return false;
    if (!CreateRootSignatures())   return false;
    if (!CreateNullDescriptors())  return false;
    if (!OpenCommandList())        return false;

    immediateCtx_ = std::make_unique<D3D12CommandList>(*this);
    return true;
}

bool D3D12Device::CreateDeviceAndQueue() {
    UINT factoryFlags = 0;

#ifdef _DEBUG
    {
        ID3D12Debug* debug = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            debug->Release();
            factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) return false;

    // Enumerate adapters, prefer discrete GPU by dedicated VRAM.
    IDXGIAdapter1* bestAdapter = nullptr;
    SIZE_T bestVRAM = 0;
    {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter->Release(); continue; }
            // Probe D3D12 feature level 11_0 before committing.
            if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                            __uuidof(ID3D12Device), nullptr))) {
                if (desc.DedicatedVideoMemory > bestVRAM) {
                    if (bestAdapter) bestAdapter->Release();
                    bestAdapter = adapter;
                    bestVRAM    = desc.DedicatedVideoMemory;
                    continue;
                }
            }
            adapter->Release();
        }
    }

    hr = D3D12CreateDevice(bestAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
    if (FAILED(hr)) { if (bestAdapter) bestAdapter->Release(); return false; }

    if (bestAdapter) {
        DXGI_ADAPTER_DESC1 desc{};
        bestAdapter->GetDesc1(&desc);
        char name[256]{};
        size_t converted = 0;
        wcstombs_s(&converted, name, sizeof(name), desc.Description, _TRUNCATE);
        deviceName_ = name;
        bestAdapter->Release();
    }

#ifdef _DEBUG
    {
        ID3D12InfoQueue* iq = nullptr;
        if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&iq)))) {
            iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      TRUE);
            iq->Release();
        }
    }
#endif

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    qd.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_));
    return SUCCEEDED(hr);
}

bool D3D12Device::CreateCommandInfra() {
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        HRESULT hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i]));
        if (FAILED(hr)) return false;
    }

    HRESULT hr = device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0], nullptr,
        IID_PPV_ARGS(&cmdList_));
    if (FAILED(hr)) return false;
    cmdListOpen_ = true;

    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    if (FAILED(hr)) return false;

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return fenceEvent_ != nullptr;
}

bool D3D12Device::CreateDescriptorPools() {
    if (!rtvPool_.Init      (device_, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,          1024)) return false;
    if (!dsvPool_.Init      (device_, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,          256))  return false;
    if (!cbvSrvUavPool_.Init(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,  16384)) return false;
    if (!samplerPool_.Init  (device_, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,      2048)) return false;

    if (!cbvSrvUavRing_.Init(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536)) return false;
    if (!samplerRing_.Init  (device_, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,     2048))  return false;

    if (!uploadRing_.Init(device_, 64 * 1024 * 1024)) return false;
    return true;
}

bool D3D12Device::CreateRootSignatures() {
    // ---- Graphics root signature ----
    {
        D3D12_DESCRIPTOR_RANGE srvRangeVs{};
        srvRangeVs.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRangeVs.NumDescriptors     = kSrvsPerStage;
        srvRangeVs.BaseShaderRegister = 0;
        srvRangeVs.RegisterSpace      = 0;
        srvRangeVs.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE srvRangePs = srvRangeVs;
        D3D12_DESCRIPTOR_RANGE samplerRangePs{};
        samplerRangePs.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRangePs.NumDescriptors     = kSamplersPerStage;
        samplerRangePs.BaseShaderRegister = 0;
        samplerRangePs.RegisterSpace      = 0;
        samplerRangePs.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[static_cast<uint32_t>(GraphicsRP::Count)] = {};

        // b0/b1 VS
        for (uint32_t i = 0; i < kRootCbvsPerStage; ++i) {
            auto& p = params[static_cast<uint32_t>(GraphicsRP::CBV_VS_0) + i];
            p.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
            p.Descriptor.ShaderRegister = i;
            p.Descriptor.RegisterSpace  = 0;
            p.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        }
        // b0/b1 PS
        for (uint32_t i = 0; i < kRootCbvsPerStage; ++i) {
            auto& p = params[static_cast<uint32_t>(GraphicsRP::CBV_PS_0) + i];
            p.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
            p.Descriptor.ShaderRegister = i;
            p.Descriptor.RegisterSpace  = 0;
            p.ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        // SRV table VS
        {
            auto& p = params[static_cast<uint32_t>(GraphicsRP::SRV_TABLE_VS)];
            p.ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges   = &srvRangeVs;
            p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;
        }
        // SRV table PS
        {
            auto& p = params[static_cast<uint32_t>(GraphicsRP::SRV_TABLE_PS)];
            p.ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges   = &srvRangePs;
            p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        // Sampler table PS
        {
            auto& p = params[static_cast<uint32_t>(GraphicsRP::SAMPLER_TABLE_PS)];
            p.ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges   = &samplerRangePs;
            p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        }

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = static_cast<UINT>(GraphicsRP::Count);
        rsd.pParameters   = params;
        rsd.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr)) { SafeRelease(err); return false; }
        hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&graphicsRS_));
        SafeRelease(blob);
        SafeRelease(err);
        if (FAILED(hr)) return false;
    }

    // ---- Compute root signature ----
    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors     = kSrvsPerStage;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace      = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors     = kUavsForCompute;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace      = 0;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE samplerRange{};
        samplerRange.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors     = kSamplersPerStage;
        samplerRange.BaseShaderRegister = 0;
        samplerRange.RegisterSpace      = 0;
        samplerRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[static_cast<uint32_t>(ComputeRP::Count)] = {};

        for (uint32_t i = 0; i < kRootCbvsPerStage; ++i) {
            auto& p = params[static_cast<uint32_t>(ComputeRP::CBV_0) + i];
            p.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
            p.Descriptor.ShaderRegister = i;
            p.Descriptor.RegisterSpace  = 0;
            p.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        }
        {
            auto& p = params[static_cast<uint32_t>(ComputeRP::SRV_TABLE)];
            p.ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges   = &srvRange;
            p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }
        {
            auto& p = params[static_cast<uint32_t>(ComputeRP::UAV_TABLE)];
            p.ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges   = &uavRange;
            p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }
        {
            auto& p = params[static_cast<uint32_t>(ComputeRP::SAMPLER_TABLE)];
            p.ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = 1;
            p.DescriptorTable.pDescriptorRanges   = &samplerRange;
            p.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
        }

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = static_cast<UINT>(ComputeRP::Count);
        rsd.pParameters   = params;
        rsd.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        if (FAILED(hr)) { SafeRelease(err); return false; }
        hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                          IID_PPV_ARGS(&computeRS_));
        SafeRelease(blob);
        SafeRelease(err);
        if (FAILED(hr)) return false;
    }

    return true;
}

bool D3D12Device::CreateNullDescriptors() {
    nullSrv_ = cbvSrvUavPool_.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels     = 1;
    device_->CreateShaderResourceView(nullptr, &sd, nullSrv_);

    nullUav_ = cbvSrvUavPool_.Allocate();
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format                  = DXGI_FORMAT_R32_UINT;
    ud.ViewDimension           = D3D12_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements      = 1;
    device_->CreateUnorderedAccessView(nullptr, nullptr, &ud, nullUav_);

    nullSampler_ = samplerPool_.Allocate();
    D3D12_SAMPLER_DESC sampd{};
    sampd.Filter         = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampd.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampd.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampd.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampd.MaxLOD         = D3D12_FLOAT32_MAX;
    device_->CreateSampler(&sampd, nullSampler_);
    return true;
}

bool D3D12Device::OpenCommandList() {
    // Command list already created open for frame 0 in CreateCommandInfra.
    // Seed fences so frame 0 has nothing to wait on.
    frameFenceValues_[0] = 0;
    frameFenceValues_[1] = 0;
    frameIndex_          = 0;
    fenceValue_          = 0;
    return true;
}

// ============================================================================
// Fence helpers
// ============================================================================

void D3D12Device::WaitForFence(uint64_t value) {
    if (fence_->GetCompletedValue() < value) {
        fence_->SetEventOnCompletion(value, fenceEvent_);
        DWORD result = WaitForSingleObject(fenceEvent_, 5000);
        if (result == WAIT_TIMEOUT) {
            OutputDebugStringA("[D3D12] WaitForFence timeout — possible GPU hang\n");
        }
    }
}

void D3D12Device::DeferredRelease(IUnknown* obj) {
    if (!obj) return;
    // Park the raw COM pointer keyed to the current (not-yet-signaled) fence
    // value. The resource remains alive until that fence signals.
    pendingDeletes_.push_back({obj, fenceValue_ + 1});
}

void D3D12Device::FlushPendingDeletes(uint64_t completedFenceValue) {
    auto it = pendingDeletes_.begin();
    while (it != pendingDeletes_.end()) {
        if (it->fenceValue <= completedFenceValue) {
            if (it->obj) it->obj->Release();
            it = pendingDeletes_.erase(it);
        } else {
            ++it;
        }
    }
}

void D3D12Device::FlushGpu() {
    // Close and submit anything pending, then drain.
    if (cmdListOpen_) {
        cmdList_->Close();
        ID3D12CommandList* lists[] = { cmdList_ };
        queue_->ExecuteCommandLists(1, lists);
        cmdListOpen_ = false;
    }
    ++fenceValue_;
    queue_->Signal(fence_, fenceValue_);
    WaitForFence(fenceValue_);

    uint64_t completed = fence_->GetCompletedValue();
    uploadRing_.Retire(completed);
    cbvSrvUavRing_.Retire(completed);
    samplerRing_.Retire(completed);
    FlushPendingDeletes(completed);
}

// ============================================================================
// Buffer
// ============================================================================

BufferHandle D3D12Device::CreateBuffer(const BufferDesc& desc, const void* initial) {
    BufferEntry entry{};
    entry.desc = desc;

    // CpuWritable buffers live entirely in the upload ring via map-rename.
    // We still create a tiny placeholder UPLOAD resource so the handle has
    // identity, but it's never actually bound. Binds read from cpuWritableVA.
    const bool cpuWritable = hasFlag(desc.usage, BufferUsage::CpuWritable);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = cpuWritable ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = std::max<UINT64>(desc.size, 256);
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags            = D3D12_RESOURCE_FLAG_NONE;

    if (hasFlag(desc.usage, BufferUsage::UnorderedAccess))
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_RESOURCE_STATES initialState =
        cpuWritable ? D3D12_RESOURCE_STATE_GENERIC_READ
                    : D3D12_RESOURCE_STATE_COMMON;

    HRESULT hr = device_->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, initialState, nullptr,
        IID_PPV_ARGS(&entry.resource));
    if (FAILED(hr)) return BufferHandle::Invalid;
    entry.currentState = initialState;

    // Initial data upload for GpuWritable / immutable buffers.
    if (initial && !cpuWritable && desc.size > 0) {
        // Sub-allocate staging in upload ring, memcpy, issue CopyBufferRegion.
        auto alloc = uploadRing_.Allocate(desc.size);
        std::memcpy(alloc.cpu, initial, desc.size);

        // Transition dst to COPY_DEST.
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = entry.resource;
        b.Transition.StateBefore = entry.currentState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList_->ResourceBarrier(1, &b);

        cmdList_->CopyBufferRegion(entry.resource, 0,
                                   alloc.resource, alloc.offset, desc.size);

        D3D12_RESOURCE_STATES postState = D3D12_RESOURCE_STATE_COMMON;
        if (hasFlag(desc.usage, BufferUsage::Vertex) ||
            hasFlag(desc.usage, BufferUsage::Index) ||
            hasFlag(desc.usage, BufferUsage::Constant))
            postState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if (hasFlag(desc.usage, BufferUsage::ShaderResource))
            postState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = postState;
        cmdList_->ResourceBarrier(1, &b);
        entry.currentState = postState;
    } else if (initial && cpuWritable && desc.size > 0) {
        // CpuWritable + initial data: seed the placeholder via Map.
        D3D12_RANGE nr{0, 0};
        void* p = nullptr;
        if (SUCCEEDED(entry.resource->Map(0, &nr, &p))) {
            std::memcpy(p, initial, desc.size);
            entry.resource->Unmap(0, nullptr);
            entry.cpuWritableVA = entry.resource->GetGPUVirtualAddress();
        }
    }

    // Structured-buffer SRV
    if (hasFlag(desc.usage, BufferUsage::ShaderResource) && desc.elementStride > 0) {
        entry.srvCpu = cbvSrvUavPool_.Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                     = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.FirstElement        = 0;
        sd.Buffer.NumElements         = static_cast<UINT>(desc.size / desc.elementStride);
        sd.Buffer.StructureByteStride = desc.elementStride;
        device_->CreateShaderResourceView(entry.resource, &sd, entry.srvCpu);
        entry.hasSrv = true;
    }

    // Structured-buffer UAV
    if (hasFlag(desc.usage, BufferUsage::UnorderedAccess) && desc.elementStride > 0) {
        entry.uavCpu = cbvSrvUavPool_.Allocate();
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format                     = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension              = D3D12_UAV_DIMENSION_BUFFER;
        ud.Buffer.FirstElement        = 0;
        ud.Buffer.NumElements         = static_cast<UINT>(desc.size / desc.elementStride);
        ud.Buffer.StructureByteStride = desc.elementStride;
        device_->CreateUnorderedAccessView(entry.resource, nullptr, &ud, entry.uavCpu);
        entry.hasUav = true;
    }

    return static_cast<BufferHandle>(buffers_.Insert(std::move(entry)));
}

void D3D12Device::Destroy(BufferHandle h) {
    if (h == BufferHandle::Invalid) return;
    auto* e = buffers_.Get(static_cast<uint64_t>(h));
    if (e) {
        if (e->hasSrv) cbvSrvUavPool_.Free(e->srvCpu);
        if (e->hasUav) cbvSrvUavPool_.Free(e->uavCpu);
        // Defer the actual ID3D12Resource release — the GPU may still be
        // reading/writing this buffer from a pending frame's command list.
        DeferredRelease(e->resource);
        e->resource       = nullptr;
        e->cpuWritableVA  = 0;
        e->cpuWritablePtr = nullptr;
    }
    buffers_.Remove(static_cast<uint64_t>(h));
}

void D3D12Device::UpdateBuffer(BufferHandle h, const void* data, size_t size) {
    auto* e = buffers_.Get(static_cast<uint64_t>(h));
    if (!e || size == 0) return;

    if (hasFlag(e->desc.usage, BufferUsage::CpuWritable)) {
        // Map-rename: fresh upload-ring slice.
        uint64_t align = hasFlag(e->desc.usage, BufferUsage::Constant) ? 256 : 16;
        if (e->desc.elementStride > 0)
            align = std::max<uint64_t>(align, e->desc.elementStride);
        auto alloc = uploadRing_.Allocate(size, align);
        std::memcpy(alloc.cpu, data, size);
        e->cpuWritableVA  = alloc.gpu;
        e->cpuWritablePtr = alloc.cpu;
        // Re-point the structured SRV at the fresh ring slice so shaders see
        // the updated data. (The SRV CPU descriptor is re-used; the next bind
        // copies the refreshed descriptor into the shader-visible heap.)
        if (e->hasSrv && e->desc.elementStride > 0) {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format                     = DXGI_FORMAT_UNKNOWN;
            sd.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
            sd.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Buffer.FirstElement        = alloc.offset / e->desc.elementStride;
            sd.Buffer.NumElements         = static_cast<UINT>(size / e->desc.elementStride);
            sd.Buffer.StructureByteStride = e->desc.elementStride;
            device_->CreateShaderResourceView(alloc.resource, &sd, e->srvCpu);
        }
    } else {
        // DEFAULT-heap buffer: stage in upload ring, CopyBufferRegion on the open list.
        auto alloc = uploadRing_.Allocate(size);
        std::memcpy(alloc.cpu, data, size);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = e->resource;
        b.Transition.StateBefore = e->currentState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if (e->currentState != D3D12_RESOURCE_STATE_COPY_DEST)
            cmdList_->ResourceBarrier(1, &b);
        cmdList_->CopyBufferRegion(e->resource, 0, alloc.resource, alloc.offset, size);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = e->currentState;
        if (e->currentState != D3D12_RESOURCE_STATE_COPY_DEST)
            cmdList_->ResourceBarrier(1, &b);
    }
}

void* D3D12Device::MapBuffer(BufferHandle h) {
    auto* e = buffers_.Get(static_cast<uint64_t>(h));
    if (!e) return nullptr;
    if (!hasFlag(e->desc.usage, BufferUsage::CpuWritable)) return nullptr;

    uint64_t align = hasFlag(e->desc.usage, BufferUsage::Constant) ? 256 : 16;
    if (e->desc.elementStride > 0)
        align = std::max<uint64_t>(align, e->desc.elementStride);
    auto alloc = uploadRing_.Allocate(e->desc.size, align);
    e->cpuWritableVA  = alloc.gpu;
    e->cpuWritablePtr = alloc.cpu;

    // For structured CpuWritable buffers (e.g. bone palette), the SRV must
    // be re-pointed at the fresh ring slice — otherwise shaders read the
    // original (empty) placeholder resource.
    if (e->hasSrv && e->desc.elementStride > 0) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                     = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.FirstElement        = alloc.offset / e->desc.elementStride;
        sd.Buffer.NumElements         = static_cast<UINT>(e->desc.size / e->desc.elementStride);
        sd.Buffer.StructureByteStride = e->desc.elementStride;
        device_->CreateShaderResourceView(alloc.resource, &sd, e->srvCpu);
    }
    return alloc.cpu;
}

void D3D12Device::UnmapBuffer(BufferHandle) {
    // No-op: upload ring writes are visible immediately on the next submit;
    // cpuWritableVA is already wired up by MapBuffer.
}

// ============================================================================
// Texture
// ============================================================================

TextureHandle D3D12Device::CreateTexture(const TextureDesc& desc, const void* initialPixels) {
    TextureEntry entry{};
    entry.desc = desc;

    const bool isDepth = hasFlag(desc.usage, TextureUsage::DepthStencil);
    const bool isRt    = hasFlag(desc.usage, TextureUsage::RenderTarget);
    const bool isSrv   = hasFlag(desc.usage, TextureUsage::ShaderResource);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width              = static_cast<UINT64>(desc.width);
    rd.Height             = static_cast<UINT>(desc.height);
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = static_cast<UINT16>(desc.mipLevels == 0 ? 1 : desc.mipLevels);
    rd.Format             = isDepth ? DepthFormatToTypeless(desc.format) : ToDXGI(desc.format);
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags              = D3D12_RESOURCE_FLAG_NONE;

    if (isRt)    rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (isDepth) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (isDepth && !isSrv)
        rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = ToDXGI(desc.format);
    D3D12_CLEAR_VALUE* pClear = nullptr;
    if (isRt) {
        clear.Color[0] = clear.Color[1] = clear.Color[2] = clear.Color[3] = 0.0f;
        pClear = &clear;
    } else if (isDepth) {
        clear.DepthStencil.Depth   = 1.0f;
        clear.DepthStencil.Stencil = 0;
        pClear = &clear;
    }

    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    if (initialPixels) initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    else if (isRt)     initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    else if (isDepth)  initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    HRESULT hr = device_->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd, initialState, pClear,
        IID_PPV_ARGS(&entry.resource));
    if (FAILED(hr)) return TextureHandle::Invalid;
    entry.currentState = initialState;

    // Initial-pixel upload for regular sampled textures.
    if (initialPixels) {
        UINT64 uploadSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        device_->GetCopyableFootprints(&rd, 0, 1, 0, &layout, nullptr, nullptr, &uploadSize);

        auto alloc = uploadRing_.Allocate(uploadSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

        // Copy row-by-row into the upload ring respecting row pitch.
        const uint32_t bpp     = FormatByteSize(desc.format);
        const uint32_t srcPitch = static_cast<uint32_t>(desc.width) * bpp;
        uint8_t* dst = static_cast<uint8_t*>(alloc.cpu);
        const uint8_t* src = static_cast<const uint8_t*>(initialPixels);
        for (int y = 0; y < desc.height; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * layout.Footprint.RowPitch,
                        src + static_cast<size_t>(y) * srcPitch, srcPitch);
        }

        layout.Offset = alloc.offset;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource        = entry.resource;
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource       = alloc.resource;
        srcLoc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = layout;

        cmdList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = entry.resource;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList_->ResourceBarrier(1, &b);
        entry.currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (isSrv) {
        entry.srvCpu = cbvSrvUavPool_.Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                    = isDepth ? DepthFormatToSrvFormat(desc.format)
                                               : ToDXGI(desc.format);
        sd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels       = desc.mipLevels == 0 ? static_cast<UINT>(-1)
                                                           : static_cast<UINT>(desc.mipLevels);
        device_->CreateShaderResourceView(entry.resource, &sd, entry.srvCpu);
        entry.hasSrv = true;
    }

    if (isRt) {
        entry.rtvCpu = rtvPool_.Allocate();
        device_->CreateRenderTargetView(entry.resource, nullptr, entry.rtvCpu);
        entry.hasRtv = true;
    }

    if (isDepth) {
        entry.dsvCpu = dsvPool_.Allocate();
        D3D12_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format        = ToDXGI(desc.format);
        dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(entry.resource, &dd, entry.dsvCpu);
        entry.hasDsv = true;
    }

    return static_cast<TextureHandle>(textures_.Insert(std::move(entry)));
}

void D3D12Device::Destroy(TextureHandle h) {
    if (h == TextureHandle::Invalid) return;
    auto* e = textures_.Get(static_cast<uint64_t>(h));
    if (e) {
        if (e->hasSrv) cbvSrvUavPool_.Free(e->srvCpu);
        if (e->hasRtv) rtvPool_.Free(e->rtvCpu);
        if (e->hasDsv) dsvPool_.Free(e->dsvCpu);
        // Only defer-release when we own the resource (swap-chain proxies
        // borrow from SwapChainEntry and are released with the swap chain).
        if (e->ownsResource) {
            DeferredRelease(e->resource);
            e->resource = nullptr;
        } else {
            e->resource = nullptr;
        }
    }
    textures_.Remove(static_cast<uint64_t>(h));
}

TextureHandle D3D12Device::CreateColorTarget(int w, int h, Format f) {
    TextureDesc desc{};
    desc.width     = w;
    desc.height    = h;
    desc.mipLevels = 1;
    desc.format    = f;
    desc.usage     = TextureUsage::RenderTarget | TextureUsage::ShaderResource;
    return CreateTexture(desc, nullptr);
}

TextureHandle D3D12Device::CreateDepthTarget(int w, int h, Format f) {
    TextureDesc desc{};
    desc.width     = w;
    desc.height    = h;
    desc.mipLevels = 1;
    desc.format    = f;
    desc.usage     = TextureUsage::DepthStencil;
    return CreateTexture(desc, nullptr);
}

// ============================================================================
// Shader
// ============================================================================

ShaderHandle D3D12Device::CreateShader(ShaderStage stage, const void* bytecode, size_t size) {
    ShaderEntry entry{};
    entry.stage = stage;
    entry.bytecode.assign(static_cast<const uint8_t*>(bytecode),
                          static_cast<const uint8_t*>(bytecode) + size);
    return static_cast<ShaderHandle>(shaders_.Insert(std::move(entry)));
}

void D3D12Device::Destroy(ShaderHandle h) {
    if (h == ShaderHandle::Invalid) return;
    auto* e = shaders_.Get(static_cast<uint64_t>(h));
    if (e) e->Release();
    shaders_.Remove(static_cast<uint64_t>(h));
}

// ============================================================================
// Pipeline
// ============================================================================

PipelineHandle D3D12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    auto* vs = shaders_.Get(static_cast<uint64_t>(desc.vs));
    auto* ps = shaders_.Get(static_cast<uint64_t>(desc.ps));
    if (!vs) return PipelineHandle::Invalid;

    // Build input layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> elems;
    elems.reserve(desc.inputLayout.size());
    for (const auto& ie : desc.inputLayout) {
        D3D12_INPUT_ELEMENT_DESC d{};
        d.SemanticName         = ie.semantic;
        d.SemanticIndex        = ie.semanticIndex;
        d.Format               = ToDXGI(ie.format);
        d.InputSlot            = 0;
        d.AlignedByteOffset    = ie.offset;
        d.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        d.InstanceDataStepRate = 0;
        elems.push_back(d);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = graphicsRS_;

    pd.VS.pShaderBytecode = vs->bytecode.data();
    pd.VS.BytecodeLength  = vs->bytecode.size();
    if (ps) {
        pd.PS.pShaderBytecode = ps->bytecode.data();
        pd.PS.BytecodeLength  = ps->bytecode.size();
    }

    // Blend
    D3D12_BLEND_DESC bd{};
    bd.AlphaToCoverageEnable  = desc.blend.alphaToCoverage ? TRUE : FALSE;
    bd.IndependentBlendEnable = FALSE;
    auto& rt0 = bd.RenderTarget[0];
    rt0.BlendEnable           = desc.blend.enable ? TRUE : FALSE;
    rt0.LogicOpEnable         = FALSE;
    rt0.SrcBlend              = ToD3D12(desc.blend.srcColor);
    rt0.DestBlend             = ToD3D12(desc.blend.dstColor);
    rt0.BlendOp               = ToD3D12(desc.blend.opColor);
    rt0.SrcBlendAlpha         = ToD3D12(desc.blend.srcAlpha);
    rt0.DestBlendAlpha        = ToD3D12(desc.blend.dstAlpha);
    rt0.BlendOpAlpha          = ToD3D12(desc.blend.opAlpha);
    rt0.LogicOp               = D3D12_LOGIC_OP_NOOP;
    rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.BlendState = bd;
    pd.SampleMask = UINT_MAX;

    // Rasterizer
    D3D12_RASTERIZER_DESC rs{};
    rs.FillMode              = ToD3D12(desc.rasterizer.fill);
    rs.CullMode              = ToD3D12(desc.rasterizer.cull);
    rs.FrontCounterClockwise = desc.rasterizer.frontCCW ? TRUE : FALSE;
    rs.DepthClipEnable       = TRUE;
    rs.AntialiasedLineEnable = TRUE;
    rs.MultisampleEnable     = FALSE;
    rs.ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pd.RasterizerState = rs;

    // Depth-stencil
    D3D12_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable      = desc.depthStencil.depthTest ? TRUE : FALSE;
    dsd.DepthWriteMask   = desc.depthStencil.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                                        : D3D12_DEPTH_WRITE_MASK_ZERO;
    dsd.DepthFunc        = ToD3D12(desc.depthStencil.depthCompare);
    dsd.StencilEnable    = FALSE;
    pd.DepthStencilState = dsd;

    pd.InputLayout.pInputElementDescs = elems.data();
    pd.InputLayout.NumElements        = static_cast<UINT>(elems.size());
    pd.PrimitiveTopologyType          = ToD3D12TopologyType(desc.topology);
    pd.NumRenderTargets               = 1;
    pd.RTVFormats[0]                  = ToDXGI(desc.rtvFormat);
    pd.DSVFormat                      = ToDXGI(desc.dsvFormat);
    pd.SampleDesc.Count               = 1;

    PipelineEntry entry{};
    entry.isCompute = false;
    entry.topology  = ToD3D12Topology(desc.topology);
    HRESULT hr = device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&entry.pso));
    if (FAILED(hr)) return PipelineHandle::Invalid;

    return static_cast<PipelineHandle>(pipelines_.Insert(std::move(entry)));
}

PipelineHandle D3D12Device::CreateComputePipeline(const ComputePipelineDesc& desc) {
    auto* cs = shaders_.Get(static_cast<uint64_t>(desc.cs));
    if (!cs) return PipelineHandle::Invalid;

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature     = computeRS_;
    pd.CS.pShaderBytecode = cs->bytecode.data();
    pd.CS.BytecodeLength  = cs->bytecode.size();

    PipelineEntry entry{};
    entry.isCompute = true;
    HRESULT hr = device_->CreateComputePipelineState(&pd, IID_PPV_ARGS(&entry.pso));
    if (FAILED(hr)) return PipelineHandle::Invalid;

    return static_cast<PipelineHandle>(pipelines_.Insert(std::move(entry)));
}

void D3D12Device::Destroy(PipelineHandle h) {
    if (h == PipelineHandle::Invalid) return;
    auto* e = pipelines_.Get(static_cast<uint64_t>(h));
    if (e) e->Release();
    pipelines_.Remove(static_cast<uint64_t>(h));
}

// ============================================================================
// Sampler
// ============================================================================

SamplerHandle D3D12Device::CreateSampler(const SamplerDesc& desc) {
    D3D12_SAMPLER_DESC sd{};
    sd.Filter         = ToD3D12Filter(desc.minFilter, desc.magFilter);
    sd.AddressU       = ToD3D12(desc.addressU);
    sd.AddressV       = ToD3D12(desc.addressV);
    sd.AddressW       = ToD3D12(desc.addressW);
    sd.MaxAnisotropy  = 1;
    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sd.MaxLOD         = D3D12_FLOAT32_MAX;

    SamplerEntry entry{};
    entry.samplerCpu = samplerPool_.Allocate();
    entry.valid      = true;
    device_->CreateSampler(&sd, entry.samplerCpu);

    return static_cast<SamplerHandle>(samplers_.Insert(std::move(entry)));
}

void D3D12Device::Destroy(SamplerHandle h) {
    if (h == SamplerHandle::Invalid) return;
    auto* e = samplers_.Get(static_cast<uint64_t>(h));
    if (e) {
        if (e->valid) samplerPool_.Free(e->samplerCpu);
        e->Release();
    }
    samplers_.Remove(static_cast<uint64_t>(h));
}

// ============================================================================
// Swap chain
// ============================================================================

SwapChainHandle D3D12Device::CreateSwapChain(void* nativeWindowHandle,
                                              int width, int height,
                                              Format colorFormat) {
    SwapChainEntry entry{};
    entry.hwnd        = static_cast<HWND>(nativeWindowHandle);
    entry.colorFormat = colorFormat;

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width       = static_cast<UINT>(width);
    scd.Height      = static_cast<UINT>(height);
    scd.Format      = ToDXGI(colorFormat);
    scd.Stereo      = FALSE;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = kFramesInFlight;
    scd.Scaling     = DXGI_SCALING_STRETCH;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
    scd.Flags       = 0;

    IDXGISwapChain1* sc1 = nullptr;
    HRESULT hr = factory_->CreateSwapChainForHwnd(queue_, entry.hwnd, &scd,
                                                   nullptr, nullptr, &sc1);
    if (FAILED(hr)) return SwapChainHandle::Invalid;

    hr = sc1->QueryInterface(IID_PPV_ARGS(&entry.swapChain));
    sc1->Release();
    if (FAILED(hr)) return SwapChainHandle::Invalid;

    factory_->MakeWindowAssociation(entry.hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Pre-allocate the proxy texture slot (contents populated by RefreshProxyTexture).
    TextureEntry proxy{};
    proxy.ownsResource = false;
    proxy.desc.width  = width;
    proxy.desc.height = height;
    proxy.desc.format = colorFormat;
    proxy.desc.usage  = TextureUsage::RenderTarget;
    entry.proxyTexHandle = textures_.Insert(std::move(proxy));

    // Acquire per-buffer resources + RTVs.
    for (UINT i = 0; i < kFramesInFlight; ++i) {
        entry.swapChain->GetBuffer(i, IID_PPV_ARGS(&entry.backBuffers[i]));
        entry.backBufferRtvs[i] = rtvPool_.Allocate();
        device_->CreateRenderTargetView(entry.backBuffers[i], nullptr, entry.backBufferRtvs[i]);
    }

    entry.currentBackBufferIndex = entry.swapChain->GetCurrentBackBufferIndex();

    uint64_t h = swapChains_.Insert(std::move(entry));
    RefreshProxyTexture(*swapChains_.Get(h));
    return static_cast<SwapChainHandle>(h);
}

void D3D12Device::RefreshProxyTexture(SwapChainEntry& sc) {
    auto* proxy = textures_.Get(sc.proxyTexHandle);
    if (!proxy) return;
    UINT idx          = sc.currentBackBufferIndex;
    proxy->resource   = sc.backBuffers[idx];
    proxy->rtvCpu     = sc.backBufferRtvs[idx];
    proxy->hasRtv     = true;
    // After Present, DXGI puts the newly-acquired back buffer in PRESENT state.
    proxy->currentState = D3D12_RESOURCE_STATE_PRESENT;
}

void D3D12Device::ResizeSwapChain(SwapChainHandle h, int width, int height) {
    auto* sc = swapChains_.Get(static_cast<uint64_t>(h));
    if (!sc || !sc->swapChain) return;

    FlushGpu();

    // Detach proxy, release back buffers, resize, re-acquire.
    auto* proxy = textures_.Get(sc->proxyTexHandle);
    if (proxy) {
        proxy->resource = nullptr;
        proxy->hasRtv   = false;
    }
    sc->ReleaseBackBuffers();

    HRESULT hr = sc->swapChain->ResizeBuffers(kFramesInFlight,
                                               static_cast<UINT>(width),
                                               static_cast<UINT>(height),
                                               DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    for (UINT i = 0; i < kFramesInFlight; ++i) {
        sc->swapChain->GetBuffer(i, IID_PPV_ARGS(&sc->backBuffers[i]));
        device_->CreateRenderTargetView(sc->backBuffers[i], nullptr, sc->backBufferRtvs[i]);
    }
    sc->currentBackBufferIndex = sc->swapChain->GetCurrentBackBufferIndex();

    if (proxy) {
        proxy->desc.width  = width;
        proxy->desc.height = height;
    }
    RefreshProxyTexture(*sc);

    // Re-open the command list since FlushGpu closed it.
    allocators_[frameIndex_]->Reset();
    cmdList_->Reset(allocators_[frameIndex_], nullptr);
    cmdListOpen_ = true;
    if (immediateCtx_) immediateCtx_->OnFrameBegin();
}

void D3D12Device::DestroySwapChain(SwapChainHandle h) {
    if (h == SwapChainHandle::Invalid) return;
    auto* sc = swapChains_.Get(static_cast<uint64_t>(h));
    if (sc) {
        FlushGpu();
        auto* proxy = textures_.Get(sc->proxyTexHandle);
        if (proxy) { proxy->resource = nullptr; proxy->hasRtv = false; }
        textures_.Remove(sc->proxyTexHandle);
        sc->Release();
    }
    swapChains_.Remove(static_cast<uint64_t>(h));
}

void D3D12Device::Present(SwapChainHandle h) {
    auto* sc = swapChains_.Get(static_cast<uint64_t>(h));
    if (!sc || !sc->swapChain) return;

    // Ensure the current back buffer is transitioned to PRESENT.
    auto* proxy = textures_.Get(sc->proxyTexHandle);
    if (proxy && proxy->resource && proxy->currentState != D3D12_RESOURCE_STATE_PRESENT) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = proxy->resource;
        b.Transition.StateBefore = proxy->currentState;
        b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList_->ResourceBarrier(1, &b);
        proxy->currentState = D3D12_RESOURCE_STATE_PRESENT;
    }

    // Close & submit.
    cmdList_->Close();
    cmdListOpen_ = false;
    ID3D12CommandList* lists[] = { cmdList_ };
    queue_->ExecuteCommandLists(1, lists);

    // Present (vsync on).
    HRESULT hrPresent = sc->swapChain->Present(1, 0);
    if (FAILED(hrPresent)) {
        HRESULT removed = device_->GetDeviceRemovedReason();
        char msg[256]{};
        _snprintf_s(msg, sizeof(msg),
            "[D3D12] Present failed 0x%08X, deviceRemovedReason=0x%08X\n",
            hrPresent, removed);
        OutputDebugStringA(msg);
    }

    // Signal end-of-frame fence; rings record this value.
    ++fenceValue_;
    queue_->Signal(fence_, fenceValue_);
    frameFenceValues_[frameIndex_] = fenceValue_;

    uploadRing_.EndFrame(fenceValue_);
    cbvSrvUavRing_.EndFrame(fenceValue_);
    samplerRing_.EndFrame(fenceValue_);

    // Advance to the next frame's slot; wait on its previous fence so we can
    // safely reset its allocator.
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    WaitForFence(frameFenceValues_[frameIndex_]);

    uint64_t completed = fence_->GetCompletedValue();
    uploadRing_.Retire(completed);
    cbvSrvUavRing_.Retire(completed);
    samplerRing_.Retire(completed);
    FlushPendingDeletes(completed);

    uploadRing_.BeginFrame(fenceValue_ + 1);
    cbvSrvUavRing_.BeginFrame(fenceValue_ + 1);
    samplerRing_.BeginFrame(fenceValue_ + 1);

    allocators_[frameIndex_]->Reset();
    cmdList_->Reset(allocators_[frameIndex_], nullptr);
    cmdListOpen_ = true;

    // Advance swap-chain back-buffer index, refresh proxy.
    sc->currentBackBufferIndex = sc->swapChain->GetCurrentBackBufferIndex();
    RefreshProxyTexture(*sc);

    if (immediateCtx_) immediateCtx_->OnFrameBegin();
}

TextureHandle D3D12Device::GetSwapChainBackBuffer(SwapChainHandle h) {
    auto* sc = swapChains_.Get(static_cast<uint64_t>(h));
    if (!sc) return TextureHandle::Invalid;
    return static_cast<TextureHandle>(sc->proxyTexHandle);
}

// ============================================================================
// Command list
// ============================================================================

IGFXCommandList* D3D12Device::GetImmediateContext() {
    return immediateCtx_.get();
}

} // namespace WhiteoutDex::gfx::d3d12
