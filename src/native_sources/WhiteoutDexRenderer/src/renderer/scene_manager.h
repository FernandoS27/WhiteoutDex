#pragma once
// ============================================================================
// WhiteoutDex Renderer — SceneManager
//
// Owns the scene-level state cluster: actors, focus, camera, camera-preset
// list + UI inbox, sequence-picker UI inbox, and the global animation clock.
//
// Phase 5 v1: SceneManager is owned by RenderService as `scene_`. RenderService
// methods that previously touched these fields directly forward through the
// scene; the public API (`SetCamera`, `GetCameraPosition`, `SetActiveSequence`,
// …) stays unchanged so test_main / dllmain / RenderWindow keep working.
//
// Phase 5 v2 (deferred):
//  - Promote SceneManager to a sibling of RenderService (host owns both).
//  - Render passes consume `RenderableView` POD instead of Actor pointers (F6).
//  - SceneManager pumps `templates.Tick()` → moves up to host (F7 final form).
//
// Lighting / terrain / water / fog land here too in future phases.
// ============================================================================

#include "actor_manager.h"
#include "camera.h"
#include "model_source.h"
#include "model_template_manager.h"
#include "model_types.h"
#include "../io/file_content_provider.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace WhiteoutDex {

class SceneManager {
public:
    SceneManager()
        : templates_(std::make_unique<ModelTemplateManager>()) {
        activeContentProvider_ = &contentProvider_;
        templates_->SetContentProvider(activeContentProvider_);
    }
    ~SceneManager() = default;
    SceneManager(const SceneManager&)            = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // ---- Actors ----
    ActorManager&       Actors()       { return actors_; }
    const ActorManager& Actors() const { return actors_; }
    ActorId             AllocActorId() { return nextActorId_++; }
    // PE1System::Simulate wants a uint32_t& so it can allocate ids inline;
    // expose the counter for that one site (and for legacy `++` patterns).
    ActorId&            NextActorIdRef() { return nextActorId_; }

    // ---- Focus actor ----
    ActorId Focus() const             { return focusActor_; }
    void    SetFocus(ActorId id)      { focusActor_ = id; }
    Actor*  FocusActor() const        { return actors_.Find(focusActor_); }
    // Migration ergonomic: read+write reference. Existing call sites mix
    // both (assignment on Clear/Remove, comparison on Set). Phase 5 v2 will
    // tighten this into proper Focus() / SetFocus() pairs.
    ActorId&       FocusRef()         { return focusActor_; }
    const ActorId& FocusRef() const   { return focusActor_; }

    // ---- Camera ----
    ::WhiteoutDex::Camera&       Camera()       { return camera_; }
    const ::WhiteoutDex::Camera& Camera() const { return camera_; }

    // ---- Camera presets ----
    void SetCameraPresets(std::vector<CameraPreset> presets) {
        cameraPresets_        = presets;
        pendingCameraPresets_ = std::move(presets);
        cameraDirty_          = true;
        activeCameraPresetIdx_ = -1;
    }
    const std::vector<CameraPreset>& CameraPresets() const { return cameraPresets_; }
    int   ActiveCameraPresetIdx() const { return activeCameraPresetIdx_; }
    void  SetActiveCameraPresetIdx(int idx) { activeCameraPresetIdx_ = idx; }
    bool  CameraLocked() const          { return cameraLocked_; }
    void  SetCameraLocked(bool locked)  { cameraLocked_ = locked; }
    std::optional<std::vector<CameraPreset>> TakePendingCameraPresets() {
        if (!cameraDirty_) return std::nullopt;
        cameraDirty_ = false;
        return std::move(pendingCameraPresets_);
    }

    // ---- Sequence picker (UI inbox + selected index) ----
    void SetSequences(std::vector<std::string> names) {
        pendingSequenceNames_ = std::move(names);
        sequencesDirty_ = true;
    }
    void SetSequenceRanges(std::vector<SequenceInfo> ranges) {
        sequenceRanges_ = std::move(ranges);
    }
    const std::vector<SequenceInfo>& SequenceRanges() const { return sequenceRanges_; }
    int  ActiveSequenceIndex() const   { return activeSequence_.load(); }
    void SetActiveSequenceIndex(int i) { activeSequence_ = i; }
    std::optional<std::vector<std::string>> TakePendingSequences() {
        if (!sequencesDirty_) return std::nullopt;
        sequencesDirty_ = false;
        auto out = std::move(pendingSequenceNames_);
        pendingSequenceNames_.clear();
        return out;
    }

    // ---- Animation clock (ms; driven by host) ----
    void SetAnimationTime(int ms)      { animationTimeMs_ = ms; }
    int  GetAnimationTime() const      { return animationTimeMs_.load(); }
    std::atomic<int>& AnimationTimeAtomic() { return animationTimeMs_; }

    // ---- Asset resolution ----
    FileContentProvider&       GetContentProvider()       { return contentProvider_; }
    const FileContentProvider& GetContentProvider() const { return contentProvider_; }
    void SetContentProvider(std::shared_ptr<IContentProvider> provider) {
        externalContentProvider_ = std::move(provider);
        activeContentProvider_   = externalContentProvider_
            ? externalContentProvider_.get()
            : static_cast<IContentProvider*>(&contentProvider_);
        templates_->SetContentProvider(activeContentProvider_);
    }
    IContentProvider* ActiveContentProvider() const { return activeContentProvider_; }

    void SetPE1BasePath(const std::filesystem::path& basePath) {
        pe1BasePath_ = basePath;
        contentProvider_.SetBasePath(basePath);
        templates_->SetBasePath(basePath);
    }
    const std::filesystem::path& PE1BasePath() const { return pe1BasePath_; }

    // ---- Model template manager ----
    ModelTemplateManager&       Templates()       { return *templates_; }
    const ModelTemplateManager& Templates() const { return *templates_; }

    // ---- PE1 spawn limits ----
    static constexpr int kMaxPE1Depth     = 3;
    static constexpr int kMaxPE1Instances = 256;
    int  PE1InstanceCount() const           { return pe1InstanceCount_; }
    int& PE1InstanceCountRef()              { return pe1InstanceCount_; }
    void SetPE1InstanceCount(int n)         { pe1InstanceCount_ = n; }

private:
    ActorManager actors_;
    ActorId      nextActorId_ = 1;
    ActorId      focusActor_  = 0;

    ::WhiteoutDex::Camera        camera_;
    std::vector<CameraPreset>    cameraPresets_;
    std::vector<CameraPreset>    pendingCameraPresets_;
    bool                         cameraDirty_           = false;
    bool                         cameraLocked_          = false;
    int                          activeCameraPresetIdx_ = -1;

    std::vector<std::string>     pendingSequenceNames_;
    bool                         sequencesDirty_ = false;
    std::atomic<int>             activeSequence_{0};
    std::vector<SequenceInfo>    sequenceRanges_;

    std::atomic<int>             animationTimeMs_{0};

    // Asset resolution.
    FileContentProvider                contentProvider_;
    std::shared_ptr<IContentProvider>  externalContentProvider_;
    IContentProvider*                  activeContentProvider_ = nullptr;
    std::filesystem::path              pe1BasePath_;

    // Template cache — owns parsed MDX cache + GPU geometry pools.
    std::unique_ptr<ModelTemplateManager> templates_;

    // PE1 spawn-limit counter.
    int pe1InstanceCount_ = 0;
};

} // namespace WhiteoutDex
