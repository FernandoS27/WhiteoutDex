#pragma once
// ============================================================================
// ShadowPass — depth-only render of shadow casters from each cascade's
// light POV.
//
// Mirrors the engine's WorldShadowBegin/End loop (preview.exe
// 0x7ff609b0f5d0 / 0x7ff609b0f9d0). Per cascade:
//   * BeginRenderPass(rtv = Invalid, dsv = ShadowService::depthTarget(c)),
//     clear depth to 1.0
//   * Drive the same actor / geoset walk the main HD pass uses, but
//     bind the depth-only PSO (HD VS prepass perm + null PS) and feed
//     vsCB2 with cascadeVP * world.
//   * Skip particles / ribbons / splats — characters and (future)
//     terrain are the only meaningful shadow casters in the model
//     viewer.
//
// The pass writes to D32 depth targets. Once the loop completes, the
// targets transition implicitly to PIXEL_SHADER_RESOURCE on the next
// BindShaderResource — no explicit barrier needed (handled by the gfx
// backends' BeginRenderPass/Bind flow).
// ============================================================================

#include "shadow_service.h"
#include "gfx/gfx.h"

namespace WhiteoutDex {
class RenderService;
} // namespace WhiteoutDex

namespace WhiteoutDex::shadow {

class ShadowPass {
public:
    explicit ShadowPass(RenderService& rs) : rs_(rs) {}

    // Returns true if the pass actually rendered (params.enabled +
    // service ready + at least one actor visible). Caller can use the
    // return as a hint; the bind path in the main HD pass falls back
    // to a default white depth SRV on Invalid handles regardless.
    bool Run(ShadowService& service);

private:
    RenderService& rs_;
};

} // namespace WhiteoutDex::shadow
