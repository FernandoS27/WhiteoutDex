// ============================================================================
// SpnSpawner — implementation.
// ============================================================================

#include "spn_spawner.h"

#include "../io/mdx_model_adapter.h"
#include "render_service.h"
#include "scene_manager.h"
#include "model_template_manager.h"
#include "model_template.h"
#include "model_instance.h"
#include "actor_manager.h"
#include "replaceable_texture_manager.h"

#include <algorithm>
#include <cstdio>

namespace WhiteoutDex {

void SpnSpawner::Spawn(uint32_t parentActor,
                       const std::string& mdxPath,
                       const Matrix44f&   parentNodeWorld,
                       int                nowMs) {
    if (mdxPath.empty()) return;
    Pending p;
    p.parentActor = parentActor;
    p.mdxPath     = mdxPath;
    p.parentWorld = parentNodeWorld;
    p.birthMs     = nowMs;
    pending_.push_back(std::move(p));
}

void SpnSpawner::Tick(int nowMs) {
    // 1) Promote any pending requests whose template is now ready. The
    // ModelTemplateManager runs an async loader so the first call to
    // GetOrLoadAsync typically returns nullptr; we keep the request in
    // the queue and try again next frame. This mirrors the attachment
    // path in EvaluateAttachmentChildren.
    if (rs_.scene_ && !pending_.empty()) {
        std::vector<Pending> stillPending;
        stillPending.reserve(pending_.size());
        for (auto& p : pending_) {
            // Make sure the parent still exists; if it was unloaded
            // before the template arrived, drop the spawn silently.
            auto pit = rs_.scene_->Actors().All().find(p.parentActor);
            if (pit == rs_.scene_->Actors().All().end()) continue;

            auto tmpl = rs_.scene_->Templates().GetOrLoadAsync(p.mdxPath);
            if (!tmpl) { stillPending.push_back(std::move(p)); continue; }

            // Recursion budget — an SPN's spawned actor may itself
            // contain SPN events, so we propagate depth like PE1.
            // ApplyFrameState will skip eval beyond kMaxPE1Depth.
            const int parentDepth = pit->second->pe1Depth;

            // Compute the one-shot lifetime from sequence 0's duration.
            // Models authored without sequences (rare for SPN spawns,
            // but possible) get a 1-second floor so they at least flash.
            int durationMs = 1000;
            std::vector<SequenceInfo> seqs;
            if (tmpl->adapter) seqs = tmpl->adapter->GetSequences();
            if (!seqs.empty()) {
                const int span = seqs[0].endMs - seqs[0].startMs;
                durationMs = (span > 0) ? span : 1000;
            }

            // Mirrors the PE1 birth path (render_service.cpp ~L279).
            uint32_t childH = rs_.scene_->NextActorIdRef()++;
            auto child = std::make_unique<Actor>();
            child->handle         = childH;
            child->parent         = p.parentActor;
            child->isPE1Child     = true;   // reuses the recursive eval/teardown plumbing
            child->pe1Depth       = parentDepth + 1;
            child->worldTransform = p.parentWorld;
            child->animation.Bind(std::static_pointer_cast<IAnimationSource>(tmpl->adapter));
            child->animation.SetActiveSequenceIndex(0);
            child->animation.SetBirthTimeMs(p.birthMs);

            rs_.stageModelFromTemplate(child.get(), tmpl);
            rs_.scene_->Actors().All()[childH] = std::move(child);

            Active a;
            a.parentActor = p.parentActor;
            a.handle      = childH;
            a.expiryMs    = p.birthMs + durationMs;
            active_.push_back(a);
        }
        pending_ = std::move(stillPending);
    }

    // 2) Age out completed spawns. Same teardown the PE1 path uses.
    if (active_.empty() || !rs_.scene_) return;
    auto it = active_.begin();
    while (it != active_.end()) {
        if (nowMs < it->expiryMs) { ++it; continue; }
        auto found = rs_.scene_->Actors().All().find(it->handle);
        if (found != rs_.scene_->Actors().All().end()) {
            if (rs_.replaceables_) rs_.replaceables_->UnregisterModel(*found->second);
            if (rs_.gfx_)          found->second->ReleaseGPU(*rs_.gfx_);
            rs_.scene_->Actors().All().erase(found);
        }
        rs_.particleService_.RemoveModel(it->handle);
        it = active_.erase(it);
    }
}

void SpnSpawner::RemoveSpawnsOf(uint32_t parentActor) {
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
            [&](const Pending& p) { return p.parentActor == parentActor; }),
        pending_.end());
    if (!rs_.scene_) { active_.clear(); return; }
    auto it = active_.begin();
    while (it != active_.end()) {
        if (it->parentActor != parentActor) { ++it; continue; }
        auto found = rs_.scene_->Actors().All().find(it->handle);
        if (found != rs_.scene_->Actors().All().end()) {
            if (rs_.replaceables_) rs_.replaceables_->UnregisterModel(*found->second);
            if (rs_.gfx_)          found->second->ReleaseGPU(*rs_.gfx_);
            rs_.scene_->Actors().All().erase(found);
        }
        rs_.particleService_.RemoveModel(it->handle);
        it = active_.erase(it);
    }
}

void SpnSpawner::Clear() {
    pending_.clear();
    if (!rs_.scene_) { active_.clear(); return; }
    for (auto& a : active_) {
        auto found = rs_.scene_->Actors().All().find(a.handle);
        if (found == rs_.scene_->Actors().All().end()) continue;
        if (rs_.replaceables_) rs_.replaceables_->UnregisterModel(*found->second);
        if (rs_.gfx_)          found->second->ReleaseGPU(*rs_.gfx_);
        rs_.scene_->Actors().All().erase(found);
        rs_.particleService_.RemoveModel(a.handle);
    }
    active_.clear();
}

} // namespace WhiteoutDex
