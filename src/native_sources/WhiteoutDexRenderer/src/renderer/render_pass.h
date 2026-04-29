#pragma once
// ============================================================================
// render_pass.h — home for CRTP render-pass shells.
//
// BlsGeosetPass<Derived> is the shell for the BLS-family mesh render paths
// (RenderGeosetsBls / RenderGeosetsHd). Shares the outer shell: availability
// check, geoset collection + sort, view/proj resolve, FrameInputs init, and
// the per-geoset light palette. Per-path divergence goes into derived hooks:
//
//   bool IsAvailable() const                     // gate — program loaded?
//   void ComputeViewProj(Matrix44f&, Matrix44f&) // RH (SD) vs LH (HD)
//   void BindPassResources(cmd*, frame&)         // samplers, IBL, fog CB
//   bls::BaselineLights Baseline() const         // fallback light key
//   void DrawGeoset(ref, frame, view, cmd, lightCount)  // per-geoset body
//
// Legacy RenderService::RenderGeosets() stays a standalone function — it
// uses the Slang CBPerFrame pipeline, not BLS CBs, so sharing with the BLS
// family would distort the model.
// ============================================================================

#include "render_service.h"            // full definition — template body dereferences members
#include "render_service_internal.h"   // CollectSortedRenderables, GeosetRef, RenderableView
#include "sampler_asset_manager.h"     // samplers_->LinearWrap() in pass setup
#include "bls/bls_draw_helpers.h"      // BaselineLights, BuildLightPalette
#include "bls/bls_frame.h"             // FrameInputs

#include <mutex>

namespace WhiteoutDex {

template <class Derived>
class BlsGeosetPass {
public:
    explicit BlsGeosetPass(RenderService& rs) noexcept : rs_(rs) {}

    bool Run() {
        Derived&  d  = self();
        if (!d.IsAvailable()) return false;
        if (rs_.scene_->Actors().All().empty()) return true;

        auto* cmd = rs_.gfx_->GetImmediateContext();

        auto collected = render_detail::CollectSortedRenderables(
            rs_.scene_->Actors().All(), rs_.ComputeSelectedLod());
        if (collected.refs.empty()) return true;

        Matrix44f view, proj;
        d.ComputeViewProj(view, proj);

        bls::FrameInputs frame;
        frame.view         = view;
        frame.projection   = proj;
        frame.effectTime   = rs_.scene_->GetAnimationTime() * 0.001f;
        frame.numLights    = 0;
        frame.viewportRect = { (float)rs_.width_, (float)rs_.height_, 0.0f, 0.0f };

        // Default t0 sampler + per-path extras (IBL probes, debug CB, etc.).
        cmd->BindSampler(gfx::ShaderStage::Pixel, 0, rs_.samplers_->LinearWrap());
        d.BindPassResources(cmd, frame);

        const bls::BaselineLights baseline = d.Baseline();

        for (auto& ref : collected.refs) {
            const auto& view_  = *ref.view;
            const auto& geo    = (*view_.geosets)[ref.idx];
            if (geo.unskinnedVb == gfx::BufferHandle::Invalid ||
                geo.ib == gfx::BufferHandle::Invalid ||
                geo.indexCount == 0) continue;

            const int lightCount = bls::BuildLightPalette(
                frame, *view_.activeLights, view, baseline,
                rs_.GetLightingMode());

            d.DrawGeoset(ref, frame, view, cmd, lightCount);
        }
        return true;
    }

protected:
    RenderService& rs_;
    Derived&       self()       { return *static_cast<Derived*>(this); }
    const Derived& self() const { return *static_cast<const Derived*>(this); }
};

} // namespace WhiteoutDex
