// ============================================================================
// DebugRenderer — see debug_renderer.h for the split rationale.
// ============================================================================

#include "debug_renderer.h"
#include "render_service.h"
#include "render_service_internal.h"
#include "compiled_shaders.h"
#include "constants.h"
#include "viewcube_atlas.h"
#include "coordinate_system.h"

#include <cmath>
#include <cstring>
#include <mutex>
#include <numbers>
#include <vector>

namespace WhiteoutDex {

// ============================================================================
// Resource lifetime
// ============================================================================

bool DebugRenderer::CreateResources() {
    if (!CreateGridResources())     return false;
    if (!CreateViewCubeResources()) return false;
    return true;
}

void DebugRenderer::DestroyResources() {
    if (!rs_.gfx_) return;
    rs_.gfx_->Destroy(gridVB_);        gridVB_      = gfx::BufferHandle::Invalid;
    rs_.gfx_->Destroy(vcCubeVB_);      vcCubeVB_    = gfx::BufferHandle::Invalid;
    rs_.gfx_->Destroy(vcCubeIB_);      vcCubeIB_    = gfx::BufferHandle::Invalid;
    rs_.gfx_->Destroy(vcOutlineVB_);   vcOutlineVB_ = gfx::BufferHandle::Invalid;
    rs_.gfx_->Destroy(vcHomeVB_);      vcHomeVB_    = gfx::BufferHandle::Invalid;
    rs_.gfx_->Destroy(vcFaceTex_);     vcFaceTex_   = gfx::TextureHandle::Invalid;
    rs_.gfx_->Destroy(viewCubePSO_);   viewCubePSO_ = gfx::PipelineHandle::Invalid;
    rs_.gfx_->Destroy(viewCubeVS_);    viewCubeVS_  = gfx::ShaderHandle::Invalid;
    rs_.gfx_->Destroy(viewCubePS_);    viewCubePS_  = gfx::ShaderHandle::Invalid;
    gridVertCount_ = 0;
}

bool DebugRenderer::CreateGridResources() {
    std::vector<LineVertex> lines;
    const float extent = 500.0f;
    const float step   = 50.0f;
    Vector4f gridColor  = {0.45f, 0.45f, 0.46f, 1.0f};
    Vector4f axisColorX = {0.75f, 0.2f,  0.2f,  1.0f};
    Vector4f axisColorY = {0.2f,  0.75f, 0.2f,  1.0f};
    Vector4f axisColorZ = {0.2f,  0.2f,  0.75f, 1.0f};

    for (float v = -extent; v <= extent; v += step) {
        Vector4f c = (v == 0.0f) ? axisColorY : gridColor;
        lines.push_back({{v, -extent, 0.0f}, c});
        lines.push_back({{v,  extent, 0.0f}, c});
        c = (v == 0.0f) ? axisColorX : gridColor;
        lines.push_back({{-extent, v, 0.0f}, c});
        lines.push_back({{ extent, v, 0.0f}, c});
    }
    lines.push_back({{0.0f, 0.0f, 0.0f},   axisColorZ});
    lines.push_back({{0.0f, 0.0f, extent}, axisColorZ});
    gridVertCount_ = (int)lines.size();

    gridVB_ = rs_.gfx_->CreateBuffer({
        .size  = sizeof(LineVertex) * lines.size(),
        .usage = gfx::BufferUsage::Vertex,
    }, lines.data());
    return gridVB_ != gfx::BufferHandle::Invalid;
}

bool DebugRenderer::CreateViewCubeResources() {
    // Generate face label atlas procedurally (platform-neutral)
    {
        int tw, th;
        auto pixels = GenerateViewCubeAtlas(tw, th);
        vcFaceTex_ = rs_.gfx_->CreateTexture({
            .width  = tw,
            .height = th,
            .format = gfx::Format::R8G8B8A8_UNORM,
            .usage  = gfx::TextureUsage::ShaderResource,
        }, pixels.data());
    }

    // Cube geometry: 24 vertices (4 per face), 36 indices
    // Face order: Front(+Y), Back(-Y), Left(-X), Right(+X), Top(+Z), Bottom(-Z)
    float s = 0.5f;

    // UV: each face maps to column i/6 in the texture atlas
    auto uv = [](int face, int corner) -> std::pair<float,float> {
        float u0 = face / 6.0f, u1 = (face + 1) / 6.0f;
        switch(corner) {
            case 0: return {u0, 1.0f};
            case 1: return {u1, 1.0f};
            case 2: return {u1, 0.0f};
            case 3: return {u0, 0.0f};
        }
        return {0,0};
    };

    std::vector<Vertex> verts;
    auto addFace = [&](int face, Vector3f p0, Vector3f p1, Vector3f p2, Vector3f p3, Vector3f n) {
        for (int c = 0; c < 4; c++) {
            auto [u, v] = uv(face, c);
            Vector3f p = (c==0) ? p0 : (c==1) ? p1 : (c==2) ? p2 : p3;
            verts.push_back({p, n, {1,1,1,1}, {u, v}});
        }
    };

    struct FaceSpec { Vector3f p0, p1, p2, p3, n; };
    FaceSpec faces[6];
    if constexpr (kDefaultCoordSpace == CoordSpace::Blizzard) {
        faces[0] = {{ s, s, s}, { s,-s, s}, { s,-s,-s}, { s, s,-s}, { 1, 0, 0}};
        faces[1] = {{-s,-s, s}, {-s, s, s}, {-s, s,-s}, {-s,-s,-s}, {-1, 0, 0}};
        faces[2] = {{ s, s, s}, {-s, s, s}, {-s, s,-s}, { s, s,-s}, { 0, 1, 0}};
        faces[3] = {{-s,-s, s}, { s,-s, s}, { s,-s,-s}, {-s,-s,-s}, { 0,-1, 0}};
        faces[4] = {{-s, s, s}, { s, s, s}, { s,-s, s}, {-s,-s, s}, { 0, 0, 1}};
        faces[5] = {{-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s}, { 0, 0,-1}};
    } else {
        faces[0] = {{ s,-s,-s}, {-s,-s,-s}, {-s,-s, s}, { s,-s, s}, { 0,-1, 0}};
        faces[1] = {{-s, s,-s}, { s, s,-s}, { s, s, s}, {-s, s, s}, { 0, 1, 0}};
        faces[2] = {{ s, s,-s}, { s,-s,-s}, { s,-s, s}, { s, s, s}, { 1, 0, 0}};
        faces[3] = {{-s,-s,-s}, {-s, s,-s}, {-s, s, s}, {-s,-s, s}, {-1, 0, 0}};
        faces[4] = {{-s, s, s}, { s, s, s}, { s,-s, s}, {-s,-s, s}, { 0, 0, 1}};
        faces[5] = {{-s,-s,-s}, { s,-s,-s}, { s, s,-s}, {-s, s,-s}, { 0, 0,-1}};
    }
    for (int i = 0; i < 6; ++i) {
        const auto& f = faces[i];
        addFace(i, f.p0, f.p1, f.p2, f.p3, f.n);
    }

    vcCubeVB_ = rs_.gfx_->CreateBuffer({
        .size  = sizeof(Vertex) * verts.size(),
        .usage = gfx::BufferUsage::Vertex,
    }, verts.data());

    std::vector<uint32_t> idx;
    for (int f = 0; f < 6; f++) {
        uint32_t base = f * 4;
        idx.insert(idx.end(), {base, base+1, base+2, base, base+2, base+3});
    }
    vcCubeIB_ = rs_.gfx_->CreateBuffer({
        .size  = sizeof(uint32_t) * idx.size(),
        .usage = gfx::BufferUsage::Index,
    }, idx.data());

    // Edge lines (12 edges of the cube, slightly offset to z-fight over the faces)
    std::vector<LineVertex> edges;
    Vector4f ec = {0.2f, 0.2f, 0.2f, 1.0f};
    float e = s * 1.001f;
    edges.push_back({{-e,-e,-e}, ec}); edges.push_back({{ e,-e,-e}, ec});
    edges.push_back({{ e,-e,-e}, ec}); edges.push_back({{ e, e,-e}, ec});
    edges.push_back({{ e, e,-e}, ec}); edges.push_back({{-e, e,-e}, ec});
    edges.push_back({{-e, e,-e}, ec}); edges.push_back({{-e,-e,-e}, ec});
    edges.push_back({{-e,-e, e}, ec}); edges.push_back({{ e,-e, e}, ec});
    edges.push_back({{ e,-e, e}, ec}); edges.push_back({{ e, e, e}, ec});
    edges.push_back({{ e, e, e}, ec}); edges.push_back({{-e, e, e}, ec});
    edges.push_back({{-e, e, e}, ec}); edges.push_back({{-e,-e, e}, ec});
    edges.push_back({{-e,-e,-e}, ec}); edges.push_back({{-e,-e, e}, ec});
    edges.push_back({{ e,-e,-e}, ec}); edges.push_back({{ e,-e, e}, ec});
    edges.push_back({{ e, e,-e}, ec}); edges.push_back({{ e, e, e}, ec});
    edges.push_back({{-e, e,-e}, ec}); edges.push_back({{-e, e, e}, ec});

    vcOutlineVB_ = rs_.gfx_->CreateBuffer({
        .size  = sizeof(LineVertex) * edges.size(),
        .usage = gfx::BufferUsage::Vertex,
    }, edges.data());

    // Dedicated viewcube.slang shader + PSO. Reuses the same 48 B Vertex
    // layout the cube VB was uploaded with (pos/normal/color/uv), with
    // opaque blend + default depth + no culling so the inside of the
    // cube silhouette stays visible from any camera angle.
    using namespace WhiteoutDex::Shaders;
    viewCubeVS_ = rs_.gfx_->CreateShader(gfx::ShaderStage::Vertex, kViewCubeVS, sizeof(kViewCubeVS));
    viewCubePS_ = rs_.gfx_->CreateShader(gfx::ShaderStage::Pixel,  kViewCubePS, sizeof(kViewCubePS));
    if (viewCubeVS_ == gfx::ShaderHandle::Invalid ||
        viewCubePS_ == gfx::ShaderHandle::Invalid) {
        return false;
    }

    gfx::InputElement vcInput[] = {
        {"POSITION", 0, gfx::Format::R32G32B32_FLOAT,    0},
        {"NORMAL",   0, gfx::Format::R32G32B32_FLOAT,   12},
        {"COLOR",    0, gfx::Format::R32G32B32A32_FLOAT, 24},
        {"TEXCOORD", 0, gfx::Format::R32G32_FLOAT,       40},
    };
    gfx::GraphicsPipelineDesc vcDesc;
    vcDesc.vs          = viewCubeVS_;
    vcDesc.ps          = viewCubePS_;
    vcDesc.inputLayout = vcInput;
    vcDesc.topology    = gfx::PrimitiveTopology::TriangleList;
    vcDesc.blend.enable = false;
    // depthStencil + rasterizer default to {depthTest=true, depthWrite=true,
    // LessEqual} / {Back, Solid, frontCCW=false} — we only override cull
    // (None so the cube is drawable from any angle) and the CCW convention.
    vcDesc.rasterizer.cull     = gfx::CullMode::None;
    vcDesc.rasterizer.frontCCW = true;
    viewCubePSO_ = rs_.gfx_->CreateGraphicsPipeline(vcDesc);

    return vcCubeVB_    != gfx::BufferHandle::Invalid &&
           vcCubeIB_    != gfx::BufferHandle::Invalid &&
           vcOutlineVB_ != gfx::BufferHandle::Invalid &&
           viewCubePSO_ != gfx::PipelineHandle::Invalid;
}

// ============================================================================
// Grid
// ============================================================================

void DebugRenderer::RenderGrid() {
    if (gridVB_ == gfx::BufferHandle::Invalid) return;
    auto* cmd = rs_.gfx_->GetImmediateContext();
    cmd->BindPipeline(rs_.linePSO_);
    cmd->BindVertexBuffer(0, gridVB_, sizeof(LineVertex));
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, rs_.cbPerFrame_);
    cmd->Draw(gridVertCount_, 0);
}

// ============================================================================
// Collision Shape Wireframe Rendering
// ============================================================================

void DebugRenderer::RenderCollisions() {
    std::vector<CollisionShape> shapes;
    Matrix44f viewMat;
    {
        std::lock_guard<std::mutex> lock(rs_.dataMutex_);
        for (auto& [h, mi] : rs_.models_) {
            if (mi->parentVisibility <= 0.02f) continue;
            shapes.insert(shapes.end(), mi->collisionShapes.begin(), mi->collisionShapes.end());
        }
        if (shapes.empty()) return;
        viewMat = rs_.camera_.GetViewMatrix();
    }

    auto* cmd = rs_.gfx_->GetImmediateContext();
    cmd->BindPipeline(rs_.linePSO_);

    struct LV { Vector3f pos; Vector4f col; };
    Vector4f col = {0.0f, 1.0f, 0.3f, 1.0f};

    // Batch every shape's line segments into one vector and emit a single
    // draw — the original code did one create-VB/draw/destroy per shape,
    // which is fine functionally but wasteful. All shapes share the same
    // colour so there's no per-shape state that demands separate draws.
    std::vector<LV> lines;

    for (auto& cs : shapes) {
        // Previewd COLLIDE_TYPE: 0=Box (2 vec3 extents), 1=Cylinder (2 vec3
        // endpoints + radius), 2=Sphere (vec3 center + radius), 3=Plane
        // (2 floats width/height). WhiteoutLib enum labels are mis-named
        // but the numeric values match the file — trust cs.type directly.
        const Vector3f& piv = cs.pivot;
        auto pushLine = [&](const Vector3f& a, const Vector3f& b) {
            Vector3f ap = {a.x + piv.x, a.y + piv.y, a.z + piv.z};
            Vector3f bp = {b.x + piv.x, b.y + piv.y, b.z + piv.z};
            Vector3f pa = whiteout::transform_point(ap, cs.transform);
            Vector3f pb = whiteout::transform_point(bp, cs.transform);
            lines.push_back({pa, col});
            lines.push_back({pb, col});
        };
        auto emitCircle = [&](const Vector3f& c, float r, int axis) {
            const int segs = 24;
            for (int i = 0; i < segs; i++) {
                float a0 = (float)i / segs * 6.28318530f;
                float a1 = (float)(i+1) / segs * 6.28318530f;
                float c0 = r * cosf(a0), s0 = r * sinf(a0);
                float c1 = r * cosf(a1), s1 = r * sinf(a1);
                Vector3f p0, p1;
                if (axis == 2)      { p0 = {c.x+c0, c.y+s0, c.z}; p1 = {c.x+c1, c.y+s1, c.z}; }
                else if (axis == 1) { p0 = {c.x+c0, c.y, c.z+s0}; p1 = {c.x+c1, c.y, c.z+s1}; }
                else                { p0 = {c.x, c.y+c0, c.z+s0}; p1 = {c.x, c.y+c1, c.z+s1}; }
                pushLine(p0, p1);
            }
        };

        if (cs.type == 0) {
            Vector3f mn = cs.vmin, mx = cs.vmax;
            Vector3f corners[8] = {
                {mn.x,mn.y,mn.z}, {mx.x,mn.y,mn.z}, {mx.x,mx.y,mn.z}, {mn.x,mx.y,mn.z},
                {mn.x,mn.y,mx.z}, {mx.x,mn.y,mx.z}, {mx.x,mx.y,mx.z}, {mn.x,mx.y,mx.z}
            };
            int edges[24] = {0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7};
            for (int i = 0; i < 24; i += 2)
                pushLine(corners[edges[i]], corners[edges[i+1]]);
        } else if (cs.type == 2) {
            emitCircle(cs.vmin, cs.radius, 0);
            emitCircle(cs.vmin, cs.radius, 1);
            emitCircle(cs.vmin, cs.radius, 2);
        } else if (cs.type == 1) {
            Vector3f axisVec = { cs.vmax.x - cs.vmin.x, cs.vmax.y - cs.vmin.y, cs.vmax.z - cs.vmin.z };
            float axisLen = std::sqrt(axisVec.x*axisVec.x + axisVec.y*axisVec.y + axisVec.z*axisVec.z);
            Vector3f axis = (axisLen > 1e-5f)
                ? Vector3f{axisVec.x/axisLen, axisVec.y/axisLen, axisVec.z/axisLen}
                : Vector3f{0, 0, 1};
            Vector3f tmp = (std::abs(axis.z) < 0.9f) ? Vector3f{0,0,1} : Vector3f{1,0,0};
            Vector3f u = {
                axis.y*tmp.z - axis.z*tmp.y,
                axis.z*tmp.x - axis.x*tmp.z,
                axis.x*tmp.y - axis.y*tmp.x };
            float uLen = std::sqrt(u.x*u.x + u.y*u.y + u.z*u.z);
            if (uLen > 1e-5f) { u.x/=uLen; u.y/=uLen; u.z/=uLen; }
            Vector3f v = {
                axis.y*u.z - axis.z*u.y,
                axis.z*u.x - axis.x*u.z,
                axis.x*u.y - axis.y*u.x };
            const int segs = 24;
            auto ringPt = [&](const Vector3f& c, float a) {
                float cs_ = cs.radius * cosf(a), sn_ = cs.radius * sinf(a);
                return Vector3f{ c.x + u.x*cs_ + v.x*sn_,
                                 c.y + u.y*cs_ + v.y*sn_,
                                 c.z + u.z*cs_ + v.z*sn_ };
            };
            for (int i = 0; i < segs; i++) {
                float a0 = (float)i / segs * 6.28318530f;
                float a1 = (float)(i+1) / segs * 6.28318530f;
                pushLine(ringPt(cs.vmin, a0), ringPt(cs.vmin, a1));
                pushLine(ringPt(cs.vmax, a0), ringPt(cs.vmax, a1));
            }
            for (int i = 0; i < 4; i++) {
                float a = (float)i / 4 * 6.28318530f;
                pushLine(ringPt(cs.vmin, a), ringPt(cs.vmax, a));
            }
        } else if (cs.type == 3) {
            float hw = cs.vmin.x, hh = cs.vmin.y;
            Vector3f p0 = {-hw, -hh, 0}, p1 = {hw, -hh, 0};
            Vector3f p2 = {hw, hh, 0},   p3 = {-hw, hh, 0};
            pushLine(p0, p1); pushLine(p1, p2); pushLine(p2, p3); pushLine(p3, p0);
        }
    }

    if (lines.empty()) return;

    {
        float aspect = (rs_.height_ > 0) ? (float)rs_.width_ / (float)rs_.height_ : 1.0f;
        render_detail::CbPerFrameDesc d;
        d.view         = viewMat;
        d.projection   = rs_.camera_.ProjectionRH(aspect);
        d.lightColor   = kCollisionLightColor;
        d.ambientColor = kCollisionAmbientColor;
        render_detail::WriteCbPerFrame(rs_.gfx_.get(), rs_.cbPerFrame_, d);
    }
    DrawWireLines(rs_.gfx_.get(), cmd, rs_.cbPerFrame_, lines);
}

// ============================================================================
// Light Markers
// ============================================================================

void DebugRenderer::RenderLightMarkers() {
    struct MarkerLight {
        Vector3f worldPos;
        Vector3f worldDir;
        Vector3f diffuse;
        bool     isDirectional;
        bool     enabled;
    };
    std::vector<MarkerLight> lights;
    Matrix44f viewMat;
    {
        std::lock_guard<std::mutex> lock(rs_.dataMutex_);
        for (auto& [h, mi] : rs_.models_) {
            if (mi->parentVisibility <= 0.02f) continue;
            for (const auto& L : mi->activeLights) {
                const bool dir = (L.kind == FrameState::LightKind::Directional);
                lights.push_back({
                    dir ? whiteout::transform_point(Vector3f{0,0,0}, mi->worldTransform)
                        : L.worldPos,
                    L.worldDir,
                    L.diffuse,
                    dir,
                    L.enabled
                });
            }
        }
        if (lights.empty()) return;
        viewMat = rs_.camera_.GetViewMatrix();
    }

    auto* cmd = rs_.gfx_->GetImmediateContext();
    cmd->BindPipeline(rs_.linePSO_);

    struct LV { Vector3f pos; Vector4f col; };
    std::vector<LV> verts;
    verts.reserve(lights.size() * 3 * 24 * 2 + lights.size() * 2);

    const float kMarkerRadius = 20.0f;
    const int   kSegs         = 24;
    for (const auto& m : lights) {
        Vector4f col = m.enabled
            ? Vector4f{ std::max(m.diffuse.x, 0.2f),
                        std::max(m.diffuse.y, 0.2f),
                        std::max(m.diffuse.z, 0.2f),
                        1.0f }
            : Vector4f{ 0.25f, 0.25f, 0.25f, 1.0f };

        const Vector3f c = m.worldPos;
        for (int plane = 0; plane < 3; ++plane) {
            for (int i = 0; i < kSegs; ++i) {
                float a0 = (float)i       / kSegs * 6.28318530f;
                float a1 = (float)(i + 1) / kSegs * 6.28318530f;
                float c0 = kMarkerRadius * std::cos(a0), s0 = kMarkerRadius * std::sin(a0);
                float c1 = kMarkerRadius * std::cos(a1), s1 = kMarkerRadius * std::sin(a1);
                Vector3f p0, p1;
                if      (plane == 0) { p0 = {c.x + c0, c.y + s0, c.z      }; p1 = {c.x + c1, c.y + s1, c.z      }; }
                else if (plane == 1) { p0 = {c.x + c0, c.y,      c.z + s0 }; p1 = {c.x + c1, c.y,      c.z + s1 }; }
                else                 { p0 = {c.x,      c.y + c0, c.z + s0 }; p1 = {c.x,      c.y + c1, c.z + s1 }; }
                verts.push_back({p0, col});
                verts.push_back({p1, col});
            }
        }

        if (m.isDirectional) {
            Vector3f d = m.worldDir;
            float    n = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
            if (n > 1e-6f) { d = { d.x/n, d.y/n, d.z/n }; } else { d = {0,0,1}; }
            const float L = kMarkerRadius * 4.0f;
            Vector3f tip = { c.x - d.x * L, c.y - d.y * L, c.z - d.z * L };
            verts.push_back({c,   col});
            verts.push_back({tip, col});
        }
    }

    if (verts.empty()) return;

    {
        float aspect = (rs_.height_ > 0) ? (float)rs_.width_ / (float)rs_.height_ : 1.0f;
        render_detail::CbPerFrameDesc d;
        d.view       = viewMat;
        d.projection = rs_.camera_.ProjectionRH(aspect);
        render_detail::WriteCbPerFrame(rs_.gfx_.get(), rs_.cbPerFrame_, d);
    }
    DrawWireLines(rs_.gfx_.get(), cmd, rs_.cbPerFrame_, verts);
}

// ============================================================================
// ViewCube
// ============================================================================

Rect DebugRenderer::GetViewCubeRect() const {
    int s = kViewCubeSize;
    int margin = 10;
    int cubeTop = margin + 28;
    return { rs_.width_ - s - margin, margin, rs_.width_ - margin, cubeTop + s };
}

void DebugRenderer::RenderViewCube() {
    if (vcCubeVB_ == gfx::BufferHandle::Invalid ||
        vcCubeIB_ == gfx::BufferHandle::Invalid) return;

    auto* cmd = rs_.gfx_->GetImmediateContext();

    int s = kViewCubeSize;
    int margin = 10;
    gfx::Viewport vp = {
        (float)(rs_.width_ - s - margin),
        (float)(margin + 28),
        (float)s, (float)s,
        0.0f, 1.0f
    };
    cmd->SetViewport(vp);

    auto* pt = rs_.primaryTarget();
    if (!pt) return;
    cmd->ClearDepth(pt->depth, 1.0f, 0);

    Matrix44f vcView;
    {
        std::lock_guard<std::mutex> lock(rs_.dataMutex_);
        float dist = 3.5f;
        float cosP = cosf(rs_.camera_.GetPitch()), sinP = sinf(rs_.camera_.GetPitch());
        float cosY = cosf(rs_.camera_.GetYaw()),   sinY = sinf(rs_.camera_.GetYaw());
        Vector3f eye = { dist * cosP * cosY, dist * cosP * sinY, dist * sinP };
        Vector3f tgt = { 0, 0, 0 };
        Vector3f up  = { 0, 0, 1 };
        vcView = Matrix44f::look_at_rh(eye, tgt, up);
    }
    Matrix44f vcProj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, 1.0f, 0.1f, 100.0f);

    {
        render_detail::CbPerFrameDesc d;
        d.view         = vcView;
        d.projection   = vcProj;
        d.lightDir     = render_detail::NormalizedLightDir4(kViewCubeLightDir);
        d.lightColor   = kViewCubeLightColor;
        d.ambientColor = kViewCubeAmbientColor;
        render_detail::WriteCbPerFrame(rs_.gfx_.get(), rs_.cbPerFrame_, d);
    }

    cmd->BindPipeline(viewCubePSO_);
    cmd->BindVertexBuffer(0, vcCubeVB_, sizeof(Vertex));
    cmd->BindIndexBuffer(vcCubeIB_, gfx::Format::R32_UINT);
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, rs_.cbPerFrame_);
    cmd->BindConstantBuffer(gfx::ShaderStage::Pixel,  0, rs_.cbPerFrame_);
    cmd->BindSampler(gfx::ShaderStage::Pixel, 0, rs_.samplerLinear_);
    if (vcFaceTex_ != gfx::TextureHandle::Invalid)
        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, vcFaceTex_);
    else
        cmd->BindShaderResource(gfx::ShaderStage::Pixel, 0, rs_.defaultTex_);
    cmd->DrawIndexed(36, 0, 0);

    cmd->BindPipeline(rs_.linePSO_);
    cmd->BindVertexBuffer(0, vcOutlineVB_, sizeof(LineVertex));
    cmd->BindConstantBuffer(gfx::ShaderStage::Vertex, 0, rs_.cbPerFrame_);
    cmd->Draw(24, 0);

    // Home button icon (above cube, only on hover)
    if (vcHovered_) {
        gfx::Viewport homeVp = {
            vp.x + (float)s * kViewCubeHomeOffset,
            vp.y - 24.0f,
            (float)s * 0.3f, 20.0f,
            0.0f, 1.0f
        };
        cmd->SetViewport(homeVp);

        {
            render_detail::CbPerFrameDesc d;
            d.projection   = Matrix44f::orthographic_rh(2.0f, 2.0f, -1.0f, 1.0f);
            d.ambientColor = {1, 1, 1, 1};
            d.extraParams  = {1, 0, 0, 0};
            render_detail::WriteCbPerFrame(rs_.gfx_.get(), rs_.cbPerFrame_, d);
        }

        if (vcHomeVB_ == gfx::BufferHandle::Invalid) {
            Vector4f hc = {0.7f, 0.7f, 0.7f, 1.0f};
            LineVertex house[] = {
                {{-0.4f, -0.6f, 0}, hc}, {{ 0.4f, -0.6f, 0}, hc},
                {{-0.4f, -0.6f, 0}, hc}, {{-0.4f,  0.0f, 0}, hc},
                {{ 0.4f, -0.6f, 0}, hc}, {{ 0.4f,  0.0f, 0}, hc},
                {{-0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc},
                {{ 0.5f,  0.0f, 0}, hc}, {{ 0.0f,  0.6f, 0}, hc},
                {{-0.5f,  0.0f, 0}, hc}, {{ 0.5f,  0.0f, 0}, hc},
            };
            vcHomeVB_ = rs_.gfx_->CreateBuffer({
                .size  = sizeof(house),
                .usage = gfx::BufferUsage::Vertex,
            }, house);
        }
        if (vcHomeVB_ != gfx::BufferHandle::Invalid) {
            cmd->BindVertexBuffer(0, vcHomeVB_, sizeof(LineVertex));
            cmd->Draw(12, 0);
        }
    }

    // Restore main viewport
    cmd->SetViewport({0, 0, (float)rs_.width_, (float)rs_.height_, 0.0f, 1.0f});

    // Restore main scene constant buffer (RenderFrame already wrote this, but
    // the ViewCube clobbered it with its own view/proj — put the main one
    // back so subsequent frames that skip the big RenderFrame setup keep
    // sane defaults).
    Matrix44f view, proj;
    {
        std::lock_guard<std::mutex> lock(rs_.dataMutex_);
        view = rs_.camera_.GetViewMatrix();
    }
    float aspect = (rs_.height_ > 0) ? (float)rs_.width_ / (float)rs_.height_ : 1.0f;
    proj = rs_.camera_.ProjectionRH(aspect);
    {
        render_detail::CbPerFrameDesc d;
        d.view         = view;
        d.projection   = proj;
        d.lightDir     = render_detail::NormalizedLightDir4(kDefaultLightDir);
        d.lightColor   = kGeosetLightColor;
        d.ambientColor = {kGeosetAmbientColor.x, kGeosetAmbientColor.y, kGeosetAmbientColor.z, 0.0f};
        render_detail::WriteCbPerFrame(rs_.gfx_.get(), rs_.cbPerFrame_, d);
    }
}

int DebugRenderer::HitTestViewCube(int mx, int my) const {
    Rect r = GetViewCubeRect();

    int cubeTop = r.top + 28;
    if (vcHovered_ && mx >= r.left && mx <= r.right && my >= r.top && my <= cubeTop)
        return 6;

    if (mx < r.left || mx > r.right || my < cubeTop || my > r.bottom)
        return -1;

    int s = kViewCubeSize;
    float vcX = (float)(rs_.width_ - s - 10);
    float vcY = 10.0f + 28.0f;

    Matrix44f vcView;
    {
        float dist = 3.5f;
        float cosP = cosf(rs_.camera_.GetPitch()), sinP = sinf(rs_.camera_.GetPitch());
        float cosY = cosf(rs_.camera_.GetYaw()),   sinY = sinf(rs_.camera_.GetYaw());
        Vector3f eye = { dist*cosP*cosY, dist*cosP*sinY, dist*sinP };
        Vector3f up  = { 0, 0, 1 };
        vcView = Matrix44f::look_at_rh(eye, {0,0,0}, up);
    }
    Matrix44f vcProj = Matrix44f::perspective_fov_rh(std::numbers::pi_v<float> / 4.0f, 1.0f, 0.1f, 100.0f);
    Matrix44f vp_mat = vcView * vcProj;

    Vector3f centers[] = {{0,.5f,0},{0,-.5f,0},{-.5f,0,0},{.5f,0,0},{0,0,.5f},{0,0,-.5f}};
    Vector3f normals[] = {{0,1,0},{0,-1,0},{-1,0,0},{1,0,0},{0,0,1},{0,0,-1}};

    float cosP = cosf(rs_.camera_.GetPitch()), sinP = sinf(rs_.camera_.GetPitch());
    float cosY = cosf(rs_.camera_.GetYaw()),   sinY = sinf(rs_.camera_.GetYaw());
    Vector3f camDir = { -cosP*cosY, -cosP*sinY, -sinP };

    int bestFace = -1;
    float bestDist = 1e9f;

    for (int i = 0; i < 6; i++) {
        float dot = normals[i].x*camDir.x + normals[i].y*camDir.y + normals[i].z*camDir.z;
        if (dot > -0.05f) continue;

        Vector3f c = centers[i];
        float cx = c.x*vp_mat.data[0][0] + c.y*vp_mat.data[1][0] + c.z*vp_mat.data[2][0] + vp_mat.data[3][0];
        float cy = c.x*vp_mat.data[0][1] + c.y*vp_mat.data[1][1] + c.z*vp_mat.data[2][1] + vp_mat.data[3][1];
        float cw = c.x*vp_mat.data[0][3] + c.y*vp_mat.data[1][3] + c.z*vp_mat.data[2][3] + vp_mat.data[3][3];
        if (fabsf(cw) < 1e-6f) continue;
        float ndcX = cx / cw;
        float ndcY = cy / cw;
        float spx = vcX + (float)s * (1.0f + ndcX) * 0.5f;
        float spy = vcY + (float)s * (1.0f - ndcY) * 0.5f;

        float dx = spx - mx;
        float dy = spy - my;
        float d = dx*dx + dy*dy;
        if (d < bestDist && d < (s*s*0.06f)) {
            bestDist = d;
            bestFace = i;
        }
    }
    return bestFace;
}

} // namespace WhiteoutDex
