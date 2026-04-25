// ============================================================================
// WhiteoutDex Renderer — AnimationDriver (impl)
// ============================================================================

#include "animation_driver.h"

namespace WhiteoutDex {

void AnimationDriver::Play(std::string_view sequenceName) {
    if (!source_) return;
    auto seqs = source_->GetSequences();
    for (int i = 0; i < (int)seqs.size(); ++i) {
        if (seqs[i].name == sequenceName) {
            Play(i);
            return;
        }
    }
    // No match — leave currentSequenceIdx_ unchanged.
}

std::vector<SequenceInfo> AnimationDriver::Sequences() const {
    if (!source_) return {};
    return source_->GetSequences();
}

FrameState AnimationDriver::Evaluate(const Matrix44f& worldTransform,
                                     const Vector3f&  cameraPos,
                                     int              globalTimeMs) const {
    if (!source_) return {};
    return source_->Evaluate(currentSequenceIdx_, timeMs_, globalTimeMs,
                             worldTransform, cameraPos);
}

} // namespace WhiteoutDex
