// MDLXImporter — Import options (core + MDX-specific)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ir {

struct CoreImportOptions {
    // ── Geometry ──
    bool importSkinning = true;

    // ── Materials ──
    bool importMaterials = true;
    bool importTextures = true;

    // ── Objects ──
    bool importObjects = true;
    bool importBones = true;
    bool importHelpers = true;
    bool importLights = true;
    bool importAttachments = true;
    bool importParticleEmitters1 = true;
    bool importParticleEmitters2 = true;
    bool importRibbonEmitters = true;
    bool importEventObjects = true;
    bool importCollisionShapes = true;
    bool importCameras = true;

    // ── Animations ──
    bool importAnimations = true;
    bool importTranslation = true;
    bool importRotation = true;
    bool importScale = true;
    bool importParameterAnimations = true;
    bool importUVAnimations = true;
    bool importTextureAnimations = true;
    bool importVisibility = true;
    bool importColorAnimations = true;

    // ── Helper Options ──
    bool importHelpersAsPointHelpers = true;

    // ── Optimization ──
    bool optimizeGeometry = true;
    bool optimizeBonesAndHelpers = true;

    // ── Mode ──
    enum class ImportMode { NewScene, Merge };
    ImportMode mode = ImportMode::NewScene;

    // ── Fast Settings Preset ──
    enum class Preset {
        Custom, StaticNoMaterials, StaticMaterials,
        AnimatedNoSkinning, AnimatedNoObjects, All
    };
    Preset preset = Preset::All;
};

} // namespace ir

struct MdlxImportOptions {
    ir::CoreImportOptions core;

    // MDX-specific (auto-detected from file)
    uint32_t detectedVersion = 0;

    // ── Texture Search ──
    // No on/off flags: the resolver always tries to open whichever paths
    // are non-empty. CASC + MPQ open calls log their own failures and the
    // resolver moves on, matching WhiteoutFlakes' FileContentProvider.
    std::wstring mpqDirectory;
    std::wstring cascDirectory;
    // The user's MPQ load order from WhiteoutDex Settings, already resolved to
    // absolute paths and in priority order. When non-empty it REPLACES the
    // scan of `mpqDirectory` — that is what lets an archive living outside the
    // game folder be searched at all, and what makes an archive the user took
    // out of the order actually stay out. Empty means "never customised", and
    // the resolver scans `mpqDirectory` the way it always did.
    std::vector<std::wstring> mpqArchives;

    // ── Reforged v1200 Objects ──
    bool importCornEmitters = true;
    bool importFaceFX = true;
};
