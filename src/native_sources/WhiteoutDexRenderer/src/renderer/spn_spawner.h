#pragma once
// ============================================================================
// SpnSpawner — manages sub-MDX actor lifecycles spawned by SPN EventObjects.
//
// MDX EventObjects with the "SPN" prefix instantiate a separate model file
// (resolved through Splats/SpawnData.slk) at the firing node's world
// transform, play its first sequence once, then disappear. We model that
// as a regular child Actor pinned at the spawn-time worldTransform with
// an explicit expiry stamp; the spawner is just the bookkeeping layer
// that drives async-template-load → promote-to-Actor → expire-on-duration
// without polluting the PE1 lifecycle (which has its own per-frame motion
// integration that doesn't apply to one-shot spawns).
//
// Lives next to RenderService (and is a friend) so the actual GPU
// resource lifecycle goes through the same paths as PE1 children — only
// the expiry timing and "play sequence 0 once" behaviour are bespoke.
// ============================================================================

#include "types.h"   // Matrix44f
#include <cstdint>
#include <string>
#include <vector>

namespace WhiteoutDex {

class RenderService;

class SpnSpawner {
public:
    explicit SpnSpawner(RenderService& rs) : rs_(rs) {}

    // Schedule a sub-MDX spawn at the parent node's world transform.
    // The template loads asynchronously through ModelTemplateManager;
    // the spawn promotes to a real Actor on the first Tick that finds
    // it ready (matches the behaviour of attachments in
    // EvaluateAttachmentChildren).
    void Spawn(uint32_t parentActor,
               const std::string& mdxPath,
               const Matrix44f&   parentNodeWorld,
               int                nowMs);

    // Per-frame: promote any ready pending loads, then age out actors
    // whose first sequence has finished. `nowMs` is the renderer's
    // global animation clock (scene_->GetAnimationTime()).
    void Tick(int nowMs);

    // Immediate teardown — used when RenderService unloads a parent
    // actor. Removes every active spawn whose parent matches.
    void RemoveSpawnsOf(uint32_t parentActor);

    // Drop everything (called from RenderService::ShutdownDevice).
    void Clear();

private:
    struct Pending {
        uint32_t    parentActor;
        std::string mdxPath;
        Matrix44f   parentWorld;
        int         birthMs;
    };
    struct Active {
        uint32_t parentActor;
        uint32_t handle;
        int      expiryMs;
    };

    RenderService&       rs_;
    std::vector<Pending> pending_;
    std::vector<Active>  active_;
};

} // namespace WhiteoutDex
