// MaxCore — Animation dispatcher (orchestrator)
#pragma once

#include "../core/intermediate_types.h"
#include "../util/error_reporter.h"

#include <max.h>
#include <inode.h>

namespace core {

class AnimDispatcher {
public:
    struct Config {
        int tickInterval = 160;      // Default sampling interval
        float angleThreshold = 0.5f; // Degrees, for IK adaptive refinement
    };

    /// Bake all node animations for the given sequences.
    /// For each animated node, chooses the appropriate sampler based on controller type
    /// and writes results into irModel.nodeAnimations.
    void bakeAll(ir::IRModel& irModel,
                 const std::vector<ir::Sequence>& sequences,
                 const Config& config,
                 ExportErrorReporter& reporter);
};

} // namespace core
