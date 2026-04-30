#pragma once
// ============================================================================
// D3D11 Backend — Slot-Map Resource Storage
//
// Each resource type gets a typed slot-map that maps opaque handles to
// D3D11 COM pointers. Handles carry a 16-bit generation counter in the
// upper bits to detect use-after-free.
// ============================================================================

#include "d3d11_translate.h"
#include "gfx/gfx.h"
#include <vector>
#include <cassert>

namespace WhiteoutDex::gfx::d3d11 {

// ============================================================================
// Handle encoding: upper 16 bits = generation, lower 48 bits = index+1
// Index 0 is reserved so that handle value 0 == Invalid.
// ============================================================================

static constexpr uint64_t kIndexBits = 48;
static constexpr uint64_t kIndexMask = (uint64_t{1} << kIndexBits) - 1;
static constexpr uint64_t kGenShift  = kIndexBits;

inline uint64_t MakeHandle(uint32_t index, uint16_t gen) {
    return (static_cast<uint64_t>(gen) << kGenShift) | static_cast<uint64_t>(index + 1);
}

inline uint32_t HandleIndex(uint64_t h) {
    return static_cast<uint32_t>((h & kIndexMask) - 1);
}

inline uint16_t HandleGen(uint64_t h) {
    return static_cast<uint16_t>(h >> kGenShift);
}

// ============================================================================
// SlotMap<Payload>
// ============================================================================

template<typename Payload>
class SlotMap {
public:
    struct Slot {
        Payload  data{};
        uint16_t generation = 1;
        bool     alive      = false;
    };

    // Allocate a slot, return raw handle value.
    uint64_t Insert(Payload&& p) {
        uint32_t idx;
        if (!freeList_.empty()) {
            idx = freeList_.back();
            freeList_.pop_back();
        } else {
            idx = static_cast<uint32_t>(slots_.size());
            slots_.push_back({});
        }
        auto& s   = slots_[idx];
        s.data     = std::move(p);
        s.alive    = true;
        return MakeHandle(idx, s.generation);
    }

    // Look up a slot by handle. Returns nullptr if invalid/stale.
    Payload* Get(uint64_t h) {
        if (h == 0) return nullptr;
        uint32_t idx = HandleIndex(h);
        if (idx >= slots_.size()) return nullptr;
        auto& s = slots_[idx];
        if (!s.alive || s.generation != HandleGen(h)) return nullptr;
        return &s.data;
    }

    const Payload* Get(uint64_t h) const {
        if (h == 0) return nullptr;
        uint32_t idx = HandleIndex(h);
        if (idx >= slots_.size()) return nullptr;
        auto& s = slots_[idx];
        if (!s.alive || s.generation != HandleGen(h)) return nullptr;
        return &s.data;
    }

    // Free a slot. Increments generation so stale handles are detected.
    void Remove(uint64_t h) {
        if (h == 0) return;
        uint32_t idx = HandleIndex(h);
        if (idx >= slots_.size()) return;
        auto& s = slots_[idx];
        if (!s.alive || s.generation != HandleGen(h)) return;
        s.data  = Payload{};
        s.alive = false;
        s.generation++;
        freeList_.push_back(idx);
    }

    // Iterate all live entries (for cleanup).
    template<typename Fn>
    void ForEach(Fn&& fn) {
        for (auto& s : slots_) {
            if (s.alive) fn(s.data);
        }
    }

    void Clear() {
        slots_.clear();
        freeList_.clear();
    }

private:
    std::vector<Slot>     slots_;
    std::vector<uint32_t> freeList_;
};

// ============================================================================
// D3D11 resource payloads
// ============================================================================

struct BufferEntry {
    ID3D11Buffer*              buffer = nullptr;
    ID3D11ShaderResourceView*  srv    = nullptr;   // non-null if ShaderResource
    ID3D11UnorderedAccessView* uav    = nullptr;   // non-null if UnorderedAccess
    BufferDesc                 desc{};

    void Release() {
        SafeRelease(uav);
        SafeRelease(srv);
        SafeRelease(buffer);
    }
};

struct TextureEntry {
    ID3D11Texture2D*           tex = nullptr;
    ID3D11ShaderResourceView*  srv = nullptr;
    ID3D11RenderTargetView*    rtv = nullptr;
    ID3D11DepthStencilView*    dsv = nullptr;
    TextureDesc                desc{};

    void Release() {
        SafeRelease(dsv);
        SafeRelease(rtv);
        SafeRelease(srv);
        SafeRelease(tex);
    }
};

struct ShaderEntry {
    ID3D11VertexShader*  vs = nullptr;
    ID3D11PixelShader*   ps = nullptr;
    ID3D11ComputeShader* cs = nullptr;
    ShaderStage          stage{};
    // Keep bytecode around for input layout creation
    std::vector<uint8_t> bytecode;

    void Release() {
        SafeRelease(vs);
        SafeRelease(ps);
        SafeRelease(cs);
        bytecode.clear();
    }
};

struct PipelineEntry {
    // Graphics pipeline baked state
    ID3D11BlendState*        blendState      = nullptr;
    ID3D11DepthStencilState* depthState      = nullptr;
    ID3D11RasterizerState*   rasterState     = nullptr;
    ID3D11InputLayout*       inputLayout     = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology        = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    bool                     alphaToCoverage = false;

    // Shader references (raw pointers; owned by their ShaderEntry)
    ID3D11VertexShader*  vs = nullptr;
    ID3D11PixelShader*   ps = nullptr;
    ID3D11ComputeShader* cs = nullptr;

    bool isCompute = false;

    void Release() {
        SafeRelease(inputLayout);
        SafeRelease(rasterState);
        SafeRelease(depthState);
        SafeRelease(blendState);
        // vs/ps/cs are not owned by the pipeline — owned by ShaderEntry
    }
};

struct SamplerEntry {
    ID3D11SamplerState* sampler = nullptr;

    void Release() { SafeRelease(sampler); }
};

struct SwapChainEntry {
    IDXGISwapChain*    swapChain = nullptr;
    ID3D11Texture2D*   backBuffer = nullptr;
    // RTV-view DXGI format. May differ from the swap-chain resource format
    // when a sRGB variant was requested; CreateSwapChainViews uses this on
    // every CreateRenderTargetView call so writes are gamma-encoded.
    DXGI_FORMAT        rtvDxgiFormat       = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    // Linear (non-sRGB) format used by the second RTV view of the same
    // back-buffer resource — see d3d12 swap-chain comment for rationale.
    DXGI_FORMAT        rtvDxgiFormatLinear = DXGI_FORMAT_R8G8B8A8_UNORM;
    // sRGB-encoding RTV proxy + non-sRGB linear RTV proxy. Both alias
    // the same physical back-buffer resource; only the RTV format
    // differs.
    uint64_t           backBufferTexHandle       = 0;
    uint64_t           backBufferTexHandleLinear = 0;

    void ReleaseBackBuffer() {
        SafeRelease(backBuffer);
    }
    void Release() {
        ReleaseBackBuffer();
        SafeRelease(swapChain);
    }
};

} // namespace WhiteoutDex::gfx::d3d11
