#pragma once
// ============================================================================
// WhiteoutDex Renderer — Actor
//
// Logical entity in the scene: identity, world transform, hierarchy, animation,
// and a borrowed RenderModel cluster.
//
// Phase 4 implementation note: Actor publicly inherits from RenderModel so
// existing call sites that read members directly (mi->gpuGeosets, mi->ribbons,
// etc.) keep compiling without textual churn. The boundary still exists at the
// type level — `actor.Render()` returns the RenderModel reference for code
// that wants to acknowledge the split.
// ============================================================================

#include "../gfx/gfx.h"
#include "animation_driver.h"
#include "model_source.h"
#include "render_model.h"

#include <memory>
#include <vector>

namespace WhiteoutDex {

// Cross-instance template (full def in renderer/model_template.h).
struct ModelTemplate;

// ============================================================================
// Actor — one renderable entity. Currently a struct (public-everything) for
// migration ergonomics; will be tightened to a class with accessors as the
// migration progresses.
// ============================================================================
struct Actor : public RenderModel {
    uint32_t  handle  = 0;
    bool      isFocus = false;

    // World transform for the entire model instance
    // (identity for focus model, per-particle transform for PE1 children)
    Matrix44f worldTransform = Matrix44f::identity();

    // Per-actor animation state machine. Holds the polymorphic
    // IAnimationSource (TemplateAnimationSource for file-backed actors,
    // MaxSceneAdapter for the live source). Carries the per-actor sequence
    // cursor + birth-time offset that PE1 children + attachment children use.
    AnimationDriver animation;

    // ---- Attachments with child models ----
    struct AttachmentSlot {
        AttachmentConfig config;
        uint32_t childModelHandle = 0;  // 0 = not yet loaded
        bool loaded = false;
        bool wasVisible = false;        // tracks first-visible for animation start
    };
    std::vector<AttachmentSlot> attachmentSlots;

    // Visibility multiplier driven by the parent model when this instance is
    // hosted as an attachment child. 1 = fully visible, 0 = fully hidden.
    // Authoritative source: only the parent's ApplyFrameState writes this.
    float parentVisibility = 1.0f;

    // ---- PE1 (model particle emitter) hierarchy state ----
    int  pe1Depth   = 0;     // recursion depth (0 = root model)
    bool isPE1Child = false; // true if spawned by a PE1 particle

    // Cross-instance template the model was staged from. Non-null for any
    // Actor built through stageModelFromTemplate (PE1 children, attachments,
    // future path-based AddModel). Owns the shared geometry buffers
    // (ib/unskinnedVb/tangentVb/boneVb) — the instance's GPUGeoset entries
    // borrow those handles and must NOT free them. Per-frame state
    // (bonePaletteCb, world matrix, geoset alpha/color) stays local.
    std::shared_ptr<ModelTemplate> sourceTemplate;

    // ---- Boundary accessors. Future cleanup will tighten direct field
    // access into these — call sites that already think in terms of "the
    // render data" should prefer Render() over inherited member access.
    RenderModel&       Render()       { return *this; }
    const RenderModel& Render() const { return *this; }

    // Release all GPU resources
    void ReleaseGPU(gfx::IGFXDevice& gfx) {
        const bool freeShared = !sourceTemplate;
        for (auto& g : gpuGeosets) g.Release(gfx, freeShared);
        gpuGeosets.clear();
        if (textures) textures->Clear();   // ModelScope keeps its allocation; pixels are freed.
        gpuMaterials.clear();
        gfx.Destroy(ribbonVB); ribbonVB = gfx::BufferHandle::Invalid; ribbonVBSize = 0;
        // Drop our refcount on the template; the template is destroyed only
        // when both the cache entry and every borrowing instance have let go.
        sourceTemplate.reset();
    }
};

} // namespace WhiteoutDex
