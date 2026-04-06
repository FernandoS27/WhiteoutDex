// MDLXExporter — Export options
#pragma once

#include <cstdint>
#include <string>

struct MdxExportOptions {
    uint32_t version = 800;
    std::string modelName;

    bool exportAnimations = true;
    bool optimizeVertices = true;
    bool optimizeKeyframes = true;
    bool mergeGeosets = true;
    bool fixNormals = false;
    bool fixSharedNormals = true;
    bool exportSmoothgroups = false;
    bool keepUnusedBonesHelpers = false;
    bool disableSkinQuantize = false;

    int32_t animSampleInterval = 160; // ticks (160 = ~30fps subsample)
    float ikRefinementThreshold = 0.5f; // degrees
    float vertexMergeThreshold = 0.001f;
    float fixNormalsThreshold = 0.1f;
    uint32_t blendTime = 150;
    int32_t extentsType = 1;      // 1=animation-dependent, 2=global
    int32_t extentsPrecision = 6; // 0-10
};
