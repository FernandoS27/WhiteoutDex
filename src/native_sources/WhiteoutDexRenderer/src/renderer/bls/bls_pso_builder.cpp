#include "bls_pso_builder.h"

#include <array>

namespace WhiteoutDex::bls {

namespace {

// Single input layout used by every BLS program we currently bind. Matches
// WhiteoutDex::Vertex (position, normal, color, uv). When we add shader
// programs that expect a different layout we'll add a per-GxShaderID lookup.
const gfx::InputElement kVertexLayout[] = {
    { "POSITION", 0, gfx::Format::R32G32B32_FLOAT,    0  },
    { "NORMAL",   0, gfx::Format::R32G32B32_FLOAT,    12 },
    { "COLOR",    0, gfx::Format::R32G32B32A32_FLOAT, 24 },
    { "TEXCOORD", 0, gfx::Format::R32G32_FLOAT,       40 },
};

gfx::BlendDesc BlendForAlpha(AlphaMode alpha) {
    gfx::BlendDesc bd{};
    switch (alpha) {
        case AlphaMode::Opaque:
            bd.enable = false;
            break;
        case AlphaMode::AlphaBlend:
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::SrcAlpha;
            bd.dstColor = gfx::BlendFactor::InvSrcAlpha;
            bd.srcAlpha = gfx::BlendFactor::One;
            bd.dstAlpha = gfx::BlendFactor::InvSrcAlpha;
            break;
        case AlphaMode::Additive:
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::SrcAlpha;
            bd.dstColor = gfx::BlendFactor::One;
            bd.srcAlpha = gfx::BlendFactor::One;
            bd.dstAlpha = gfx::BlendFactor::One;
            break;
        case AlphaMode::AlphaToMask:
            bd.enable          = false;
            bd.alphaToCoverage = true;
            break;
        case AlphaMode::Modulate:
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::Zero;
            bd.dstColor = gfx::BlendFactor::SrcColor;
            bd.srcAlpha = gfx::BlendFactor::Zero;
            bd.dstAlpha = gfx::BlendFactor::SrcAlpha;
            break;
        case AlphaMode::Modulate2x:
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::DstColor;
            bd.dstColor = gfx::BlendFactor::SrcColor;
            bd.srcAlpha = gfx::BlendFactor::DstColor;
            bd.dstAlpha = gfx::BlendFactor::SrcColor;
            break;
        case AlphaMode::AddAlpha:
            bd.enable   = true;
            bd.srcColor = gfx::BlendFactor::One;
            bd.dstColor = gfx::BlendFactor::One;
            bd.srcAlpha = gfx::BlendFactor::One;
            bd.dstAlpha = gfx::BlendFactor::One;
            break;
    }
    return bd;
}

gfx::DepthStencilDesc DepthForOverride(DepthOverride d) {
    gfx::DepthStencilDesc ds{};
    switch (d) {
        case DepthOverride::Default:  ds.depthTest = true;  ds.depthWrite = true;  break;
        case DepthOverride::NoWrite:  ds.depthTest = true;  ds.depthWrite = false; break;
        case DepthOverride::Disabled: ds.depthTest = false; ds.depthWrite = false; break;
    }
    ds.depthCompare = gfx::CompareOp::LessEqual;
    return ds;
}

gfx::RasterizerDesc RasterForState(CullOverride cull, bool wireframe) {
    gfx::RasterizerDesc r{};
    switch (cull) {
        case CullOverride::Back:  r.cull = gfx::CullMode::Back;  break;
        case CullOverride::None:  r.cull = gfx::CullMode::None;  break;
        case CullOverride::Front: r.cull = gfx::CullMode::Front; break;
    }
    r.fill     = wireframe ? gfx::FillMode::Wireframe : gfx::FillMode::Solid;
    r.frontCCW = false;
    return r;
}

uint64_t HashRequest(const PsoRequest& r) {
    // Pack the cache key into a single 64-bit value. Program pointer disambiguates
    // between different BlsProgram instances (per-catalog uniqueness).
    uint64_t k = reinterpret_cast<uintptr_t>(r.program);
    k ^= uint64_t(r.vsIndex) * 0x9E3779B185EBCA87ull;
    k ^= uint64_t(r.psIndex) * 0xC2B2AE3D27D4EB4Full;
    uint32_t bits =
        (uint32_t(r.alpha)                 & 0x0F)        |
        ((uint32_t(r.cull)                 & 0x03) << 4)  |
        ((uint32_t(r.depth)                & 0x03) << 6)  |
        ((uint32_t(r.topology)             & 0x03) << 8)  |
        ((uint32_t(r.rtvFormat)            & 0xFF) << 10) |
        ((uint32_t(r.dsvFormat)            & 0xFF) << 18) |
        ((r.wireframe ? 1u : 0u)                   << 26);
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
    desc.inputLayout  = std::span<const gfx::InputElement>(kVertexLayout, std::size(kVertexLayout));
    desc.topology     = request.topology;
    desc.blend        = BlendForAlpha(request.alpha);
    desc.depthStencil = DepthForOverride(request.depth);
    desc.rasterizer   = RasterForState(request.cull, request.wireframe);
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
