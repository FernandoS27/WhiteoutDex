// MDLXImporter — Wc3 scene builders for format-specific Max nodes
#pragma once

#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <max.h>
#include <iparamb2.h>
#include <string>
#include <vector>

namespace mdx_scene {
// Crash-safe diagnostic log helper. Appends to %TEMP%\mdlx_import_debug.log
// AND echoes to the MAXScript Listener. Used by the popcorn / corn-emitter
// path because that's where Max has been observed to crash mid-import.
void PopcornDiagLog(const std::string& msg);
} // namespace mdx_scene

namespace mdx_scene {
class TextureResolver;
}

namespace mdx_scene {

/// Build Wc3_Light scripted plugin helpers from IR light data.
class Wc3LightBuilder {
public:
    void buildLights(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build Wc3_AttachPoint scripted plugin helpers from IR attachment data.
class Wc3AttachmentBuilder {
public:
    void buildAttachments(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        const std::wstring& modelDir,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build Wc3Particles1 native plugin instances from IR PE1 data.
class Wc3Particle1Builder {
public:
    void buildParticles(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        const std::wstring& modelDir,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build Wc3Particles2 native plugin instances from IR PE2 data.
class Wc3Particle2Builder {
public:
    void buildParticles(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        const std::wstring& modelDir,
        TextureResolver* resolver,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build Wc3Ribbon native plugin instances from IR ribbon data.
class Wc3RibbonBuilder {
public:
    void buildRibbons(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        const std::vector<Mtl*>& materials,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build Wc3_Event scripted plugin helpers from IR event data.
class Wc3EventBuilder {
public:
    void buildEvents(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build Wc3CollisionSphere/Box scripted plugin helpers from IR collision data.
class Wc3CollisionBuilder {
public:
    void buildCollisions(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Apply Wc3VertexMod modifier to mesh nodes from IR vertex color data.
class Wc3VertexColorBuilder {
public:
    void applyVertexColors(
        const ir::IRModel& irModel,
        const std::vector<INode*>& meshNodes,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build BlizzPopcorn scripted plugin instances from IR corn emitter data (v1200).
class Wc3PopcornBuilder {
public:
    void buildPopcorn(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        const std::wstring& modelDir,
        TextureResolver* resolver,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build BlizzFaceFX scripted plugin instances from IR face effect data (v1200).
class Wc3FaceFxBuilder {
public:
    void buildFaceFX(
        const ir::IRModel& irModel,
        const std::vector<INode*>& nodeMap,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Build Max target cameras from IR camera data.
class Wc3CameraBuilder {
public:
    struct CameraNodePair {
        INode* cameraNode = nullptr;
        INode* targetNode = nullptr;
    };

    std::vector<CameraNodePair> buildCameras(
        const ir::IRModel& irModel,
        std::vector<INode*>& nodeMap,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

/// Create Note Track entries for animation sequences.
class Wc3SequenceBuilder {
public:
    void buildSequences(
        const ir::IRModel& irModel,
        Interface* gi,
        core::ExportErrorReporter& reporter);
};

} // namespace mdx_scene
