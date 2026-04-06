// MaxCore — Camera extractor
#pragma once

#include "../core/intermediate_types.h"
#include "../util/error_reporter.h"

#include <max.h>
#include <inode.h>

namespace core {

class CameraExtractor {
public:
    /// Extract camera data from a camera node.
    ir::Camera extract(INode* node, TimeValue t, ExportErrorReporter& reporter);
};

} // namespace core
