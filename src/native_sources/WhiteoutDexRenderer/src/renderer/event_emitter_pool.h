#pragma once
// ============================================================================
// EventEmitterPool — per-actor MDX EventObject dispatcher.
//
// Owns one entry per EventObject parsed from the actor's source model.
// Each Tick walks `eventTrackTimes` for keys crossed since the previous
// sample (mirrors preview.exe CKeyFrameTrackBase::JustPastKeyForward
// @0x1409668b0) and dispatches one event per crossing to the
// appropriate service (SplatService for SPL/UBR/FPT, SpnSpawner for SPN,
// ISoundEmitter for SND). Multi-key tracks (e.g. an FPT footprint with
// one key per wheel hit) emit every key per loop, not just the first.
//
// The pool itself is stateless w.r.t. graphics — it's just the
// crossing detector and the dispatch fan-out. All the heavy lifting
// (loading textures, allocating actors, playing audio) lives in the
// per-prefix services.
// ============================================================================

#include "model_types.h"   // EventObjectConfig
#include "types.h"         // Matrix44f, Vector3f

#include <cstdint>
#include <vector>

namespace WhiteoutDex {

class ISoundEmitter;
class SpnSpawner;

namespace particle { class SplatService; }

struct Actor;

class EventEmitterPool {
public:
    // Rebuild the pool for a freshly-loaded actor. Called from
    // RenderService::stageModelFromTemplate after the actor's
    // ModelTemplate has been bound. Clears any prior state.
    void Reset(std::vector<EventObjectConfig> configs,
               std::vector<uint32_t>          globalSequences);

    // Per-frame dispatch. Called from ApplyFrameState once the actor's
    // bone palette has been written, so worldXform lookups land on the
    // current pose.
    //
    // - actor:           the actor that owns this pool (carries handle + worldTransform).
    // - boneWorldMatrices: the same vector the renderer just produced.
    // - activeSeqIdx:    the actor's currently active sequence index.
    //                    A change between ticks re-primes the per-key
    //                    crossing state so the new sequence's frame range
    //                    can't backfire keys that were already crossed in
    //                    the previous one. Pass -1 for global-sequence-only
    //                    actors.
    // - localTimeMs:     the active sequence's current time within its window.
    // - globalTimeMs:    the renderer's free-running global clock.
    // - seqStartMs / seqEndMs: the active sequence's window, used to
    //   bound the rising-edge scan for non-global tracks.
    void Tick(const Actor&                    actor,
              const std::vector<Matrix44f>&   boneWorldMatrices,
              int                             activeSeqIdx,
              int                             localTimeMs,
              int                             globalTimeMs,
              int                             seqStartMs,
              int                             seqEndMs,
              particle::SplatService*         splats,
              SpnSpawner*                     spn,
              ISoundEmitter*                  sounds);

    bool Empty() const { return entries_.empty(); }

private:
    // Per-entry per-key crossing state. Persists across ticks but
    // resets on sequence change (detected in Tick via activeSeqIdx vs
    // prevSeqIdx_).
    struct Entry {
        EventObjectConfig cfg;
        int  lastFrame        = -1;   // -1 sentinel → first tick primes without firing
        bool resolutionFailed = false; // SLK row missing → silently skip subsequent fires
    };
    std::vector<Entry>    entries_;
    std::vector<uint32_t> globalSequences_;
    int                   prevSeqIdx_ = -1;  // -1 sentinel → first Tick primes
};

} // namespace WhiteoutDex
