#pragma once
// ============================================================================
// WhiteoutDex Renderer — Actor
//
// Logical entity in the scene: identity, world transform, hierarchy, animation,
// and an owned RenderModel cluster.
//
// Phase 5 v3: composition over inheritance — Actor holds a `render` member of
// type RenderModel instead of inheriting from it. The boundary is now enforced
// at the call site: `actor.render.gpuGeosets` (preferred) or `actor.Render()`
// (the explicit accessor). Keeps the cluster split clean and prevents Actor
// from accumulating render-data methods on its public surface.
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
struct Actor {
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

    // Free-running sequence-loop bookkeeping. Driven by SceneManager::Update
    // for top-level (non-PE1) actors so each actor can play its own sequence
    // at its own pace independent of the host loop. PE1/attachment children
    // skip this entirely — their local time is derived from BirthTimeMs and
    // computed inside EvaluatePE1Children.
    //
    //   `sequenceStartTimeMs` resets to SceneManager::animationTime_ each
    //   time `prevActiveSequence` mismatches the driver's current index, so
    //   localTime = animationTime_ - sequenceStartTimeMs.
    int sequenceStartTimeMs = 0;
    int prevActiveSequence  = -1;

    // Eval scheduling. By default the renderer's per-tick
    // EvaluateTopLevelActors walks every top-level actor on the render
    // thread. Sources that read mutable host-thread state (the Max plugin's
    // MaxSceneAdapter queries live Max scene graph nodes, which are *only*
    // safe to touch from Max's UI thread) must opt out and let the host
    // call RenderService::EvaluateAndApply explicitly from the right thread.
    // SpawnActorFromLiveSource sets this; static MDX-backed actors leave it
    // false and ride on the auto-evaluate path.
    bool externallyDriven = false;

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

    // ---- Hierarchy ----
    // Parent actor id (0 = root). Set when this actor was spawned as a PE1
    // child or attached through an AttachmentSlot. Walks up the hierarchy
    // (despawn cascades, visibility propagation) go through this. The
    // forward direction (parent → children) is still encoded slot-wise via
    // attachmentSlots[i].childModelHandle and through PE1System's per-frame
    // birth/death lists; an explicit `children_` ID list isn't needed yet.
    uint32_t parent     = 0;

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

    // ---- The render-side cluster. Single concern: per-actor GPU + sim state.
    RenderModel render;

    // ---- Convenience accessors. `actor.render.X` is the canonical form;
    // `actor.Render()` keeps reading well at sites that pass the cluster
    // along (e.g. RenderableView builders).
    RenderModel&       Render()       { return render; }
    const RenderModel& Render() const { return render; }

    // Release all GPU resources
    void ReleaseGPU(gfx::IGFXDevice& gfx) {
        const bool freeShared = !sourceTemplate;
        for (auto& g : render.gpuGeosets) g.Release(gfx, freeShared);
        render.gpuGeosets.clear();
        if (render.textures) render.textures->Clear();   // ModelScope keeps its allocation; pixels are freed.
        render.gpuMaterials.clear();
        gfx.Destroy(render.ribbonVB);
        render.ribbonVB = gfx::BufferHandle::Invalid;
        render.ribbonVBSize = 0;
        // Drop our refcount on the template; the template is destroyed only
        // when both the cache entry and every borrowing instance have let go.
        sourceTemplate.reset();
    }
};

} // namespace WhiteoutDex
