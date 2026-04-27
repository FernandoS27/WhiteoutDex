#pragma once
// ============================================================================
// D3D11 Backend — IGFXDevice implementation
// ============================================================================

#include "gfx/gfx.h"
#include "d3d11_resources.h"
#include <string>

namespace WhiteoutDex::gfx::d3d11 {

class D3D11CommandList;

class D3D11Device final : public IGFXDevice {
public:
    D3D11Device();
    ~D3D11Device() override;

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

    TextureHandle CreateColorTarget(int w, int h, Format f) override;
    TextureHandle CreateDepthTarget(int w, int h, Format f) override;

    IGFXCommandList* GetImmediateContext() override;

    GfxApi      GetApi() const override { return GfxApi::D3D11; }
    const char* GetDeviceName() const override { return deviceName_.c_str(); }

    // Internal accessors for command list
    ID3D11Device*        GetD3DDevice()  const { return device_; }
    ID3D11DeviceContext* GetD3DContext() const { return context_; }

    // Slot-map accessors for command list
    BufferEntry*   GetBuffer  (BufferHandle h)   { return buffers_.Get(static_cast<uint64_t>(h)); }
    TextureEntry*  GetTexture (TextureHandle h)  { return textures_.Get(static_cast<uint64_t>(h)); }
    PipelineEntry* GetPipeline(PipelineHandle h) { return pipelines_.Get(static_cast<uint64_t>(h)); }
    SamplerEntry*  GetSampler (SamplerHandle h)  { return samplers_.Get(static_cast<uint64_t>(h)); }

private:
    TextureHandle RegisterBackBuffer(ID3D11Texture2D* bb, DXGI_FORMAT rtvFormat);
    void CreateSwapChainViews(SwapChainEntry& sc);

    ID3D11Device*        device_  = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIFactory1*       factory_ = nullptr;

    SlotMap<BufferEntry>    buffers_;
    SlotMap<TextureEntry>   textures_;
    SlotMap<ShaderEntry>    shaders_;
    SlotMap<PipelineEntry>  pipelines_;
    SlotMap<SamplerEntry>   samplers_;
    SlotMap<SwapChainEntry> swapChains_;

    std::unique_ptr<D3D11CommandList> immediateCtx_;
    std::string deviceName_;
};

} // namespace WhiteoutDex::gfx::d3d11
