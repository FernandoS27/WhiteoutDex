#pragma once
// ============================================================================
// WhiteoutDex Renderer — ActorManager
//
// Flat owner of every Actor in the scene. Indexed by ActorId (alias for the
// existing 32-bit handle so the migration doesn't churn callers that still
// use raw handles).
//
// Phase 4 v1: thin wrapper over a `std::unordered_map<ActorId, unique_ptr<Actor>>`,
// with `Spawn` / `Despawn` / `Find` / `AllActors` / `Roots`. Hierarchy
// (`parent_` + `children_` ID lists) is the user's design F5 — added on
// Actor as a follow-up; for now hierarchy stays implicit through the existing
// AttachmentSlot + PE1 spawn mechanisms.
//
// In Phase 5, SceneManager owns the ActorManager and exposes
// `SpawnActorByPath` / `SpawnActorFromLiveSource`. For now `RenderService`
// owns one and forwards through its existing add/remove API.
// ============================================================================

#include "model_instance.h"   // Actor

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex {

using ActorId = uint32_t;

class ActorManager {
public:
    using Map = std::unordered_map<ActorId, std::unique_ptr<Actor>>;

    // Spawn — caller-supplied id (matches the existing nextModelHandle_ flow).
    // Returns a non-owning pointer to the freshly-created Actor.
    Actor* Spawn(ActorId id) {
        auto a = std::make_unique<Actor>();
        a->handle = id;
        Actor* raw = a.get();
        actors_[id] = std::move(a);
        return raw;
    }

    // Adopt a pre-built Actor (used by callers that need to populate it
    // before insertion — e.g. the existing AddModel path).
    Actor* Adopt(std::unique_ptr<Actor> actor) {
        if (!actor) return nullptr;
        const ActorId id = actor->handle;
        Actor* raw = actor.get();
        actors_[id] = std::move(actor);
        return raw;
    }

    // Remove. Returns the unique_ptr so callers can run teardown (ReleaseGPU,
    // particleService_.RemoveModel, replaceables_->UnregisterModel) before
    // the object is destroyed.
    std::unique_ptr<Actor> Despawn(ActorId id) {
        auto it = actors_.find(id);
        if (it == actors_.end()) return nullptr;
        auto out = std::move(it->second);
        actors_.erase(it);
        return out;
    }

    // Find returns a mutable `Actor*` even from a const-qualified manager.
    // The map's `unique_ptr<Actor>` storage is what's logically const here,
    // not the actor itself — mirroring the long-standing pattern in
    // RenderService (const helpers returning non-const Actor*).
    Actor* Find(ActorId id) const {
        auto it = actors_.find(id);
        return (it != actors_.end()) ? it->second.get() : nullptr;
    }

    bool   Empty() const { return actors_.empty(); }
    size_t Size()  const { return actors_.size(); }
    void   Clear()       { actors_.clear(); }

    // Direct access for the existing iteration patterns (RenderService walks
    // every Actor in many places). Returning the underlying map keeps the
    // structured-bindings code shape the existing call sites use.
    Map&       All()       { return actors_; }
    const Map& All() const { return actors_; }

    ActorId FirstId() const {
        return actors_.empty() ? 0 : actors_.begin()->first;
    }

private:
    Map actors_;
};

} // namespace WhiteoutDex
