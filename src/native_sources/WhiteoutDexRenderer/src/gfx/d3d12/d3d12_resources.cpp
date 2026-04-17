// ============================================================================
// D3D12 Backend — UploadRing / DescriptorHeapRing / CpuDescriptorPool impls
// ============================================================================

#include "d3d12_resources.h"

namespace WhiteoutDex::gfx::d3d12 {

// ============================================================================
// UploadRing
// ============================================================================

bool UploadRing::Init(ID3D12Device* device, uint64_t sizeBytes) {
    capacity_ = sizeBytes;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = capacity_;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags              = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&resource_));
    if (FAILED(hr)) return false;

    D3D12_RANGE readRange{0, 0};
    hr = resource_->Map(0, &readRange, reinterpret_cast<void**>(&mapped_));
    if (FAILED(hr)) { SafeRelease(resource_); return false; }

    gpuBase_ = resource_->GetGPUVirtualAddress();
    head_    = 0;
    tail_    = 0;
    return true;
}

void UploadRing::Release() {
    if (resource_) {
        resource_->Unmap(0, nullptr);
        SafeRelease(resource_);
    }
    mapped_   = nullptr;
    gpuBase_  = 0;
    capacity_ = 0;
    head_     = 0;
    tail_     = 0;
    retiredQueue_.clear();
}

void UploadRing::BeginFrame(uint64_t nextFenceValue) {
    currentFrameFence_ = nextFenceValue;
}

void UploadRing::EndFrame(uint64_t signaledFenceValue) {
    retiredQueue_.push_back({signaledFenceValue, head_});
}

void UploadRing::Retire(uint64_t completedFenceValue) {
    // Every retired frame whose fence is reached can have its bytes recycled.
    // tail_ advances to the most recent completed frame's head snapshot.
    while (!retiredQueue_.empty() &&
           retiredQueue_.front().fenceValue <= completedFenceValue) {
        tail_ = retiredQueue_.front().head;
        retiredQueue_.erase(retiredQueue_.begin());
    }
}

UploadRing::Allocation UploadRing::Allocate(uint64_t size, uint64_t alignment) {
    Allocation out{};
    if (size == 0 || size > capacity_) return out;

    // head_ is a linear counter (never wrapped modulo). Physical offset is
    // head_ % capacity_. This keeps head_ monotonically increasing so
    // retiredQueue entries compare meaningfully and tail_ tracks correctly.
    uint64_t pos     = head_ % capacity_;
    uint64_t aligned = AlignUp(pos, alignment);
    uint64_t pad     = aligned - pos;

    // If the aligned slot would cross the end of the physical ring, skip
    // the remaining bytes and start at the top of the next ring window.
    if (aligned + size > capacity_) {
        pad    += (capacity_ - aligned);
        aligned = 0;
    }

    // No explicit stall: caller must ensure ring is sized so wrap interval
    // exceeds kFramesInFlight. With 64 MB and typical ~100 KB/frame usage,
    // wrap happens every ~650 frames — well beyond the 2-frame in-flight
    // window, so data at a wrapped-over slot is guaranteed GPU-idle.

    out.cpu      = mapped_ + aligned;
    out.gpu      = gpuBase_ + aligned;
    out.resource = resource_;
    out.offset   = aligned;
    head_       += pad + size;
    return out;
}

// ============================================================================
// DescriptorHeapRing
// ============================================================================

bool DescriptorHeapRing::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                               uint32_t totalSize) {
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = type;
    hd.NumDescriptors = totalSize;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_));
    if (FAILED(hr)) return false;

    stride_   = device->GetDescriptorHandleIncrementSize(type);
    capacity_ = totalSize;
    head_     = 0;
    tail_     = 0;
    cpuBase_  = heap_->GetCPUDescriptorHandleForHeapStart();
    gpuBase_  = heap_->GetGPUDescriptorHandleForHeapStart();
    return true;
}

void DescriptorHeapRing::Release() {
    SafeRelease(heap_);
    stride_   = 0;
    capacity_ = 0;
    head_     = 0;
    tail_     = 0;
    cpuBase_  = {0};
    gpuBase_  = {0};
    retiredQueue_.clear();
}

void DescriptorHeapRing::BeginFrame(uint64_t nextFenceValue) {
    currentFrameFence_ = nextFenceValue;
}

void DescriptorHeapRing::EndFrame(uint64_t signaledFenceValue) {
    retiredQueue_.push_back({signaledFenceValue, head_});
}

void DescriptorHeapRing::Retire(uint64_t completedFenceValue) {
    while (!retiredQueue_.empty() &&
           retiredQueue_.front().fenceValue <= completedFenceValue) {
        tail_ = retiredQueue_.front().head;
        retiredQueue_.erase(retiredQueue_.begin());
    }
}

DescriptorHeapRing::Slice DescriptorHeapRing::Allocate(uint32_t count) {
    Slice s{};
    if (count == 0 || count > capacity_) return s;

    // head_ is a linear (non-modulo) counter; physical index is head_ % capacity_.
    uint32_t pos = head_ % capacity_;
    uint32_t pad = 0;
    if (pos + count > capacity_) {
        pad = capacity_ - pos;
        pos = 0;
    }

    s.cpu.ptr = cpuBase_.ptr + static_cast<SIZE_T>(pos) * stride_;
    s.gpu.ptr = gpuBase_.ptr + static_cast<UINT64>(pos) * stride_;
    head_    += pad + count;
    return s;
}

// ============================================================================
// CpuDescriptorPool
// ============================================================================

bool CpuDescriptorPool::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                              uint32_t capacity) {
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = type;
    hd.NumDescriptors = capacity;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_));
    if (FAILED(hr)) return false;

    stride_   = device->GetDescriptorHandleIncrementSize(type);
    capacity_ = capacity;
    head_     = 0;
    cpuBase_  = heap_->GetCPUDescriptorHandleForHeapStart();
    return true;
}

void CpuDescriptorPool::Release() {
    SafeRelease(heap_);
    stride_   = 0;
    capacity_ = 0;
    head_     = 0;
    cpuBase_  = {0};
    freeList_.clear();
}

D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptorPool::Allocate() {
    uint32_t idx;
    if (!freeList_.empty()) {
        idx = freeList_.back();
        freeList_.pop_back();
    } else {
        assert(head_ < capacity_ && "CpuDescriptorPool exhausted");
        idx = head_++;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE out{};
    out.ptr = cpuBase_.ptr + static_cast<SIZE_T>(idx) * stride_;
    return out;
}

void CpuDescriptorPool::Free(D3D12_CPU_DESCRIPTOR_HANDLE h) {
    if (!h.ptr || !cpuBase_.ptr) return;
    SIZE_T delta = h.ptr - cpuBase_.ptr;
    uint32_t idx = static_cast<uint32_t>(delta / stride_);
    if (idx < head_) freeList_.push_back(idx);
}

} // namespace WhiteoutDex::gfx::d3d12
