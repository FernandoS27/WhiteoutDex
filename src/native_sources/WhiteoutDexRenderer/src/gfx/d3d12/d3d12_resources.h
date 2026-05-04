#pragma once
// ============================================================================
// D3D12 Backend — Resource Payloads, SlotMap, Upload Ring, Descriptor Ring
//
// Same opaque-handle / slot-map pattern as the D3D11 backend, but with D3D12
// payloads and extra infrastructure to emulate D3D11's immediate-execution
// semantics on top of D3D12's explicit frame model:
//
//   - UploadRing        — map-discard-style sub-allocation for dynamic CBs / VBs
//                         and staging buffers for initial-data uploads.
//   - DescriptorHeapRing — per-frame shader-visible descriptor slice allocator.
// ============================================================================

#include "d3d12_translate.h"
#include "gfx/gfx.h"

#include <vector>
#include <array>
#include <cassert>
#include <cstring>

namespace WhiteoutDex::gfx::d3d12 {

// ============================================================================
// Frames in flight
// ============================================================================
inline constexpr uint32_t kFramesInFlight = 2;

// ============================================================================
// Root-signature layout (graphics)
//   [0]  Root CBV  b0  VS
//   [1]  Root CBV  b1  VS
//   [2]  Root CBV  b0  PS
//   [3]  Root CBV  b1  PS
//   [4]  Table SRV    t0..t7   VS
//   [5]  Table SRV    t0..t7   PS
//   [6]  Table Sampler s0..s3  PS
// Root-signature layout (compute)
//   [0]  Root CBV  b0  ALL
//   [1]  Root CBV  b1  ALL
//   [2]  Table SRV    t0..t7   ALL
//   [3]  Table UAV    u0..u3   ALL
//   [4]  Table Sampler s0..s3  ALL
// ============================================================================
// SRV slot 15 is reserved by HD shaders for the split-sum BRDF LUT; bump
// to 16 so HD draws can bind it without the command-list dropping the
// request. CB slot 2 (b2) is where HD VSPerDraw / PSPerDraw live per
// Wc3Shaders/types/cb_structs.slang, so we need at least three root CBVs
// per stage (b0, b1, b2). The SD pipeline only uses b0 today, so the
// extra slots are harmless for it.
//
// `kSamplersPerStage` stays at 4 -- D3D12's sampler descriptor heap is
// hard-capped at 2048 entries per heap, so allocating 16 per draw blows
// the ring within a single frame (wraps over GPU-live data -> TDR). HD
// needs samplers at s13/s14/s15 too, but those are always linear/default
// so the root signature declares them as STATIC samplers (off-heap) --
// see CreateRootSignatures in d3d12_device.cpp.
inline constexpr uint32_t kSrvsPerStage     = 16;
inline constexpr uint32_t kUavsForCompute   = 4;
inline constexpr uint32_t kSamplersPerStage = 4;
// Root CBVs per stage: b0..b3.
// b3 is the bone palette consumed by vs/hd.bls's FourBoneSkinning
// policy (ConstantBuffer<BonePalette> vsCB3 in Wc3Shaders/types/
// cb_structs.slang). Without slot b3 reachable from the root signature
// the bind silently drops and the HD VS reads garbage for skinning,
// collapsing every vertex to the origin.
inline constexpr uint32_t kRootCbvsPerStage = 4;

enum class GraphicsRP : uint32_t {
    CBV_VS_0 = 0, CBV_VS_1, CBV_VS_2, CBV_VS_3,
    CBV_PS_0,     CBV_PS_1, CBV_PS_2, CBV_PS_3,
    SRV_TABLE_VS,
    SRV_TABLE_PS,
    SAMPLER_TABLE_PS,
    Count
};
enum class ComputeRP : uint32_t {
    CBV_0 = 0, CBV_1, CBV_2, CBV_3,
    SRV_TABLE,
    UAV_TABLE,
    SAMPLER_TABLE,
    Count
};

// ============================================================================
// Handle encoding (identical to D3D11 backend)
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
// SlotMap<Payload> — identical shape to D3D11 backend
// ============================================================================
template<typename Payload>
class SlotMap {
public:
    struct Slot {
        Payload  data{};
        uint16_t generation = 1;
        bool     alive      = false;
    };

    uint64_t Insert(Payload&& p) {
        uint32_t idx;
        if (!freeList_.empty()) {
            idx = freeList_.back();
            freeList_.pop_back();
        } else {
            idx = static_cast<uint32_t>(slots_.size());
            slots_.push_back({});
        }
        auto& s = slots_[idx];
        s.data  = std::move(p);
        s.alive = true;
        return MakeHandle(idx, s.generation);
    }

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
// Resource payloads
// ============================================================================

struct BufferEntry {
    // For CpuWritable buffers the resource itself is only used as a fallback
    // identity; actual per-frame storage comes from the upload ring and the
    // cached (cpuWritableVA / cpuWritablePtr) fields below (map-rename).
    ID3D12Resource*          resource     = nullptr;
    D3D12_RESOURCE_STATES    currentState = D3D12_RESOURCE_STATE_COMMON;
    BufferDesc               desc{};

    // Cached per-frame rename slice (CpuWritable only).
    // Refreshed by MapBuffer / UpdateBuffer on every call.
    D3D12_GPU_VIRTUAL_ADDRESS cpuWritableVA  = 0;
    void*                     cpuWritablePtr = nullptr;

    // CPU-visible (non-shader-visible) descriptor staging slots.
    // Filled at CreateBuffer for structured SRV / UAV; copied to shader-visible
    // heap at bind time.
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{0};
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu{0};
    bool                        hasSrv = false;
    bool                        hasUav = false;

    void Release() {
        SafeRelease(resource);
        cpuWritableVA  = 0;
        cpuWritablePtr = nullptr;
    }
};

struct TextureEntry {
    ID3D12Resource*          resource     = nullptr;
    D3D12_RESOURCE_STATES    currentState = D3D12_RESOURCE_STATE_COMMON;
    TextureDesc              desc{};
    bool                     ownsResource = true;     // false for swap-chain proxy

    // Staging descriptor handles (CPU-only heaps).
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu{0};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu{0};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu{0};
    bool                        hasSrv = false;
    bool                        hasRtv = false;
    bool                        hasDsv = false;

    void Release() {
        if (ownsResource) SafeRelease(resource);
        else              resource = nullptr;
    }
};

struct ShaderEntry {
    std::vector<uint8_t> bytecode;
    ShaderStage          stage{};

    void Release() { bytecode.clear(); }
};

struct PipelineEntry {
    ID3D12PipelineState*     pso       = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY topology  = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    bool                     isCompute = false;

    void Release() { SafeRelease(pso); }
};

struct SamplerEntry {
    D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu{0};  // in D3D12Device's CPU sampler heap
    bool                         valid = false;

    void Release() { valid = false; samplerCpu = {0}; }
};

struct SwapChainEntry {
    IDXGISwapChain3*                          swapChain = nullptr;
    HWND                                      hwnd      = nullptr;
    Format                                    colorFormat = Format::R8G8B8A8_UNORM_SRGB;
    // RTV-view DXGI format. May differ from the swap-chain resource format
    // when the caller asked for an _SRGB variant — flip-model swap chains
    // require the resource to be the linear/raw form, so we keep the sRGB
    // form here and pass it as the explicit RTV-desc format on every
    // `CreateRenderTargetView` (see CreateSwapChain / ResizeSwapChain).
    DXGI_FORMAT                               rtvDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    UINT                                      currentBackBufferIndex = 0;

    // One real back-buffer resource per swap-chain buffer.
    std::array<ID3D12Resource*,             kFramesInFlight> backBuffers{};
    // sRGB-encoding RTV (rtvDxgiFormat above — usually `_UNORM_SRGB`).
    // Hardware encodes linear → sRGB on write; the HD tonemap output
    // targets these.
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kFramesInFlight> backBufferRtvs{};
    // Linear (raw UNORM) RTV view of the same back-buffer resource. SD
    // draws target these so display-ready sRGB-byte outputs land
    // verbatim in memory (no hardware re-encode). Same lifetime as the
    // sRGB RTVs — both come from rtvPool_ and are freed via FreeAll on
    // ReleaseBackBuffers / DestroySwapChain.
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kFramesInFlight> backBufferRtvsLinear{};
    // Linear DXGI format for the second RTV view. Computed from the
    // caller's colorFormat by stripping the sRGB suffix; equal to
    // `rtvDxgiFormat` when the caller already asked for a linear view.
    DXGI_FORMAT                             rtvDxgiFormatLinear = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Stable proxy texture handle handed out to the renderer. Its internal
    // resource/rtv/state fields are rewritten each Present to track the
    // current back-buffer index.
    uint64_t proxyTexHandle       = 0;
    uint64_t proxyTexHandleLinear = 0;   // alias with the linear RTV

    void ReleaseBackBuffers() {
        for (auto& bb : backBuffers) SafeRelease(bb);
    }
    void Release() {
        ReleaseBackBuffers();
        SafeRelease(swapChain);
    }
};

// ============================================================================
// UploadRing — single UPLOAD-heap buffer sub-allocator.
//
// Provides map-discard-style per-Map renames: each Allocate() returns a fresh
// 256-byte-aligned slice of the ring. Wraps at the end of the ring. Per-frame
// fencing is managed externally (by D3D12Device): before a new frame begins,
// the device calls OnFrameBegin(frameIdx) which advances the head fence value.
// If Allocate() would overrun the tail, we stall on the previous frame's fence.
// ============================================================================
class UploadRing {
public:
    bool Init(ID3D12Device* device, uint64_t sizeBytes);
    void Release();

    // Record the fence value a frame's allocations must be done by before
    // their bytes can be recycled.
    void BeginFrame(uint64_t nextFenceValue);
    void EndFrame(uint64_t signaledFenceValue);
    void Retire(uint64_t completedFenceValue);

    // Allocate a slice of `size` bytes, aligned to `alignment`. Returns the
    // mapped CPU pointer + GPU virtual address of that slice.
    struct Allocation {
        void*                     cpu = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
        ID3D12Resource*           resource = nullptr;   // the ring's resource
        uint64_t                  offset   = 0;
    };
    Allocation Allocate(uint64_t size, uint64_t alignment = 256);

private:
    struct Retired {
        uint64_t fenceValue = 0;
        uint64_t head       = 0;  // ring head at the time of BeginFrame
    };

    ID3D12Resource*           resource_ = nullptr;
    uint8_t*                  mapped_   = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpuBase_  = 0;
    uint64_t                  capacity_ = 0;
    uint64_t                  head_     = 0;
    uint64_t                  tail_     = 0;

    std::vector<Retired>      retiredQueue_;
    uint64_t                  currentFrameFence_ = 0;
};

// ============================================================================
// DescriptorHeapRing — shader-visible descriptor heap with ring allocation
// across frames (gated by fence). Callers stage CPU descriptors into a
// contiguous GPU slice at draw time.
// ============================================================================
class DescriptorHeapRing {
public:
    bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t totalSize);
    void Release();

    void BeginFrame(uint64_t nextFenceValue);
    void EndFrame(uint64_t signaledFenceValue);
    void Retire(uint64_t completedFenceValue);

    // Allocate `count` contiguous descriptors. Returns CPU + GPU handle to
    // the first. Caller then uses ID3D12Device::CopyDescriptors[Simple] to
    // populate the slice from CPU-staging descriptors.
    struct Slice {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{0};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{0};
    };
    Slice Allocate(uint32_t count);

    ID3D12DescriptorHeap* Heap() const { return heap_; }
    uint32_t              Stride() const { return stride_; }

private:
    struct Retired {
        uint64_t fenceValue = 0;
        uint32_t head       = 0;
    };

    ID3D12DescriptorHeap* heap_     = nullptr;
    uint32_t              stride_   = 0;
    uint32_t              capacity_ = 0;
    uint32_t              head_     = 0;
    uint32_t              tail_     = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuBase_{0};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuBase_{0};

    std::vector<Retired>  retiredQueue_;
    uint64_t              currentFrameFence_ = 0;
};

// ============================================================================
// CpuDescriptorPool — allocator into a non-shader-visible CPU heap.
// Free list reclaims slots on Destroy so long-running sessions don't exhaust
// the heap as resources are created and destroyed.
// ============================================================================
class CpuDescriptorPool {
public:
    bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity);
    void Release();

    D3D12_CPU_DESCRIPTOR_HANDLE Allocate();
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE h);
    uint32_t Stride() const { return stride_; }

private:
    ID3D12DescriptorHeap* heap_     = nullptr;
    uint32_t              stride_   = 0;
    uint32_t              capacity_ = 0;
    uint32_t              head_     = 0;
    std::vector<uint32_t> freeList_;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuBase_{0};
};

// ============================================================================
// Small helper: 256-byte CBV size alignment
// ============================================================================
inline uint64_t AlignUp(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

} // namespace WhiteoutDex::gfx::d3d12
