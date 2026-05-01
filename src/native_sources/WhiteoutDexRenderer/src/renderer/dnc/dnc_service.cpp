// ============================================================================
// DncService — implementation
// ============================================================================

#include "dnc_service.h"

#include <algorithm>
#include <cmath>

namespace WhiteoutDex::dnc {

namespace {
// Wrap `tod` into [0, hoursPerDay).
float WrapTod(float tod, float hoursPerDay) {
    if (hoursPerDay <= 0.0f) return 0.0f;
    float w = std::fmod(tod, hoursPerDay);
    if (w < 0.0f) w += hoursPerDay;
    return w;
}
} // namespace

DncService::DncService(IContentProvider* contentProvider)
    : contentProvider_(contentProvider),
      cache_(std::make_unique<DncCache>(contentProvider)),
      unitPath_(kDefaultUnitMdl) {
    // Try the default Unit MDL up-front. Failure is non-fatal; the
    // service stays usable as a "paused at noon" stub and renders
    // fall back to the legacy hardcoded baseline.
    unitAsset_ = cache_->Acquire(unitPath_);
}

DncService::~DncService() {
    if (unitAsset_) {
        cache_->Release(unitAsset_);
        unitAsset_ = nullptr;
    }
}

void DncService::SetUnitMdl(const std::string& path) {
    if (path == unitPath_) return;

    // Drop the previous binding before acquiring the new one — a
    // toggle between two paths shouldn't temporarily double the
    // refcount on the new path.
    if (unitAsset_) {
        cache_->Release(unitAsset_);
        unitAsset_ = nullptr;
    }
    unitPath_ = path;
    if (!path.empty()) {
        unitAsset_ = cache_->Acquire(path);
    }
}

bool DncService::HasAsset() const {
    return unitAsset_ != nullptr && unitAsset_->HasLight();
}

void DncService::SetTimeOfDay(float hours) {
    tod_.store(WrapTod(hours, hoursPerDay_), std::memory_order_relaxed);
}

void DncService::Advance(float dtSec) {
    if (suspended_ || todScale_ <= 0.0f || dtSec <= 0.0f) return;
    if (secondsPerDay_ <= 0.0f) return;
    // hours-per-real-second = hoursPerDay / secondsPerDay; scale lets
    // the user fast-forward.
    const float deltaHours = dtSec * todScale_ * hoursPerDay_ / secondsPerDay_;
    const float current = tod_.load(std::memory_order_relaxed);
    tod_.store(WrapTod(current + deltaHours, hoursPerDay_), std::memory_order_relaxed);
}

DncSample DncService::SampleNow() const {
    if (!unitAsset_) return DncSample{};
    return Sample(*unitAsset_, tod_.load(std::memory_order_relaxed), hoursPerDay_);
}

DncSample DncService::SampleAt(float todHours) const {
    if (!unitAsset_) return DncSample{};
    return Sample(*unitAsset_, todHours, hoursPerDay_);
}

DncService::EnvMapBlend DncService::ComputeEnvMapBlend() const {
    // Verbatim port of preview.exe EnvironmentMapTimeOfDay @
    // 0x7ff609b116f0. Window is `dayLength * 0.1` in the engine; we
    // take the equivalent value in the hours domain (hoursPerDay × 0.1)
    // because the inputs we have are already in hours.
    const float currentTod = tod_.load(std::memory_order_relaxed);
    const float window = hoursPerDay_ * 0.1f;
    EnvMapBlend out;

    if (currentTod < dawnHours_) {
        const float t = std::clamp((dawnHours_ - currentTod) / window, 0.0f, 1.0f);
        out.transitionT = 1.0f - t;
        out.isDaytime   = false;
    } else if (currentTod < duskHours_) {
        const float t = std::clamp((duskHours_ - currentTod) / window, 0.0f, 1.0f);
        out.transitionT = 1.0f - t;
        out.isDaytime   = true;
    } else {
        out.transitionT = 0.0f;
        out.isDaytime   = false;
    }
    return out;
}

} // namespace WhiteoutDex::dnc
