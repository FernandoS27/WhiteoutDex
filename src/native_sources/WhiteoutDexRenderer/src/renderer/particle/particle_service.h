#pragma once
// ============================================================================
// ParticleService — registry + tick/render layer over the PE2 emitters.
//
// Scope for the initial landing (see docs/PARTICLEEMITTERS2.md §2.1):
//  - Owns PlaneEmitter instances keyed by (ModelId, emitterId).
//  - Exposes a global emission scaler and a pluggable fog sampler.
//  - Simulate(dt) ticks every registered emitter.
//  - BuildGeometry(view) populates a per-emitter draw-list so the existing
//    DX11 path in render_service can keep batching per-model.
//
// The service coexists with the legacy ParticleSystem (see particle.h). The
// render_service will only dispatch through this service once an emitter is
// registered via the new AddPlaneEmitter path; everything else continues to
// use the legacy code until Phase 6 cut-over.
//
// -------- Visible flag contract --------------------------------------------
// Emitter2::InternalUpdate clears the Visible flag (bit 0) at the end of each
// simulation step, matching the RE engine's "anim parent re-raises Visible
// each frame" model. In our render-service integration Simulate always runs
// before BuildGeometry, so the flag would always be false at render time.
// BuildGeometry therefore does NOT gate on Visible — emission is gated via
// Enabled() inside InternalUpdate (which does need Visible set when the caller
// wants particles to spawn), and rendering is gated only on Pool().AliveCount().
// The practical sequence for a consumer is:
//
//   for each emitter:
//       emitter->SetVisible(true)       // required for emission
//       (set other per-frame state)
//   service->Simulate(dt)                // clears Visible, spawns & ages particles
//   service->BuildGeometry(view, ...)    // draws whatever is alive
//
// SetVisible(false) stops emission but alive particles continue to age and
// render until they expire — matching SetDead() behaviour (§3.1).
// ============================================================================

#include "particle_geometry.h"
#include "plane_emitter.h"
#include "types.h"

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex::particle {

using ModelId = uint32_t;

struct EmitterKey {
    ModelId model;
    int     id;

    bool operator==(const EmitterKey& o) const { return model == o.model && id == o.id; }
};

struct EmitterKeyHash {
    size_t operator()(const EmitterKey& k) const noexcept {
        return (static_cast<uint64_t>(k.model) * 0x9E3779B97F4A7C15ull) ^ static_cast<uint32_t>(k.id);
    }
};

// Per-emitter draw data for BuildGeometry consumers.
struct EmitterDrawList {
    ModelId                 model;
    int                     emitterId;
    int                     vertexCount;
    int                     priorityPlane;
    ParticleMaterialDesc    material;
};

class ParticleService {
public:
    ParticleService();
    ~ParticleService();

    // ---- Registry ------------------------------------------------------

    void AddPlaneEmitter(ModelId model, int emitterId,
                         std::unique_ptr<PlaneEmitter> emitter);
    void RemoveModel(ModelId model);
    void Clear();

    // Fetch emitter for mutation (per-frame state updates from animation).
    // Returns nullptr if not registered. Thread-safe.
    PlaneEmitter* GetEmitter(ModelId model, int emitterId);

    int EmitterCount() const;
    int TotalParticleCount() const;

    // True if this model has at least one emitter registered with the service.
    // Used by RenderService to skip the legacy render path for models that
    // the service already owns.
    bool HasEmittersForModel(ModelId model) const;

    // ---- Per-frame ----------------------------------------------------

    void Simulate(float dt);

    // Fills `outVertices` and `outDrawLists` with geometry for every visible
    // emitter. outDrawLists[i] describes outVertices[startOfSlice..endOfSlice].
    // Call under the same lock as Simulate if multiple threads touch state.
    void BuildGeometry(const Matrix44f& worldToView,
                       std::vector<Vertex>& outVertices,
                       std::vector<EmitterDrawList>& outDrawLists) const;

    // ---- Global hooks --------------------------------------------------

    void SetGlobalScaler(float s);
    float GlobalScaler() const;

    void SetFogEnabled(bool on) { fogEnabled_ = on; }
    bool FogEnabled() const     { return fogEnabled_; }

    // Default sampler returns opaque white → CombineColors is a no-op.
    // Passing nullptr restores the default.
    void SetFogSampler(FogSampler sampler);

private:
    mutable std::mutex mutex_;
    std::unordered_map<EmitterKey, std::unique_ptr<PlaneEmitter>, EmitterKeyHash> emitters_;

    bool       fogEnabled_ = false;
    FogSampler fogSampler_;
};

} // namespace WhiteoutDex::particle
