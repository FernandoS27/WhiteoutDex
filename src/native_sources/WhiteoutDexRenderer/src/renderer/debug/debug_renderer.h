#pragma once
// ============================================================================
// DebugRenderer — scene-overlay passes that live on top of the main render.
//
// Split out of RenderService so the primary mesh/particle/ribbon draw path
// isn't tangled with the visualization passes (grid, collision wireframes,
// light markers, ViewCube + Home hotspot). DebugRenderer owns its own GPU
// resources (grid VB, ViewCube cube/outline/home/face atlas) and borrows
// the shared CBPerFrame + samplers + default textures from RenderService
// via a friended reference.
//
// Public interface stays thin: construct, CreateResources() once the device
// is alive, call the Render* methods from RenderService::RenderFrame, and
// DestroyResources() during shutdown. ViewCube input queries (hit test,
// rect, hover) go through the same class so the rendering and pick logic
// don't drift.
// ============================================================================

#include "gfx/gfx.h"
#include "render_target.h"  // Rect
#include "types.h"          // LineVertex lives in render_service.h currently

#include <cstring>
#include <vector>

namespace WhiteoutDex {

class RenderService;

// Upload `lines` to a transient vertex buffer, bind it + cbPerFrame, draw,
// and destroy. Shared by DebugRenderer::RenderCollisions and RenderLightMarkers
// (both emit coloured LineVertex data through linePSO_). Templated on the
// vertex struct so call sites can keep their local {pos, col} struct rather
// than round-tripping through the more-detailed renderer::LineVertex.
//
// The CB state on entry is expected to already contain the desired camera
// matrices; we only BindConstantBuffer here, we do not fill it.
template <class LV>
void DrawWireLines(gfx::IGFXDevice*      gfx,
                   gfx::IGFXCommandList* cmd,
                   gfx::BufferHandle     cbPerFrame,
                   const std::vector<LV>& lines) {
    if (lines.empty() || !gfx) return;
    gfx::BufferDesc bd;
    bd.size  = static_cast<uint32_t>(sizeof(LV) * lines.size());
    bd.usage = gfx::BufferUsage::Vertex | gfx::BufferUsage::CpuWritable;
    gfx::BufferHandle tempVB = gfx->CreateBuffer(bd);
    if (tempVB == gfx::BufferHandle::Invalid) return;

    if (void* mapped = gfx->MapBuffer(tempVB)) {
        std::memcpy(mapped, lines.data(), sizeof(LV) * lines.size());
        gfx->UnmapBuffer(tempVB);

        cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, cbPerFrame);
        cmd->BindVertexBuffer(0, tempVB, sizeof(LV));
        cmd->Draw(static_cast<uint32_t>(lines.size()), 0);
    }
    gfx->Destroy(tempVB);
}

class DebugRenderer {
public:
    explicit DebugRenderer(RenderService& rs) noexcept : rs_(rs) {}

    // Device-scoped resource lifetime. Must run after the gfx device exists
    // but before the first frame. DestroyResources is idempotent.
    bool CreateResources();
    void DestroyResources();

    // Per-frame passes. Each takes the already-active command list; none of
    // them reconfigure the viewport except RenderViewCube (which restores
    // the primary viewport before returning).
    void RenderGrid();
    void RenderCollisions();
    void RenderLightMarkers();
    void RenderViewCube();

    // Input queries. ViewCube rect is in window pixels; HitTest returns a
    // face index 0..5, 6 = Home button, -1 = miss.
    Rect GetViewCubeRect() const;
    int  HitTestViewCube(int mx, int my) const;
    void SetViewCubeHovered(bool h) noexcept { vcHovered_ = h; }
    bool IsViewCubeHovered()       const noexcept { return vcHovered_; }

    // Public so RenderService doesn't have to re-derive the cube pixel size
    // when it lays out adjacent HUD elements.
    static constexpr int kViewCubeSize = 120;

private:
    // Build just the grid line VB (axis-coloured planes at Z=0). Called
    // from CreateResources; kept separate from the ViewCube geometry so
    // either can be rebuilt in isolation if we ever need it.
    bool CreateGridResources();

    // Procedurally generate the 24-vertex cube + 24 outline verts + face
    // atlas texture. Matches the geometry layout the engine's ViewCube
    // shader expects (six 4-vertex faces, atlas mapped u=face/6..+1/6).
    bool CreateViewCubeResources();

    RenderService& rs_;

    // ---- Grid ----
    gfx::BufferHandle gridVB_        = gfx::BufferHandle::Invalid;
    int               gridVertCount_ = 0;

    // ---- ViewCube ----
    // Atlas texture lifetime is owned by TextureAssetManager under this
    // name; vcFaceTex_ caches the handle for fast per-frame bind.
    static constexpr const char* kViewCubeFaceTexName = "debug.viewCubeFace";

    gfx::BufferHandle   vcCubeVB_    = gfx::BufferHandle::Invalid;
    gfx::BufferHandle   vcCubeIB_    = gfx::BufferHandle::Invalid;
    gfx::BufferHandle   vcOutlineVB_ = gfx::BufferHandle::Invalid;
    gfx::BufferHandle   vcHomeVB_    = gfx::BufferHandle::Invalid;
    gfx::TextureHandle  vcFaceTex_   = gfx::TextureHandle::Invalid;
    // Dedicated viewcube.slang shader pair + PSO. Kept here rather than
    // on RenderService because the ViewCube is the only consumer -- no
    // other code in the renderer should ever reach for these.
    gfx::ShaderHandle   viewCubeVS_  = gfx::ShaderHandle::Invalid;
    gfx::ShaderHandle   viewCubePS_  = gfx::ShaderHandle::Invalid;
    gfx::PipelineHandle viewCubePSO_ = gfx::PipelineHandle::Invalid;
    bool                vcHovered_   = false;
};

} // namespace WhiteoutDex
