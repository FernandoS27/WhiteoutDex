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
    // HD mesh layout: slot 0 holds the ParticleSD stream (pos / normal /
    // color / tc0) matching our Vertex struct; slot 1 holds a dedicated
    // float4 tangent stream feeding ATTR7 (.xyz = tangent, .w =
    // handedness sign) that wc3_shaders/types/vs_io.slang declares for
    // the HD VS. Picked only by HD draws with real tangent data.
    MeshHDTangent = 4,
    // HD skinned mesh layout: adds slot 2 bone weights (ATTR5,
    // R8G8B8A8_UNORM) and bone indices (ATTR6, R8G8B8A8_UINT) so
    // vs/hd.bls's FourBoneSkinning policy can skin position/normal/
    // tangent in the VS. Slot 0 is the REST-pose Vertex data (not the
    // compute-skinned gg.vb) -- the HD VS multiplies by the bone
    // palette at vsCB3 to get animated geometry.
    MeshHDSkinned = 5,
    // HD skinned mesh without authored tangents. Slot 0 = PNCT0 rest
    // pose, slot 1 = bone weights (ATTR5) + indices (ATTR6). Picked
    // when the source MDX geoset has bones but omits the tangent
    // frame (rare for v1200 HD; common for classic meshes routed
    // through the HD program). The hasTangent permute is forced to 0
    // so the compiled VS doesn't read ATTR7.
    MeshHDSkinnedNoTangent = 6,
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
    // Folded into the PSO hash so HD (LH) and SD (RH) stacks keep
    // separate cached PSOs even when the rasterizer desc matches.
    bool                   lhClipSpace = false;
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
