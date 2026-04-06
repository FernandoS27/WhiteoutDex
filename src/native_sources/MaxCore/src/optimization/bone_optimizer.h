// MaxCore — Bone optimizer (remove unused bones)
#pragma once

#include "../core/intermediate_types.h"

namespace core {

class BoneOptimizer {
public:
    /// Remove bones that are not referenced by any skin influence
    /// and have no meaningful animation. Reparent children of removed bones.
    /// Updates all indices in meshes and node animations.
    void optimize(ir::IRModel& model);
};

} // namespace core
