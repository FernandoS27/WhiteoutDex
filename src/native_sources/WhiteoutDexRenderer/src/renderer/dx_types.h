#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — DX11/Win32 Internal Types
// Internal header: only included by render_service.cpp, render_target.cpp,
// model_instance.h, gpu_resources.h, render_window.cpp. Never by public API.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace WhiteoutDex {

// Safe COM release
template<typename T>
inline void SafeRelease(T*& ptr) {
    if (ptr) { ptr->Release(); ptr = nullptr; }
}

} // namespace WhiteoutDex
