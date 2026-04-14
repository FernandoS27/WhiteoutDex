// ============================================================================
// RenderTarget — implementation
// ============================================================================

#include "render_target.h"
#include "dx_types.h"   // SafeRelease

namespace WhiteoutDex {

bool RenderTarget::Resize(ID3D11Device* device, int w, int h) {
    if (!device) return false;

    if (swapChain) {
        // Swap-chain target: resize the swap chain and recreate RTV + DSV.
        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (ctx) {
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ctx->Release();
        }

        SafeRelease(rtv);
        SafeRelease(dsv);
        SafeRelease(depthBuf);

        swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN,
                                 DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

        ID3D11Texture2D* backBuffer = nullptr;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (!backBuffer) return false;
        device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
        backBuffer->Release();
    } else if (colorTex) {
        // Off-screen target: recreate the color texture + RTV.
        SafeRelease(rtv);
        SafeRelease(dsv);
        SafeRelease(depthBuf);
        SafeRelease(colorTex);

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        device->CreateTexture2D(&td, nullptr, &colorTex);
        if (!colorTex) return false;
        device->CreateRenderTargetView(colorTex, nullptr, &rtv);
    } else {
        return false;
    }

    // Depth/stencil buffer (shared between both target types).
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = w;
    dd.Height = h;
    dd.MipLevels = dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc = {1, 0};
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    device->CreateTexture2D(&dd, nullptr, &depthBuf);
    device->CreateDepthStencilView(depthBuf, nullptr, &dsv);

    width = w;
    height = h;
    return true;
}

void RenderTarget::Release() {
    SafeRelease(rtv);
    SafeRelease(dsv);
    SafeRelease(depthBuf);
    SafeRelease(colorTex);
    SafeRelease(swapChain);
    width = height = 0;
}

} // namespace WhiteoutDex
