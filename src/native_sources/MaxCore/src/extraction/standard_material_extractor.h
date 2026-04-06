// MaxCore — Standard material extractor (fallback for StdMat2)
#pragma once

#include "../core/intermediate_types.h"
#include "../util/error_reporter.h"

#include <max.h>
#include <stdmat.h>

namespace core {

class StandardMaterialExtractor {
public:
    /// Extract a basic material from a StdMat2 or generic Mtl.
    /// This is a fallback; format-specific backends override with their own extractors.
    ir::Material extract(Mtl* mtl, TimeValue t, ExportErrorReporter& reporter);
};

} // namespace core
