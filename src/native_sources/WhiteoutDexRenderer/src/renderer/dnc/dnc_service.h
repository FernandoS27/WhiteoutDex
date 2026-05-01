#pragma once
// ============================================================================
// DncService — renderer-facing facade for the Day/Night-Cycle pipeline.
//
// Owns:
//   * DncCache — the parsed-MDL store
//   * One acquired DncAsset for the active "Unit" DNC MDL
//     (preview.exe model viewer also tracks Terrain/Portrait variants;
//     not exposed here because the model viewer has no terrain or
//     portrait pass)
//   * The current TOD value + clock-advance state
//
// Threading: the service is shared between the host UI thread (slider
// updates) and the render thread (Baseline() queries every frame). The
// TOD value is std::atomic<float>; everything else (path, knobs that
// rarely change) is touched only from the host thread, so a plain
// member suffices. Callers that want a coherent "everything at once"
// snapshot can call Snapshot().
//
// Mirrors PopcornService's shape: lazily construct, plumb a content
// provider, expose getters/setters on plain bools/floats. No locks
// inside the hot Sample path.
// ============================================================================

#include "dnc_asset.h"
#include "dnc_cache.h"
#include "io/content_provider.h"

#include <atomic>
#include <memory>
#include <string>

namespace WhiteoutDex::dnc {

class DncService {
public:
    explicit DncService(IContentProvider* contentProvider);
    ~DncService();

    DncService(const DncService&) = delete;
    DncService& operator=(const DncService&) = delete;

    // ---- Default DNC paths (preview.exe Lordaeron triplet) ----
    static constexpr const char* kDefaultUnitMdl =
        "Environment/DNC/DNCLordaeron/DNCLordaeronUnit/DNCLordaeronUnit.mdl";
    static constexpr const char* kDefaultTerrainMdl =
        "Environment/DNC/DNCLordaeron/DNCLordaeronTerrain/DNCLordaeronTerrain.mdl";
    static constexpr const char* kDefaultPortraitMdl =
        "Environment/DNC/DNCLordaeron/DNCLordaeronPortrait/DNCLordaeronPortrait.mdl";

    // ---- DNC asset binding ----

    // Replace the active Unit DNC MDL. Pass empty path to unbind (the
    // service then falls back to the legacy hardcoded baseline).
    // Mirrors JASS native SetDayNightModels(_, units).
    void SetUnitMdl(const std::string& path);
    const std::string& UnitMdlPath() const { return unitPath_; }

    // True iff an asset is currently loaded and has a usable light
    // node. Cheap; safe to call before SampleNow.
    bool HasAsset() const;

    // ---- TOD control ----

    // Current TOD in virtual hours. Engine domain is [0, hoursPerDay).
    // Set from the UI thread; read from the render thread via
    // SampleNow / GetTimeOfDay.
    void  SetTimeOfDay(float hours);
    float GetTimeOfDay() const  { return tod_.load(std::memory_order_relaxed); }

    void  SetHoursPerDay(float h)         { hoursPerDay_   = (h > 0.0f) ? h : 24.0f; }
    float GetHoursPerDay() const          { return hoursPerDay_; }

    void  SetDayLengthSeconds(float s)    { secondsPerDay_ = (s > 0.0f) ? s : 480.0f; }
    float GetDayLengthSeconds() const     { return secondsPerDay_; }

    void  SetDawnHours(float h)           { dawnHours_ = h; }
    float GetDawnHours() const            { return dawnHours_; }

    void  SetDuskHours(float h)           { duskHours_ = h; }
    float GetDuskHours() const            { return duskHours_; }

    // 0 = paused (default — model-preview tool is interactive, not a
    // wall-clock simulation), 1 = real-time, >1 = fast-forward.
    void  SetTodScale(float s)            { todScale_ = s; }
    float GetTodScale() const             { return todScale_; }

    // Suspends Advance() — mirrors JASS native SuspendTimeOfDay.
    void  Suspend(bool s)                 { suspended_ = s; }
    bool  IsSuspended() const             { return suspended_; }

    // Per-tick clock advance. Host calls once per frame with dt in
    // wall-clock seconds. Walking the cycle once takes
    // `secondsPerDay_ / todScale_` real seconds. No-op when suspended
    // or todScale==0.
    void Advance(float dtSec);

    // ---- Sampling ----

    // Sample at the current TOD. Returns valid=false if no asset is
    // loaded. Cheap (just track interpolation); safe from the render
    // thread.
    DncSample SampleNow() const;

    // Sample at an explicit TOD without touching service state. Useful
    // for tooling (TOD-indicator UI, scrubbing previews).
    DncSample SampleAt(float todHours) const;

    // ---- Day/Night IBL transition (Phase 4 hook; provided now so
    // the service is a single source of truth for the TOD math) ----

    struct EnvMapBlend {
        bool  isDaytime   = true;
        float transitionT = 0.0f;
    };
    EnvMapBlend ComputeEnvMapBlend() const;

private:
    IContentProvider*           contentProvider_ = nullptr;
    std::unique_ptr<DncCache>   cache_;
    DncAsset*                   unitAsset_       = nullptr;   // borrowed from cache_
    std::string                 unitPath_;

    std::atomic<float>          tod_             { 12.0f };   // noon default
    float                       hoursPerDay_     = 24.0f;
    float                       secondsPerDay_   = 480.0f;    // engine ships ≈ 480s/cycle
    float                       dawnHours_       = 6.0f;
    float                       duskHours_       = 18.0f;
    float                       todScale_        = 0.0f;      // paused by default
    bool                        suspended_       = false;
};

} // namespace WhiteoutDex::dnc
