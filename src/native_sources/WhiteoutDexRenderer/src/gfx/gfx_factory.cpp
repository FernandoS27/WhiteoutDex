// ============================================================================
// GFX Service — Device factory
// ============================================================================

#include "gfx/gfx.h"
#include "gfx/d3d11/d3d11_device.h"

#include <stdexcept>

namespace WhiteoutDex::gfx {

std::unique_ptr<IGFXDevice> CreateDevice(GfxApi api) {
    switch (api) {
        case GfxApi::D3D11: {
            auto device = std::make_unique<d3d11::D3D11Device>();
            if (!device->Init())
                return nullptr;
            return device;
        }
        default:
            return nullptr;
    }
}

} // namespace WhiteoutDex::gfx
