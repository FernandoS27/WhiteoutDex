// MDLXExporter — Export options
#pragma once

#include <cstdint>
#include <string>

struct MdxExportOptions {
    uint32_t version = 800;
    std::string modelName;

    // File output
    bool autoIncrementFilename = false;
    bool openFolderAfterExport = false;

    bool exportAnimations = true;
    bool optimizeVertices = true;
    bool optimizeKeyframes = true;
    bool mergeGeosets = true;
    bool fixSharedNormals = true;
    bool exportSmoothgroups = true;       // always on; not user-toggleable
    bool keepUnusedBonesHelpers = false;
    bool disableSkinQuantize = false;

    // Texture conversion (shared between BLP and DDS)
    bool texConvertEnabled = true;        // master toggle
    bool texGenerateMipmaps = true;
    bool texOverwriteExisting = false;
    // BLP-specific (used when version == 800)
    int32_t blpCompression = 0;           // 0 = Paletted (256 colors), 1 = JPEG
    int32_t blpJpegQuality = 75;          // 1..100
    bool    blpDithering = false;
    // DDS-specific (used for Reforged, version >= 1200)
    int32_t ddsFormat = 0;                // 0 = BC3, 1 = BC7

    int32_t animSampleInterval = 160; // ticks (160 = ~30fps subsample)
    float ikRefinementThreshold = 0.5f; // degrees
    float vertexMergeThreshold = 0.001f;
    uint32_t blendTime = 150;
    int32_t extentsType = 1;       // always 1 = animation-dependent
    int32_t extentsPrecision = 10; // 0-10 — default at max precision
};
