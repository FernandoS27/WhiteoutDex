// ============================================================================
// WhiteoutDex Renderer — SceneManager (impl)
// ============================================================================

#include "scene_manager.h"

#include "model_instance.h"
#include "model_source.h"   // SequenceInfo

#include <algorithm>        // std::min for the NonLooping clamp

namespace WhiteoutDex {

void SceneManager::Update(float dtSec) {
    const int dtMs = (dtSec > 0.0f) ? (int)(dtSec * 1000.0f + 0.5f) : 0;
    if (dtMs > 0) animationTimeMs_.fetch_add(dtMs);
    const int now = animationTimeMs_.load();

    for (auto& [h, mi] : actors_.All()) {
        // PE1 + attachment children run their own clock through BirthTimeMs;
        // EvaluatePE1Children does the loop math for them.
        if (mi->isPE1Child) continue;
        // Externally-driven actors (Max plugin) own their own time cursor
        // — the host writes SetTimeMs on TimeChanged. We don't auto-advance
        // or re-loop because that would fight the externally-set time.
        if (mi->externallyDriven) continue;
        if (!mi->animation.HasSource()) continue;

        const auto seqs = mi->animation.Sequences();
        if (seqs.empty()) continue;

        // Picker write-through (RenderService::SetActiveSequence) lands on
        // the driver from the UI thread; we observe the change here and
        // restart the local clock so the new sequence plays from frame 0.
        const int rawIdx     = mi->animation.ActiveSequenceIndex();
        const int boundedIdx = ((rawIdx % (int)seqs.size()) + (int)seqs.size()) % (int)seqs.size();
        if (rawIdx != mi->prevActiveSequence) {
            mi->sequenceStartTimeMs = now;
            mi->prevActiveSequence  = rawIdx;
        }

        const auto& seq      = seqs[boundedIdx];
        const int   duration = seq.endMs - seq.startMs;
        int elapsed = now - mi->sequenceStartTimeMs;
        if (elapsed < 0) elapsed = 0;
        // NonLooping sequences (Death / climax / Decay) clamp at the
        // last frame instead of wrapping back to startMs. The
        // per-actor `ignoreNonLooping` overrides this — useful for
        // animation editors that want to preview the clip in a loop.
        int frameMs;
        if (duration <= 0) {
            frameMs = seq.startMs;
        } else if (seq.nonLooping && !mi->ignoreNonLooping) {
            frameMs = seq.startMs + (std::min)(elapsed, duration);
        } else {
            frameMs = seq.startMs + (elapsed % duration);
        }
        mi->animation.SetTimeMs(frameMs);
    }
}

} // namespace WhiteoutDex
