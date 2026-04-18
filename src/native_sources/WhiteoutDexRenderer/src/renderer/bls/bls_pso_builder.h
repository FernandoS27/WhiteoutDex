#pragma once
// ============================================================================
// BlsPsoBuilder — builds gfx::PipelineHandles from BlsProgram + permute indices
// + render state. Mirrors CGxDevice::GetPipelineFromCurrentState (Previewd
// 0x1403f97b0) but drops the fields our renderer does not yet model (stencil,
// depth bias, multi-vertex-buffer, root signatures).
// ============================================================================

#include "bls_permuter.h"
#include "bls_program.h"
#include "gfx/gfx.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex::bls {

enum class AlphaMode : uint8_t {
    Opaque      = 0,
    AlphaBlend  = 1,
    Additive    = 2,
    AlphaToMask = 3,
    Modulate    = 4,
    Modulate2x  = 5,
    AddAlpha    = 6,
};

enum class CullOverride : uint8_t { Back = 0, None = 1, Front = 2 };
enum class DepthOverride : uint8_t { Default = 0, NoWrite = 1, Disabled = 2 };

struct PsoRequest {
    const BlsProgram*      program = nullptr;
    uint32_t               vsIndex = 0;
    uint32_t               psIndex = 0;
    AlphaMode              alpha   = AlphaMode::Opaque;
    CullOverride           cull    = CullOverride::Back;
    DepthOverride          depth   = DepthOverride::Default;
    gfx::PrimitiveTopology topology = gfx::PrimitiveTopology::TriangleList;
    gfx::Format            rtvFormat = gfx::Format::R8G8B8A8_UNORM;
    gfx::Format            dsvFormat = gfx::Format::D24_UNORM_S8_UINT;
    bool                   wireframe = false;
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
