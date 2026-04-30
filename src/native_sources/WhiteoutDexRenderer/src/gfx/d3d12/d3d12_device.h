#pragma once
// ============================================================================
// D3D12 Backend — IGFXDevice implementation
// ============================================================================

#include "gfx/gfx.h"
#include "d3d12_resources.h"

#include <memory>
#include <string>

namespace WhiteoutDex::gfx::d3d12 {

class D3D12CommandList;

class D3D12Device final : public IGFXDevice {
public:
    D3D12Device();
    ~D3D12Device() override;

    bool Init();

    // -- IGFXDevice --
    BufferHandle   CreateBuffer (const BufferDesc&,  const void* initial) override;
    TextureHandle  CreateTexture(const TextureDesc&, const void* initialPixels) override;
    ShaderHandle   CreateShader (ShaderStage, const void* bytecode, size_t size) override;
    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc&) override;
    PipelineHandle CreateComputePipeline (const ComputePipelineDesc&) override;
    SamplerHandle  CreateSampler(const SamplerDesc&) override;

    void Destroy(BufferHandle)   override;
    void Destroy(TextureHandle)  override;
    void Destroy(ShaderHandle)   override;
    void Destroy(PipelineHandle) override;
    void Destroy(SamplerHandle)  override;

    void  UpdateBuffer(BufferHandle, const void* data, size_t size) override;
    void* MapBuffer   (BufferHandle) override;
    void  UnmapBuffer (BufferHandle) override;

    SwapChainHandle CreateSwapChain(void* nativeWindowHandle,
                                    int width, int height,
                                    Format colorFormat) override;
    void          ResizeSwapChain (SwapChainHandle, int width, int height) override;
    void          DestroySwapChain(SwapChainHandle) override;
    void          Present         (SwapChainHandle) override;
    TextureHandle GetSwapChainBackBuffer(SwapChainHandle) override;
    TextureHandle GetSwapChainBackBufferLinear(SwapChainHandle) override;

    TextureHandle CreateColorTarget(int w, int h, Format f) override;
    TextureHandle CreateDepthTarget(int w, int h, Format f) override;

    IGFXCommandList* GetImmediateContext() override;

    GfxApi      GetApi() const override { return GfxApi::D3D12; }
    const char* GetDeviceName() const override { return deviceName_.c_str(); }

    // ---- Internal accessors used by the command list ----
    ID3D12Device*              GetDevice()     const { return device_; }
    ID3D12GraphicsCommandList* GetCmdList()    const { return cmdList_; }
    ID3D12RootSignature*       GetGraphicsRS() const { return graphicsRS_; }
    ID3D12RootSignature*       GetComputeRS()  const { return computeRS_; }

    DescriptorHeapRing& CbvSrvUavRing() { return cbvSrvUavRing_; }
    DescriptorHeapRing& SamplerRing()   { return samplerRing_; }
    UploadRing&         Upload()        { return uploadRing_; }

    BufferEntry*    GetBuffer  (BufferHandle h)   { return buffers_.Get(static_cast<uint64_t>(h)); }
    TextureEntry*   GetTexture (TextureHandle h)  { return textures_.Get(static_cast<uint64_t>(h)); }
    PipelineEntry*  GetPipeline(PipelineHandle h) { return pipelines_.Get(static_cast<uint64_t>(h)); }
    SamplerEntry*   GetSampler (SamplerHandle h)  { return samplers_.Get(static_cast<uint64_t>(h)); }
    SwapChainEntry* GetSwapChain(SwapChainHandle h) { return swapChains_.Get(static_cast<uint64_t>(h)); }

    // Null descriptors (used to pad unbound slots in shader-visible tables).
    D3D12_CPU_DESCRIPTOR_HANDLE GetNullSrv() const { return nullSrv_; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetNullUav() const { return nullUav_; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetNullSampler() const { return nullSampler_; }

private:
    // ---- Init helpers ----
    bool CreateDeviceAndQueue();
    bool CreateCommandInfra();
    bool CreateDescriptorPools();
    bool CreateRootSignatures();
    bool CreateNullDescriptors();
    bool OpenCommandList();

    // ---- Frame / fence ----
    void FlushGpu();                       // Wait until all submitted work has completed.
    void WaitForFence(uint64_t value);

    // ---- Swap-chain proxy handling ----
    void RefreshProxyTexture(SwapChainEntry& sc);

    // ---- State ----
    ID3D12Device*              device_     = nullptr;
    IDXGIFactory4*             factory_    = nullptr;
    ID3D12CommandQueue*        queue_      = nullptr;

    ID3D12CommandAllocator*    allocators_[kFramesInFlight]{};
    ID3D12GraphicsCommandList* cmdList_    = nullptr;
    ID3D12Fence*               fence_      = nullptr;
    HANDLE                     fenceEvent_ = nullptr;
    uint64_t                   fenceValue_ = 0;
    uint64_t                   frameFenceValues_[kFramesInFlight]{};
    uint32_t                   frameIndex_ = 0;
    bool                       cmdListOpen_ = false;

    ID3D12RootSignature*       graphicsRS_ = nullptr;
    ID3D12RootSignature*       computeRS_  = nullptr;

    CpuDescriptorPool          rtvPool_;
    CpuDescriptorPool          dsvPool_;
    CpuDescriptorPool          cbvSrvUavPool_;       // CPU staging heap
    CpuDescriptorPool          samplerPool_;          // CPU staging heap

    DescriptorHeapRing         cbvSrvUavRing_;        // shader-visible
    DescriptorHeapRing         samplerRing_;          // shader-visible

    UploadRing                 uploadRing_;

    D3D12_CPU_DESCRIPTOR_HANDLE nullSrv_{0};
    D3D12_CPU_DESCRIPTOR_HANDLE nullUav_{0};
    D3D12_CPU_DESCRIPTOR_HANDLE nullSampler_{0};

    SlotMap<BufferEntry>    buffers_;
    SlotMap<TextureEntry>   textures_;
    SlotMap<ShaderEntry>    shaders_;
    SlotMap<PipelineEntry>  pipelines_;
    SlotMap<SamplerEntry>   samplers_;
    SlotMap<SwapChainEntry> swapChains_;

    // Deferred-release queue: D3D12 requires us to keep GPU-owned resources
    // alive until the GPU is done with them. Destroy() parks the raw COM
    // pointer here with the current fence value, and we do the actual Release
    // once that fence has signaled (polled in Present).
    struct PendingDelete {
        IUnknown* obj;
        uint64_t  fenceValue;
    };
    std::vector<PendingDelete> pendingDeletes_;
    void DeferredRelease(IUnknown* obj);
    void FlushPendingDeletes(uint64_t completedFenceValue);

    std::unique_ptr<D3D12CommandList> immediateCtx_;
    std::string                       deviceName_;
};

} // namespace WhiteoutDex::gfx::d3d12
