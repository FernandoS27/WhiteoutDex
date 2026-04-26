// ============================================================================
// WhiteoutDex Renderer — SceneManager (impl)
// ============================================================================

#include "scene_manager.h"

#include "model_instance.h"
#include "model_source.h"   // SequenceInfo

namespace WhiteoutDex {

void SceneManager::Update(float dtSec) {
    const int dtMs = (dtSec > 0.0f) ? (int)(dtSec * 1000.0f + 0.5f) : 0;
    if (dtMs > 0) animationTimeMs_.fetch_add(dtMs);
    const int now = animationTimeMs_.load();

    for (auto& [h, mi] : actors_.All()) {
        // PE1 + attachment children run their own clock through BirthTimeMs;
        // EvaluatePE1Children does the loop math for them.
        if (mi->isPE1Child) continue;
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
        const int looped = (duration > 0)
                             ? seq.startMs + (elapsed % duration)
                             : seq.startMs;
        mi->animation.SetTimeMs(looped);
    }
}

} // namespace WhiteoutDex
