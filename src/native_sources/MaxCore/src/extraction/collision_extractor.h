// MaxCore — Collision shape extractor
#pragma once

#include "../core/intermediate_types.h"
#include "../util/error_reporter.h"

#include <max.h>
#include <inode.h>

namespace core {

class CollisionExtractor {
public:
    /// Extract collision shape data from a classified collision node.
    /// For box shapes: reads dimensions from the object paramblock.
    /// For sphere shapes: reads radius.
    ir::CollisionShape extract(INode* node, ir::CollisionShape::Shape shapeType,
                                TimeValue t, ExportErrorReporter& reporter);
};

} // namespace core
