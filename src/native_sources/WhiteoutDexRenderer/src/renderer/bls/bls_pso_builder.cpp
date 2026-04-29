#include "bls_pso_builder.h"

#include <array>
#include <cstddef>

namespace WhiteoutDex::bls {

namespace {

// ============================================================================
// Input layouts per VertexLayoutKind. All semantics are "ATTR" -- Blizzard's
// SD/SD_on_HD/HD VS shaders bind their inputs by generic semantic number, not
// POSITION/NORMAL/TEXCOORD. See docs/BLS_ShaderABI.md for the attribute
// numbering:
//   ATTR0 position, ATTR1 normal, ATTR2 color, ATTR3 tc0, ATTR4 tc1,
//   ATTR5 bone weights (float4 normalized), ATTR6 bone indices (uint4).
// ============================================================================

// MeshSD (PNT0) -- offsets match MeshVertexSD { pos, normal, uv0, uv1 }
constexpr gfx::InputElement kMeshSD[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT, 0,  0 }, // position
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT, 12, 0 }, // normal
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,    24, 0 }, // tc0
};

// MeshSDTc2 (PNT0T1) -- adds tc1
constexpr gfx::InputElement kMeshSDTc2[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT, 0,  0 },
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT, 12, 0 },
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,    24, 0 },
    { "ATTR", 4, gfx::Format::R32G32_FLOAT,    32, 0 },
};

// MeshSDSkinned -- geometry on slot 0, bone weights/indices on slot 1.
constexpr gfx::InputElement kMeshSDSkinned[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT, 0,  0 },
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT, 12, 0 },
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,    24, 0 },
    { "ATTR", 5, gfx::Format::R8G8B8A8_UNORM,  0,  1 }, // bone weights (normalized float4)
    { "ATTR", 6, gfx::Format::R8G8B8A8_UINT,   4,  1 }, // bone indices (uint4)
};

// ParticleSD (PNCT0) -- pos, normal, color (float4), tc0
constexpr gfx::InputElement kParticleSD[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT,    0,  0 },
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT,    12, 0 },
    { "ATTR", 2, gfx::Format::R32G32B32A32_FLOAT, 24, 0 },
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,       40, 0 },
};

// ParticleSDSkinned -- ParticleSD geometry on slot 0 + bone weights/
// indices side-stream on slot 1. Matches the layout the SD VS expects
// when numWeights=4 is picked, mirroring kMeshHDSkinnedNoTangent's
// slot-1 bone packing.
constexpr gfx::InputElement kParticleSDSkinned[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT,    0,  0 },
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT,    12, 0 },
    { "ATTR", 2, gfx::Format::R32G32B32A32_FLOAT, 24, 0 },
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,       40, 0 },
    { "ATTR", 5, gfx::Format::R8G8B8A8_UNORM,     0,  1 }, // weights
    { "ATTR", 6, gfx::Format::R8G8B8A8_UINT,      4,  1 }, // bone indices
};

// MeshHDTangent (ParticleSD geometry + tangent side-stream on slot 1).
// ATTR7 = float4 tangent (.xyz direction, .w handedness), consumed by
// wc3_shaders/hd_vs.slang when the hasTangent permute is picked. The
// geometry stream on slot 0 is the same PNCT0 layout as particles so we
// don't have to duplicate staging -- the tangent side-stream is
// optional and only bound when the source geoset had real tangent data.
constexpr gfx::InputElement kMeshHDTangent[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT,    0,  0 },
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT,    12, 0 },
    { "ATTR", 2, gfx::Format::R32G32B32A32_FLOAT, 24, 0 },
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,       40, 0 },
    { "ATTR", 7, gfx::Format::R32G32B32A32_FLOAT, 0,  1 }, // tangent
};

// MeshHDSkinned -- MeshHDTangent + slot 2 bone weights/indices feeding
// FourBoneSkinning in vs/hd.bls. Slot 0 MUST carry rest-pose positions/
// normals (geo.unskinnedVb); the VS performs the skin-blend itself using
// the bone palette uploaded to vsCB3.
constexpr gfx::InputElement kMeshHDSkinned[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT,    0,  0 },
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT,    12, 0 },
    { "ATTR", 2, gfx::Format::R32G32B32A32_FLOAT, 24, 0 },
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,       40, 0 },
    { "ATTR", 7, gfx::Format::R32G32B32A32_FLOAT, 0,  1 }, // tangent
    { "ATTR", 5, gfx::Format::R8G8B8A8_UNORM,     0,  2 }, // weights
    { "ATTR", 6, gfx::Format::R8G8B8A8_UINT,      4,  2 }, // bone indices
};

// MeshHDSkinnedNoTangent -- FourBoneSkinning without ATTR7. Bone data
// collapses onto slot 1 (no tangent stream). Paired with the hasTangent=0
// HD VS permute so the compiler-stripped ATTR7 matches the IA layout.
constexpr gfx::InputElement kMeshHDSkinnedNoTangent[] = {
    { "ATTR", 0, gfx::Format::R32G32B32_FLOAT,    0,  0 },
    { "ATTR", 1, gfx::Format::R32G32B32_FLOAT,    12, 0 },
    { "ATTR", 2, gfx::Format::R32G32B32A32_FLOAT, 24, 0 },
    { "ATTR", 3, gfx::Format::R32G32_FLOAT,       40, 0 },
    { "ATTR", 5, gfx::Format::R8G8B8A8_UNORM,     0,  1 }, // weights
    { "ATTR", 6, gfx::Format::R8G8B8A8_UINT,      4,  1 }, // bone indices
};

std::span<const gfx::InputElement> LayoutFor(VertexLayoutKind k) {
    switch (k) {
        case VertexLayoutKind::MeshSD:        return {kMeshSD,        std::size(kMeshSD)};
        case VertexLayoutKind::MeshSDTc2:     return {kMeshSDTc2,     std::size(kMeshSDTc2)};
        case VertexLayoutKind::MeshSDSkinned: return {kMeshSDSkinned, std::size(kMeshSDSkinned)};
        case VertexLayoutKind::ParticleSD:    return {kParticleSD,    std::size(kParticleSD)};
        case VertexLayoutKind::ParticleSDSkinned:
            return {kParticleSDSkinned, std::size(kParticleSDSkinned)};
        case VertexLayoutKind::MeshHDTangent: return {kMeshHDTangent, std::size(kMeshHDTangent)};
        case VertexLayoutKind::MeshHDSkinned: return {kMeshHDSkinned, std::size(kMeshHDSkinned)};
        case VertexLayoutKind::MeshHDSkinnedNoTangent:
            return {kMeshHDSkinnedNoTangent, std::size(kMeshHDSkinnedNoTangent)};
    }
    return {kMeshSD, std::size(kMeshSD)};
}

// ============================================================================
// Blend / depth / raster derived from MatParams. The factor table matches
// Previewd's s_prismSrcBlend / s_prismDstBlend arrays (indexed by
// EGxMatAlphaOp).
// ============================================================================

gfx::BlendDesc BlendFor(GxMatAlpha alpha) {
    // Factor tables verified against Previewd's s_prismSrcBlend /
    // s_prismDstBlend / s_prismSrcBlendAlpha / s_prismDstBlendAlpha at
    // 0x142cd1b90..0x142cd1bc0. The engine uses different factors for the
    // color channel vs. the alpha channel in most modes (Add/Modulate/
    // Modulate2X) -- the alpha-channel factors rarely matter for
    // R8G8B8A8_UNORM swap-chains but matching them avoids surprises on
    // non-trivial render targets.
    gfx::BlendDesc bd{};
    switch (alpha) {
        case GxMatAlpha::Opaque:
        case GxMatAlpha::AlphaKey:
            // AlphaKey is a hard discard in the PS (alpha-ref CB slot);
            // the blend stage is off.
            bd.enable = false;
            break;
        case GxMatAlpha::Blend:
            // src=SrcAlpha, dst=InvSrcAlpha; srcA=One, dstA=Zero.
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::SrcAlpha;
            bd.dstColor = gfx::BlendFactor::InvSrcAlpha;
            bd.srcAlpha = gfx::BlendFactor::One;
            bd.dstAlpha = gfx::BlendFactor::Zero;
            break;
        case GxMatAlpha::Add:
            // src=SrcAlpha, dst=One; srcA=Zero, dstA=One.
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::SrcAlpha;
            bd.dstColor = gfx::BlendFactor::One;
            bd.srcAlpha = gfx::BlendFactor::Zero;
            bd.dstAlpha = gfx::BlendFactor::One;
            break;
        case GxMatAlpha::Modulate:
            // src=DstColor, dst=Zero; srcA=DstAlpha (8), dstA=Zero.
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::DstColor;
            bd.dstColor = gfx::BlendFactor::Zero;
            // gfx::BlendFactor lacks DstAlpha; Zero on srcA matches visually
            // for R8G8B8A8_UNORM (we never read the alpha channel of the RT).
            bd.srcAlpha = gfx::BlendFactor::Zero;
            bd.dstAlpha = gfx::BlendFactor::Zero;
            break;
        case GxMatAlpha::Modulate2X:
            // src=DstColor, dst=SrcColor; srcA=DstAlpha, dstA=SrcAlpha.
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::DstColor;
            bd.dstColor = gfx::BlendFactor::SrcColor;
            bd.srcAlpha = gfx::BlendFactor::Zero;
            bd.dstAlpha = gfx::BlendFactor::Zero;
            break;
    }
    return bd;
}

gfx::DepthStencilDesc DepthFor(const MatParams& m) {
    gfx::DepthStencilDesc ds{};
    ds.depthTest    = m.DepthTestEnabled();
    ds.depthWrite   = m.DepthWriteEnabled();
    ds.depthCompare = gfx::CompareOp::LessEqual;
    return ds;
}

gfx::RasterizerDesc RasterFor(const MatParams& m, bool wireframe, bool lhClipSpace) {
    (void)lhClipSpace;  // reserved for future per-stack state (depth-clear etc.)
    gfx::RasterizerDesc r{};
    r.cull     = m.CullEnabled() ? gfx::CullMode::Back : gfx::CullMode::None;
    r.fill     = wireframe ? gfx::FillMode::Wireframe : gfx::FillMode::Solid;
    // MDX triangles project as CCW in clip space under both our RH and LH pipelines.
    r.frontCCW = true;
    return r;
}

uint64_t HashRequest(const PsoRequest& r) {
    uint64_t k = reinterpret_cast<uintptr_t>(r.program);
    k ^= uint64_t(r.vsIndex) * 0x9E3779B185EBCA87ull;
    k ^= uint64_t(r.psIndex) * 0xC2B2AE3D27D4EB4Full;
    uint32_t bits =
        (uint32_t(r.material.alpha)      & 0x07u)        |
        ((r.material.disables            & 0x1Fu) << 3)  | // low 5 disable bits = render-affecting
        ((uint32_t(r.layout)             & 0x03u) << 8)  |
        ((uint32_t(r.topology)           & 0x03u) << 10) |
        ((uint32_t(r.rtvFormat)          & 0xFFu) << 12) |
        ((uint32_t(r.dsvFormat)          & 0xFFu) << 20) |
        ((r.wireframe ? 1u : 0u)                  << 28) |
        ((r.lhClipSpace ? 1u : 0u)                << 29) |
        // Depth-prepass clones differ from the color pass only in colorMask
        // off (kDisableBit8) — fold it into the key so they cache separately.
        ((r.material.ColorWriteEnabled() ? 0u : 1u) << 30);
    k ^= uint64_t(bits) * 0xFF51AFD7ED558CCDull;
    return k;
}

} // namespace

BlsPsoBuilder::BlsPsoBuilder(gfx::IGFXDevice* device) : device_(device) {}

BlsPsoBuilder::~BlsPsoBuilder() { Clear(); }

gfx::PipelineHandle BlsPsoBuilder::GetOrBuild(const PsoRequest& request) {
    if (!device_ || !request.program || !request.program->IsValid()) {
        return gfx::PipelineHandle::Invalid;
    }
    if (request.vsIndex >= request.program->vs->PermuteCount() ||
        request.psIndex >= request.program->ps->PermuteCount()) {
        return gfx::PipelineHandle::Invalid;
    }

    const uint64_t key = HashRequest(request);
    if (auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }

    gfx::GraphicsPipelineDesc desc{};
    desc.vs           = request.program->vs->permuteHandles[request.vsIndex];
    desc.ps           = request.program->ps->permuteHandles[request.psIndex];
    desc.inputLayout  = LayoutFor(request.layout);
    desc.topology     = request.topology;
    desc.blend        = BlendFor(request.material.alpha);
    desc.blend.colorWrite = request.material.ColorWriteEnabled();
    desc.depthStencil = DepthFor(request.material);
    desc.rasterizer   = RasterFor(request.material, request.wireframe, request.lhClipSpace);
    desc.rtvFormat    = request.rtvFormat;
    desc.dsvFormat    = request.dsvFormat;

    gfx::PipelineHandle pso = device_->CreateGraphicsPipeline(desc);
    if (pso != gfx::PipelineHandle::Invalid) {
        cache_.emplace(key, pso);
    }
    return pso;
}

void BlsPsoBuilder::Clear() {
    if (!device_) { cache_.clear(); return; }
    for (auto& [k, pso] : cache_) {
        device_->Destroy(pso);
    }
    cache_.clear();
}

} // namespace WhiteoutDex::bls
