#pragma once
// ============================================================================
// WhiteoutDex Renderer — AnimationDriver
//
// Per-actor animation state machine (Phase 4 v1: single sequence + cursor +
// optional birth-time offset for global sequences). Holds a shared_ptr to a
// polymorphic IAnimationSource (F3) — for template-backed actors that's a
// TemplateAnimationSource (Phase 5); for live sources (MaxSceneAdapter) the
// adapter implements IAnimationSource directly.
//
// `Evaluate()` forwards to the source — no branch on source type, no
// per-call mutation. The mutation footprint (`currentSequenceIdx_`,
// `timeMs_`, `birthTimeMs_`) lives here, not on the source, so multiple
// actors can share one source without stepping on each other.
//
// State-machine v2 (transitions, blending, layer masks) lands in a follow-up
// once we have a real character driving the requirements.
// ============================================================================

#include "model_source.h"   // IAnimationSource, FrameState, SequenceInfo
#include "types.h"           // Matrix44f, Vector3f

#include <memory>
#include <string_view>
#include <vector>

namespace WhiteoutDex {

class AnimationDriver {
public:
    // Bind to a source. Pass nullptr to clear (the actor stops evaluating).
    void Bind(std::shared_ptr<IAnimationSource> source) { source_ = std::move(source); }
    bool HasSource() const { return static_cast<bool>(source_); }
    const std::shared_ptr<IAnimationSource>& Source() const { return source_; }

    // Sequence selection. v1 supports a single active sequence; v2 will add
    // transitions + blends. `Play(name)` looks up by name; on miss the index
    // stays unchanged (caller-side validation hook).
    void Play(int sequenceIdx, int startTimeMs = 0) {
        currentSequenceIdx_ = sequenceIdx;
        timeMs_             = startTimeMs;
    }
    void Play(std::string_view sequenceName);

    int  ActiveSequenceIndex() const          { return currentSequenceIdx_; }
    void SetActiveSequenceIndex(int idx)      { currentSequenceIdx_ = idx; }

    int  TimeMs() const                       { return timeMs_; }
    void SetTimeMs(int ms)                    { timeMs_ = ms; }

    int  BirthTimeMs() const                  { return birthTimeMs_; }
    void SetBirthTimeMs(int ms)               { birthTimeMs_ = ms; }

    // Sequence list — convenience forward to source_->GetSequences().
    // Empty if no source is bound.
    std::vector<SequenceInfo> Sequences() const;

    // Evaluate to a FrameState for the given world transform + camera.
    // Returns an empty FrameState if no source is bound.
    FrameState Evaluate(const Matrix44f& worldTransform,
                        const Vector3f&  cameraPos,
                        int              globalTimeMs) const;

private:
    std::shared_ptr<IAnimationSource> source_;
    int currentSequenceIdx_ = 0;
    int timeMs_             = 0;
    int birthTimeMs_        = 0;
    // Future: blendFromIdx_, blendT_, layerMasks_, ...
};

} // namespace WhiteoutDex
