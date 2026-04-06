// MDLXExporter — MDX material mapper: ir::Material → mdx::Material
#pragma once

#include <core/intermediate_types.h>
#include <whiteout/models/mdx/structures.h>
#include "mdx_coord_transform.h"

class MdxMaterialMapper {
public:
    whiteout::mdx::Material map(const ir::Material& irMat,
                                 const ir::IRModel& model,
                                 uint32_t version);

private:
    whiteout::mdx::Layer mapLayer(const ir::MaterialLayer& irLayer,
                                   const ir::IRModel& model,
                                   uint32_t version);
};
