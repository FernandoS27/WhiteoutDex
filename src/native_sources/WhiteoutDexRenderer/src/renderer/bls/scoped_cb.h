#pragma once
// ============================================================================
// ScopedCb<T> — RAII wrapper around IGFXDevice::MapBuffer / UnmapBuffer.
//
// Replaces the scattered `if (auto* p = (T*)gfx->MapBuffer(h)) { Build...; gfx->UnmapBuffer(h); }`
// stanza with a single scope. The wrapper unmaps in the destructor so an
// early return out of a draw loop can no longer leak a mapped buffer.
//
// Usage:
//   if (auto vs = bls::ScopedCb<bls::SdVsCbA>(gfx, blsSdVsCb_)) {
//       bls::BuildSdVsCbA(*vs, frame, mp);
//   } // <-- unmap happens here
//
// Non-copyable, non-movable: the buffer handle is owned for the scope and
// the lifetime is always trivially stack-scoped at the call sites we care
// about. Keeping it immovable avoids ambiguity around who owns the unmap.
// ============================================================================

#include "gfx/gfx.h"

namespace WhiteoutDex::bls {

template <class T>
class ScopedCb {
public:
    ScopedCb(gfx::IGFXDevice* gfx, gfx::BufferHandle handle)
        : gfx_(gfx), handle_(handle),
          ptr_(gfx && handle != gfx::BufferHandle::Invalid
                   ? static_cast<T*>(gfx->MapBuffer(handle))
                   : nullptr) {}

    ~ScopedCb() {
        if (ptr_) gfx_->UnmapBuffer(handle_);
    }

    ScopedCb(const ScopedCb&)            = delete;
    ScopedCb& operator=(const ScopedCb&) = delete;
    ScopedCb(ScopedCb&&)                 = delete;
    ScopedCb& operator=(ScopedCb&&)      = delete;

    explicit operator bool() const { return ptr_ != nullptr; }

    T* operator->()             { return ptr_; }
    const T* operator->() const { return ptr_; }
    T& operator*()              { return *ptr_; }
    const T& operator*()  const { return *ptr_; }
    T* get()                    { return ptr_; }
    const T* get()        const { return ptr_; }

private:
    gfx::IGFXDevice*  gfx_;
    gfx::BufferHandle handle_;
    T*                ptr_;
};

} // namespace WhiteoutDex::bls
