#pragma once
// ============================================================================
// BlsPsoBuilder — builds gfx::PipelineHandles from a BlsProgram + permute
// indices + MatParams, mirroring CGxDevice::GetPipelineFromCurrentState
// (Previewd 0x1403f97b0). See docs/BLS_ShaderABI.md for the binding model
// these pipelines expect.
// ============================================================================

#include "bls_mat_params.h"
#include "bls_permuter.h"
#include "bls_program.h"
#include "gfx/gfx.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex::bls {

// Vertex layout family -- decides which attributes are bound from which slot.
// Matches Previewd's EGxVertexBufferFormat rows for the SD / SD_on_HD paths.
enum class VertexLayoutKind : uint8_t {
    MeshSD        = 0, // PNT0   (pos, normal, tc0)                    one VB slot
    MeshSDTc2     = 1, // PNT0T1 (pos, normal, tc0, tc1)                one VB slot
    MeshSDSkinned = 2, // MeshSD + separate bones VB slot 1
    ParticleSD    = 3, // PNCT0  (pos, normal, color, tc0)              one VB slot
};

struct PsoRequest {
    const BlsProgram*      program    = nullptr;
    uint32_t               vsIndex    = 0;
    uint32_t               psIndex    = 0;
    MatParams              material;
    VertexLayoutKind       layout     = VertexLayoutKind::MeshSD;
    gfx::PrimitiveTopology topology   = gfx::PrimitiveTopology::TriangleList;
    gfx::Format            rtvFormat  = gfx::Format::R8G8B8A8_UNORM;
    gfx::Format            dsvFormat  = gfx::Format::D24_UNORM_S8_UINT;
    bool                   wireframe  = false;
};

class BlsPsoBuilder {
public:
    explicit BlsPsoBuilder(gfx::IGFXDevice* device);
    ~BlsPsoBuilder();

    gfx::PipelineHandle GetOrBuild(const PsoRequest& request);

    void Clear();

private:
    gfx::IGFXDevice*                                   device_ = nullptr;
    std::unordered_map<uint64_t, gfx::PipelineHandle>  cache_;
};

} // namespace WhiteoutDex::bls
