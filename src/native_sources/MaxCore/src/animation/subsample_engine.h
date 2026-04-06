// MaxCore — Transform sampling engine
#pragma once

#include "../core/intermediate_types.h"

#include <max.h>
#include <inode.h>

namespace core {

class SubsampleEngine {
public:
    /// Evaluate local-space transform of a node relative to its parent.
    /// Decomposes into position, rotation, and scale.
    static void evaluateLocalTransform(INode* node, INode* parent, TimeValue t,
                                        Point3& outPos, Quat& outRot, Point3& outScl);

    /// Dense sample a node over a time range, producing position/rotation/scale tracks.
    /// tickInterval: sampling interval in ticks (e.g., 160 = every frame at 30fps, 320 = every 2 frames)
    struct SampleConfig {
        int tickInterval = 160;      // Default: every frame at 30fps
        bool adaptiveRefine = false;
        float angleThreshold = 0.5f; // Degrees, for adaptive refinement
        int maxDepth = 4;
    };

    static void sampleNode(INode* node, INode* parent,
                            TimeValue startTime, TimeValue endTime,
                            const SampleConfig& config,
                            ir::Vec3Track& outPos, ir::QuatTrack& outRot, ir::Vec3Track& outScl);

private:
    static void adaptiveRefine(INode* node, INode* parent,
                                TimeValue t0, TimeValue t1,
                                const Quat& q0, const Quat& q1,
                                float angleThreshold, int depth, int maxDepth,
                                ir::Vec3Track& posTrack, ir::QuatTrack& rotTrack,
                                ir::Vec3Track& sclTrack);
};

} // namespace core
