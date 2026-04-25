#pragma once
// ============================================================================
// WhiteoutDex Renderer — Abstract Model Source Interface
// Adapters (Max scene, MDX file, etc.) implement this interface.
// The HOST calls Get*() and Evaluate() on its own thread and passes
// the results to the Renderer. The Renderer never calls back into the adapter.
// ============================================================================

#include "model_types.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace WhiteoutDex {

class IModelSource {
public:
    virtual ~IModelSource() = default;

    // ── Cross-model texture-cache query ─────────────────────────────────
    // Optional callback the host wires up before triggering texture loads
    // (CollectScene for the Max adapter, the first GetTextures call for
    // MDX). Adapters call IsTextureCached(key) before the BLP/CASC decode
    // path; on hit they emit a TextureData with sharedKey set and pixels
    // empty, signalling the renderer to borrow from TextureAssetManager's
    // cross-model cache instead of paying the decode + upload cost a
    // second time. Result is advisory — see TextureAssetManager.h for
    // the eviction-race contract.
    using TextureCacheQuery = std::function<bool(std::string_view)>;
    void SetTextureCacheQuery(TextureCacheQuery q) { textureCacheQuery_ = std::move(q); }

    // ---- Static data (called once when loading a model) ----
    virtual std::vector<MeshData>              GetMeshes()           = 0;
    virtual std::vector<TextureData>           GetTextures()         = 0;
    virtual std::vector<MaterialData>          GetMaterials()        = 0;
    virtual SkeletonData                       GetSkeleton()         = 0;
    virtual std::vector<SkinWeightData>        GetSkinWeights()      = 0;
    virtual std::vector<ParticleEmitterConfig> GetParticleConfigs()  = 0;
    virtual std::vector<RibbonEmitterConfig>   GetRibbonConfigs()    = 0;
    virtual std::vector<CollisionShapeData>    GetCollisionShapes()  = 0;

    // ---- Attachment configs ----
    virtual std::vector<AttachmentConfig>     GetAttachmentConfigs() { return {}; }

    // ---- PE1 (model particle emitter) configs ----
    virtual std::vector<PE1EmitterConfig>     GetPE1Configs()       { return {}; }

    // ---- Sequence control ----
    // SetActiveSequence selects which animation sequence Evaluate() uses.
    // For the Max adapter this is a no-op (Max controls the timeline).
    // For the MDX adapter this sets the [seqStart, seqEnd] range.
    // Index -1 means no sequence (rest pose).
    virtual void SetActiveSequence(int sequenceIndex) = 0;

    // ---- Camera info for billboard evaluation ----
    // Called before Evaluate() each frame. Default no-op (Max handles billboarding).
    virtual void SetCameraPosition(float x, float y, float z) { (void)x; (void)y; (void)z; }

    // ---- Per-frame (called by the HOST every frame on its own thread) ----
    // globalTimeMs = wall-clock milliseconds since model load (for global
    // sequences that run independently of the active animation). If -1,
    // falls back to timeMs (MaxSceneAdapter does not need separate timing).
    virtual FrameState Evaluate(int timeMs, int globalTimeMs = -1) = 0;

    // ---- Sequence info (for playback UI) ----
    struct SequenceInfo {
        std::string name;
        int startMs, endMs;
    };
    virtual std::vector<SequenceInfo> GetSequences() = 0;

protected:
    // Adapter-side helper: true when the host has wired a cache query
    // and the renderer reports `key` already cached. Falls back to false
    // (decode normally) when no host has wired a query.
    bool IsTextureCached(std::string_view key) const {
        return textureCacheQuery_ && textureCacheQuery_(key);
    }

private:
    TextureCacheQuery textureCacheQuery_;
};

} // namespace WhiteoutDex
