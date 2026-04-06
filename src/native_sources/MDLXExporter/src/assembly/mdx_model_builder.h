// MDLXExporter — MDX model builder: IR → mdx::Model
#pragma once

#include <core/intermediate_types.h>
#include "../mdx_export_options.h"
#include <whiteout/models/mdx/types.h>
#include <whiteout/models/mdx/structures.h>

class MdxModelBuilder {
public:
    whiteout::mdx::Model build(const ir::IRModel& ir, const MdxExportOptions& opts);
};
