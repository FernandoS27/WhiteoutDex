#pragma once
// ============================================================================
// WhiteoutDex Renderer — Adapter interfaces
//
// Phase 3 splits the legacy `IModelSource` god-interface into two narrow
// contracts:
//
//   - IModelDataSource: produces static parsed data (`Build()`). Used by
//     ModelTemplateManager when constructing a template; by the Max plugin
//     when snapshotting the live scene.
//
//   - IAnimationSource: produces a per-frame `FrameState` for a given
//     (sequenceIdx, timeMs, globalTimeMs, worldTransform, cameraPos).
//     Stateless w.r.t. sequence/time — every actor's `AnimationDriver`
//     carries its own sequence cursor and passes it on each call.
//
// `IModelSource` is kept as the union of both interfaces so existing
// adapter types (MdxModelAdapter, MaxSceneAdapter) and call sites compile
// during the migration. The granular `Get*()` accessors on IModelSource
// stay until Phase 4 (Actor + RenderModel) lets us replace them with a
// single Build() call.
// ============================================================================

#include "model_types.h"
#include "particle.h"
#include "ribbon.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteoutDex {

// Sequence info — animation segment metadata. One entry per MDX sequence.
struct SequenceInfo {
    std::string name;
    int         startMs = 0;
    int         endMs   = 0;
    // Game-units per second the unit covers when this sequence plays at
    // 1× speed. Sourced verbatim from the MDX SEQS chunk's `moveSpeed`
    // — Blizzard authors it on Walk / Run cycles so the in-game
    // movement code can sync foot-plant timing to actual displacement.
    // 0 = stationary (Stand, Attack, Death, ...).
    // Hosts that want to visualise locomotion can drift the actor +
    // camera target by `moveSpeed * dt` each tick while a non-zero
    // sequence is active, leaving world-space splats / particles to
    // fall behind. Engine reference: preview.exe scrolls the legacy
    // reference grid by this value instead of moving the actor; same
    // perception, opposite frame.
    float       moveSpeed = 0.0f;
    // True when the MDX SEQS-chunk flag bit 0 (NonLooping) is set —
    // the sequence plays once and holds its final frame instead of
    // wrapping back to startMs. Convention: Death / Decay / climax
    // poses are NonLooping; Stand / Walk / Attack loop. Hosts that
    // want to preview a NonLooping clip in a loop (animation editing,
    // breakdown views) override per-actor via Actor::ignoreNonLooping
    // — see model_instance.h.
    bool        nonLooping = false;
};

// ----------------------------------------------------------------------------
// ModelData — the full static-data snapshot a template (or a Max scene
// snapshot) produces. Aggregates everything that used to come from the
// granular Get*() accessors on IModelSource.
// ----------------------------------------------------------------------------

struct ModelData {
    std::vector<MeshData>              meshes;
    std::vector<TextureData>           textures;
    std::vector<MaterialData>          materials;
    SkeletonData                       skeleton;
    std::vector<SkinWeightData>        skinWeights;
    std::vector<ParticleEmitterConfig> pe2Configs;
    std::vector<RibbonEmitterConfig>   ribbonConfigs;
    std::vector<CollisionShapeData>    collisionConfigs;
    std::vector<AttachmentConfig>      attachmentConfigs;
    std::vector<PE1EmitterConfig>      pe1Configs;
    std::vector<EventObjectConfig>     eventObjects;
    std::vector<CameraPreset>          cameraPresets;
    std::vector<SequenceInfo>          sequences;
    // Global-sequence durations in milliseconds, indexed by the
    // EventObjectConfig::globalSequenceId. Empty when the model uses
    // none (the typical case).
    std::vector<uint32_t>              globalSequences;
};

// ----------------------------------------------------------------------------
// IModelDataSource — the static-data half of the old IModelSource.
// ----------------------------------------------------------------------------

class IModelDataSource {
public:
    virtual ~IModelDataSource() = default;

    // Build a complete ModelData snapshot. For MDX adapters this aggregates
    // the parsed data; for MaxSceneAdapter it captures the current Max scene.
    virtual ModelData Build() = 0;

    // Cross-model texture-cache query — when the host wires this in, adapters
    // skip BLP/CASC decode for textures the renderer already cached. Result
    // is advisory — see TextureAssetManager.h for the eviction-race contract.
    using TextureCacheQuery = std::function<bool(std::string_view)>;
    void SetTextureCacheQuery(TextureCacheQuery q) { textureCacheQuery_ = std::move(q); }

protected:
    bool IsTextureCached(std::string_view key) const {
        return textureCacheQuery_ && textureCacheQuery_(key);
    }

private:
    TextureCacheQuery textureCacheQuery_;
};

// ----------------------------------------------------------------------------
// IAnimationSource — the per-frame-evaluation half.
// ----------------------------------------------------------------------------

class IAnimationSource {
public:
    virtual ~IAnimationSource() = default;

    // Evaluate to a FrameState for the given sequence + time + scene context.
    // Implementations must be const — animation state machine state lives on
    // the per-actor AnimationDriver, not here.
    virtual FrameState Evaluate(int sequenceIdx, int timeMs, int globalTimeMs,
                                const Matrix44f& worldTransform,
                                const Vector3f&  cameraPos) const = 0;

    // Sequence list. Returned by value (the MdxModelAdapter builds it from
    // the parsed model; MaxSceneAdapter has none).
    virtual std::vector<SequenceInfo> GetSequences() const = 0;
};

// ----------------------------------------------------------------------------
// IModelSource — union of both narrow interfaces, plus the granular Get*
// accessors used by ModelTemplateManager and tests during the migration.
// ----------------------------------------------------------------------------

class IModelSource : public IModelDataSource, public IAnimationSource {
public:
    // ---- Granular static accessors ----
    virtual std::vector<MeshData>              GetMeshes()           = 0;
    virtual std::vector<TextureData>           GetTextures()         = 0;
    virtual std::vector<MaterialData>          GetMaterials()        = 0;
    virtual SkeletonData                       GetSkeleton()         = 0;
    virtual std::vector<SkinWeightData>        GetSkinWeights()      = 0;
    virtual std::vector<ParticleEmitterConfig> GetParticleConfigs()  = 0;
    virtual std::vector<RibbonEmitterConfig>   GetRibbonConfigs()    = 0;
    virtual std::vector<CollisionShapeData>    GetCollisionShapes()  = 0;
    virtual std::vector<AttachmentConfig>      GetAttachmentConfigs() { return {}; }
    virtual std::vector<PE1EmitterConfig>      GetPE1Configs()       { return {}; }
    virtual std::vector<EventObjectConfig>     GetEventObjects()     { return {}; }
    virtual std::vector<uint32_t>              GetGlobalSequences()  { return {}; }

    // Default Build() aggregates the granular methods. Adapters can override
    // for efficiency, but the default is correct.
    ModelData Build() override {
        ModelData d;
        d.meshes            = GetMeshes();
        d.textures          = GetTextures();
        d.materials         = GetMaterials();
        d.skeleton          = GetSkeleton();
        d.skinWeights       = GetSkinWeights();
        d.pe2Configs        = GetParticleConfigs();
        d.ribbonConfigs     = GetRibbonConfigs();
        d.collisionConfigs  = GetCollisionShapes();
        d.attachmentConfigs = GetAttachmentConfigs();
        d.pe1Configs        = GetPE1Configs();
        d.eventObjects      = GetEventObjects();
        d.globalSequences   = GetGlobalSequences();
        d.sequences         = GetSequences();
        return d;
    }
};

} // namespace WhiteoutDex
