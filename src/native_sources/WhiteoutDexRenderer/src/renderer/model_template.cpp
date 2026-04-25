// ============================================================================
// WhiteoutDex Renderer — ModelTemplate (impl)
// ============================================================================

#include "model_template.h"
#include "animation.h"           // SkinningData
#include "../io/mdx_model_adapter.h"  // for ~shared_ptr<MdxModelAdapter>

namespace WhiteoutDex {

ModelTemplate::ModelTemplate()  = default;
ModelTemplate::~ModelTemplate() = default;

void ModelTemplate::ReleaseGPU(gfx::IGFXDevice& gfx) {
    for (auto& g : sharedGeosets) {
        gfx.Destroy(g.ib);
        gfx.Destroy(g.unskinnedVb);
        gfx.Destroy(g.tangentVb);
        gfx.Destroy(g.boneVb);
    }
    sharedGeosets.clear();
    gpuUploaded = false;
}

} // namespace WhiteoutDex
