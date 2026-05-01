// ============================================================================
// ShadowPass — implementation
//
// Walks every top-level actor's opaque GPUGeoset and renders depth-only
// from each cascade's POV. Mirrors the engine's WorldShadowBegin/End
// loop (preview.exe 0x7ff609b0f5d0 / 0x7ff609b0f9d0). Per cascade:
//
//   * BeginRenderPass(rtv = Invalid, dsv = ShadowService::depthTarget(c)),
//     clear depth to 1.0
//   * For each actor's geoset (skinned OR static):
//     - Skinned (boneVb + bonePaletteCb both present): bind the
//       FourBoneSkinning PSO (HD VS perm 4), slot-0 = unskinnedVb,
//       slot-1 = boneVb, vsCB3 = bonePaletteCb.
//     - Static (no bones): bind the Rigid PSO (HD VS perm 0,
//       kParticleSD layout — same combination the HD draw path
//       uses for the same `(no bones, no tangent)` case). Slot-0
//       only.
//     - Both branches share index buffer, vsCB2 (HdVsCb with
//       worldViewProj = world * cascadeVP), and DrawIndexed.
//
// Particles / ribbons / splats are intentionally skipped — they
// don't cast meaningful shadows for character preview, and their
// VBs use different layouts that wouldn't match either depth-only
// PSO's IA.
//
// The depth target stays in DSV state through the whole loop; the
// transition to PIXEL_SHADER_RESOURCE happens implicitly when the HD
// pass binds it at t10..t12 next frame (gfx backend handles barriers).
// ============================================================================

#include "shadow_pass.h"

#include "renderer/render_service.h"
#include "renderer/render_service_internal.h"   // RenderableView access not used yet but keeps include parity
#include "renderer/scene_manager.h"
#include "renderer/model_instance.h"
#include "renderer/render_model.h"
#include "renderer/bls/bls_cb_layout.h"
#include "renderer/bls/scoped_cb.h"
#include "renderer/types.h"

namespace WhiteoutDex::shadow {

namespace {

// Build a HdVsCb specifically for a depth-only shadow draw. Only
// `worldViewProj` matters — the null PS doesn't sample anything the
// VS writes besides SV_POSITION. `world` and `worldView` are filled
// with deterministic values to avoid "uninitialised UB" reads in
// PIX captures.
void BuildShadowVsCb(bls::HdVsCb&     out,
                     const Matrix44f& worldTransform,
                     const Matrix44f& cascadeVP) {
    std::memset(&out, 0, sizeof(out));
    out.world         = worldTransform;
    out.worldView     = worldTransform;          // unused by null PS
    out.worldViewProj = worldTransform * cascadeVP;
    out.misc          = { 0.0f, 1.0f, 0.0f, 0.0f };
    out.diffuseColor  = { 1.0f, 1.0f, 1.0f, 1.0f };
    out.texMtx0       = {};
    out.texMtx1       = {};
}

} // namespace

bool ShadowPass::Run(ShadowService& service) {
    if (!service.IsEnabled()) return false;

    auto* gfx = rs_.GetGfxDevice();
    if (!gfx) return false;
    auto* cmd = gfx->GetImmediateContext();
    if (!cmd) return false;

    // PSOs + CB are constructed in InitBlsShaders. If both PSOs are
    // missing (e.g. HD program failed to load), the service can
    // still be enabled but we silently skip the depth render. The
    // HD main pass will read all-1.0 depth (cleared) and shadowAtten
    // ≈ 1.0 at every pixel, so the result is "no shadowing" —
    // fail-soft.
    const gfx::PipelineHandle psoSkinned = rs_.shadowPSO_;
    const gfx::PipelineHandle psoRigid   = rs_.shadowPSORigid_;
    const gfx::BufferHandle   vsCb       = rs_.shadowVsCb_;
    const bool                anyPso =
        (psoSkinned != gfx::PipelineHandle::Invalid ||
         psoRigid   != gfx::PipelineHandle::Invalid)
        && vsCb != gfx::BufferHandle::Invalid;

    bool any = false;
    for (int c = 0; c < service.cascadeCount(); ++c) {
        const gfx::TextureHandle dst = service.depthTarget(c);
        if (dst == gfx::TextureHandle::Invalid) continue;

        cmd->BeginRenderPass(/*rtv*/        gfx::TextureHandle::Invalid,
                              /*dsv*/        dst,
                              /*clearColor*/ nullptr,
                              /*clearDepth*/ 1.0f,
                              /*clearStencil*/ 0);
        const float res = static_cast<float>(service.Params().cascadeResolution);
        cmd->SetViewport({0.0f, 0.0f, res, res, 0.0f, 1.0f});

        if (anyPso) {
            const Matrix44f& cascadeVP = service.cascadeVP(c);

            // Match the HD draw path's visibility/LOD selection so
            // we don't write hidden geosets (LOD-out, alpha-zero,
            // animation-hidden) into the cascade depth map. Without
            // this filter, invisible geosets cast shadows that the
            // HD pass then samples — visually manifests as shadow
            // contributions from geometry that isn't even on screen.
            const int selectedLod = rs_.ComputeSelectedLod();

            // Track the currently-bound PSO so we only emit a
            // BindPipeline call when the geoset's skin policy
            // actually changes — typical scenes group an actor's
            // skinned geos consecutively, so two binds per actor
            // is the worst case.
            gfx::PipelineHandle currentPso = gfx::PipelineHandle::Invalid;

            std::lock_guard<std::mutex> lock(rs_.dataMutex_);
            for (auto& [h, mi] : rs_.scene_->Actors().All()) {
                if (!mi)               continue;
                if (mi->isPE1Child)    continue;
                if (mi->parentVisibility <= 0.02f) continue;

                // Per-actor LOD: render.hasLods is set during upload
                // when any geoset advertises a LOD other than 0 /
                // 0xFFFFFFFFu. Models without LODs pin to 0 so all
                // their geosets pass the LOD gate.
                const int modelLod = mi->render.hasLods ? selectedLod : 0;

                for (auto& geo : mi->render.gpuGeosets) {
                    if (geo.unskinnedVb == gfx::BufferHandle::Invalid) continue;
                    if (geo.ib          == gfx::BufferHandle::Invalid) continue;
                    if (geo.indexCount  == 0)                          continue;

                    // LOD filter — same predicate the HD path uses
                    // through render_detail::CollectSortedRenderables.
                    if (!RenderService::GeosetPassesLod(geo.lod, modelLod))
                        continue;

                    // Animated geoset visibility — KGAO/MDX geoset
                    // alpha track times the actor's parent visibility.
                    // The HD path treats anything <= 0 as hidden
                    // (issueHdDraw early-out at the geoAlpha gate).
                    const float geoAlpha =
                        geo.geosetAlpha * mi->parentVisibility;
                    if (geoAlpha <= 0.0f) continue;

                    const bool hasBones =
                        geo.boneVb        != gfx::BufferHandle::Invalid &&
                        geo.bonePaletteCb != gfx::BufferHandle::Invalid;
                    const gfx::PipelineHandle pso =
                        hasBones ? psoSkinned : psoRigid;
                    if (pso == gfx::PipelineHandle::Invalid) continue;

                    if (pso != currentPso) {
                        cmd->BindPipeline(pso);
                        currentPso = pso;
                    }

                    if (auto vs = bls::ScopedCb<bls::HdVsCb>(gfx, vsCb)) {
                        BuildShadowVsCb(*vs, mi->worldTransform, cascadeVP);
                    }
                    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 2, vsCb);

                    cmd->BindIndexBuffer(geo.ib, gfx::Format::R32_UINT);
                    cmd->BindVertexBuffer(0, geo.unskinnedVb, sizeof(Vertex));

                    if (hasBones) {
                        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex,
                                                3, geo.bonePaletteCb);
                        cmd->BindVertexBuffer(1, geo.boneVb, sizeof(BoneVertex));
                    }

                    cmd->DrawIndexed(static_cast<uint32_t>(geo.indexCount),
                                      /*startIndex*/  0,
                                      /*baseVertex*/  0);
                }
            }
        }

        cmd->EndRenderPass();
        any = true;
    }
    return any;
}

} // namespace WhiteoutDex::shadow
