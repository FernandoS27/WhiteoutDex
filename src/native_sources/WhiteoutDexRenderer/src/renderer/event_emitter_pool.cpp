// ============================================================================
// EventEmitterPool — implementation.
// ============================================================================

#include "event_emitter_pool.h"

#include "model_instance.h"
#include "particle/splat_service.h"
#include "sound_emitter.h"
#include "spn_spawner.h"

#include "../io/event_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace WhiteoutDex {

void EventEmitterPool::Reset(std::vector<EventObjectConfig> configs,
                             std::vector<uint32_t>          globalSequences) {
    entries_.clear();
    entries_.reserve(configs.size());
    for (auto& c : configs) {
        Entry e;
        e.cfg = std::move(c);
        entries_.push_back(std::move(e));
    }
    globalSequences_ = std::move(globalSequences);
    prevSeqIdx_      = -1;
}

namespace {

// Walk the track times from newest to oldest looking for a crossing
// inside [windowLo, windowHi]: value=1 between the most recent crossing
// and the next, 0 outside. We stop at the first match — the algorithm
// only cares about *whether* a crossing exists at-or-before `frame`.
int ValueAtFrame(const std::vector<uint32_t>& times,
                 int frame, int windowLo, int windowHi) {
    if (times.empty()) return 0;
    if (frame < windowLo || frame > windowHi) return 0;
    for (int i = (int)times.size() - 1; i >= 0; --i) {
        const int t = (int)times[i];
        if (t < windowLo)  return 0;     // walked past the window
        if (t <= frame)    return 1;     // most recent crossing reached
    }
    return 0;
}

// Lift the hierarchy's "delta from bind" matrix into an absolute world
// transform. Mirrors `worldOf` in mdx_model_adapter.cpp: T(pivot) *
// boneMatrix puts the node's origin at its animated pivot world
// position, and a final * actor.worldTransform folds in the actor's
// scene placement (identity for top-level actors, non-identity for
// PE1/SPN children whose world placement comes from their parent).
Matrix44f BuildNodeWorld(const Matrix44f& bone,
                         const Vector3f&  pivot,
                         const Matrix44f& actorWorld) {
    Matrix44f pivotT = Matrix44f::translation({pivot.x, pivot.y, pivot.z});
    return pivotT * bone * actorWorld;
}

// Build the spawn frame: world position from the absolute matrix, plus
// right/forward axes scaled by `scale`. Splats sit on the local XY
// plane, which after the renderer's coord-system swizzle is the ground
// plane in both Max and Blz default conventions.
//
// Matrix44f is row-major: data[3] is the translation row, data[0] /
// data[1] are the local X / Y basis vectors. Splat `scale` from the
// SLK is the corner offset (half-width); full quad width = 2*scale.
//
// Important: the basis rows of `m` carry whatever scale the parent
// bone has been animated with — for a bone with scale=2 in a death
// throes anim, m.data[0] would have length 2, doubling the splat size.
// We want the rotated axis *direction* at unit length and the SLK
// scale applied on top, so each basis row gets normalised here before
// it's multiplied by `scale`.
void ExtractSpawnFrame(const Matrix44f& m,
                       float            scale,
                       Vector3f&        outOrigin,
                       Vector3f&        outRight,
                       Vector3f&        outForward) {
    auto normRow = [&](int r, Vector3f& out) {
        const float x = m.data[r][0], y = m.data[r][1], z = m.data[r][2];
        const float len = std::sqrt(x*x + y*y + z*z);
        const float inv = (len > 1e-6f) ? (scale / len) : 0.0f;
        out = Vector3f{ x * inv, y * inv, z * inv };
    };
    outOrigin = Vector3f{ m.data[3][0], m.data[3][1], m.data[3][2] };
    normRow(0, outRight);
    normRow(1, outForward);
}

Vector3f ExtractWorldPos(const Matrix44f& m) {
    return Vector3f{ m.data[3][0], m.data[3][1], m.data[3][2] };
}

// Splats are ground decals — they live on the terrain, not on the
// animated bone they happened to be parented to. The MDX EventObject
// gives us the FIRE TIME and the X/Y location, but its animated Z
// follows whatever the parent bone is doing (a Footman's spine lifts
// during death, a tank's hull sinks). The game obviously projects
// splats onto terrain Z; with no terrain in the preview we project
// onto the actor's own ground plane (Z = actorWorld translation Z),
// which is 0 for top-level actors and the spawn-time ground for
// PE1/SPN children. Right/forward are also flattened onto the XY
// plane and re-orthonormalised so the splat stays a square parallel
// to the ground regardless of what the parent bone's tilt is doing.
void ProjectToGroundPlane(Vector3f& origin,
                          Vector3f& right,
                          Vector3f& forward,
                          const Matrix44f& actorWorld) {
    const float groundZ = actorWorld.data[3][2];
    origin.z = groundZ;

    // Flatten right onto XY, keep its length (the SLK scale).
    const float rLen2D = std::sqrt(right.x * right.x + right.y * right.y);
    const float rLen   = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rLen2D > 1e-6f) {
        const float k = rLen / rLen2D;
        right.x *= k; right.y *= k;
    }
    right.z = 0.f;

    // Forward is right rotated 90° around +Z, preserving its original
    // magnitude. Avoids any tilt that the parent bone's local Y axis
    // might have introduced; matches the corner construction in
    // SplatService::BuildCorners which expects right ⊥ forward in XY.
    const float fLen = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    const float rOnly = std::sqrt(right.x * right.x + right.y * right.y);
    if (rOnly > 1e-6f) {
        const float invR = fLen / rOnly;
        forward.x = -right.y * invR;
        forward.y =  right.x * invR;
    } else {
        forward.x = 0.f; forward.y = fLen;
    }
    forward.z = 0.f;
}

} // namespace

void EventEmitterPool::Tick(const Actor&                  actor,
                            const std::vector<Matrix44f>& boneWorldMatrices,
                            int                            activeSeqIdx,
                            int                            localTimeMs,
                            int                            globalTimeMs,
                            int                            seqStartMs,
                            int                            seqEndMs,
                            particle::SplatService*        splats,
                            SpnSpawner*                    spn,
                            ISoundEmitter*                 sounds) {
    if (entries_.empty()) return;

    // Sequence change → re-prime so the new window's tracks don't
    // inherit a stale lastValue=1 from the previous sequence (which
    // would swallow a rising edge at the new sequence's first track).
    // SceneManager::Update updates Actor::prevActiveSequence before
    // ApplyFrameState runs, so we can't detect the change off the
    // actor; we keep our own prev cursor instead.
    if (activeSeqIdx != prevSeqIdx_) {
        for (auto& e : entries_) { e.lastValue = 0; e.lastFrame = -1; }
        prevSeqIdx_ = activeSeqIdx;
    }

    for (auto& e : entries_) {
        const auto& cfg = e.cfg;
        if (cfg.kind == EventObjectConfig::Kind::Unknown) continue;
        if (cfg.eventTrackTimes.empty())                  continue;
        if (e.resolutionFailed)                           continue;

        // Pick the time window — global sequence has its own duration
        // and runs on the renderer's free-running clock; otherwise the
        // current sequence's interval drives.
        int frame    = 0;
        int windowLo = 0;
        int windowHi = 0;
        if (cfg.globalSequenceId != 0xFFFFFFFFu &&
            cfg.globalSequenceId < globalSequences_.size()) {
            const uint32_t dur = globalSequences_[cfg.globalSequenceId];
            if (dur == 0) continue;
            frame    = (int)((uint32_t)globalTimeMs % dur);
            windowLo = 0;
            windowHi = (int)dur - 1;
        } else {
            frame    = localTimeMs;
            windowLo = seqStartMs;
            windowHi = seqEndMs;
        }

        // Rising-edge detection. Loop wrap (`frame < lastFrame` and
        // we've already primed) re-arms the value gate so the very next
        // crossing fires again. First tick (`lastFrame == -1`) primes
        // without firing — otherwise loading a model in the middle of a
        // sequence would dispatch every event whose track time was
        // already past.
        const int value = ValueAtFrame(cfg.eventTrackTimes, frame,
                                       windowLo, windowHi);
        bool fire = false;
        if (e.lastFrame < 0) {
            // First sample: just record state, don't fire.
        } else if (frame < e.lastFrame) {
            // Sequence wrapped — drop edge, then re-evaluate.
            e.lastValue = 0;
            fire = (value == 1);
        } else {
            fire = (value == 1 && e.lastValue == 0);
        }
        e.lastValue = value;
        e.lastFrame = frame;

        if (!fire) continue;

        // Resolve the firing world transform. The hierarchy stores a
        // "delta from bind" matrix per node, so to get the animated
        // pivot's absolute world position we need:
        //     T(pivot) * boneMatrix * actor.worldTransform
        // Out-of-range nodeIndex (EventObject without a hierarchy slot)
        // collapses onto the actor's worldTransform so the event still
        // fires somewhere sensible (the actor origin).
        Matrix44f nodeWorld;
        if (cfg.nodeIndex >= 0 && cfg.nodeIndex < (int)boneWorldMatrices.size()) {
            nodeWorld = BuildNodeWorld(boneWorldMatrices[cfg.nodeIndex],
                                       cfg.pivot,
                                       actor.worldTransform);
        } else {
            nodeWorld = actor.worldTransform;
        }

        switch (cfg.kind) {
        case EventObjectConfig::Kind::SPN: {
            if (!spn) break;
            const io::SpnEntry* row = io::FindSpn(cfg.id);
            if (!row) {
                // Don't spam — log once per id by zeroing the entry's
                // tracks so subsequent rising edges silently skip.
                std::fprintf(stderr,
                    "[WDEX events] SPN id '%s' not in SpawnData.slk\n",
                    cfg.id.c_str());
                e.resolutionFailed = true;
                break;
            }
            spn->Spawn(actor.handle, row->modelPath, nodeWorld, globalTimeMs);
            break;
        }
        case EventObjectConfig::Kind::SPL:
        case EventObjectConfig::Kind::FPT: {
            if (!splats) break;
            const io::SplEntry* row = io::FindSpl(cfg.id);
            if (!row) {
                std::fprintf(stderr,
                    "[WDEX events] SPL/FPT id '%s' not in SplatData.slk\n",
                    cfg.id.c_str());
                e.resolutionFailed = true;
                break;
            }
            Vector3f origin, right, forward;
            ExtractSpawnFrame(nodeWorld, row->scale, origin, right, forward);
            ProjectToGroundPlane(origin, right, forward, actor.worldTransform);
            splats->SpawnSpl(*row, origin, right, forward);
            break;
        }
        case EventObjectConfig::Kind::UBR: {
            if (!splats) break;
            const io::UbrEntry* row = io::FindUbr(cfg.id);
            if (!row) {
                std::fprintf(stderr,
                    "[WDEX events] UBR id '%s' not in UberSplatData.slk\n",
                    cfg.id.c_str());
                e.resolutionFailed = true;
                break;
            }
            Vector3f origin, right, forward;
            ExtractSpawnFrame(nodeWorld, row->scale, origin, right, forward);
            ProjectToGroundPlane(origin, right, forward, actor.worldTransform);
            splats->SpawnUbr(*row, origin, right, forward);
            break;
        }
        case EventObjectConfig::Kind::SND: {
            if (!sounds) break;
            const io::SndEntry* row = io::FindSnd(cfg.id);
            if (!row) {
                std::fprintf(stderr,
                    "[WDEX events] SND id '%s' not in any UI/SoundInfo/*Sounds*.slk\n",
                    cfg.id.c_str());
                e.resolutionFailed = true;
                break;
            }
            sounds->Play(*row, ExtractWorldPos(nodeWorld));
            break;
        }
        case EventObjectConfig::Kind::Unknown:
            break;
        }
    }
}

} // namespace WhiteoutDex
