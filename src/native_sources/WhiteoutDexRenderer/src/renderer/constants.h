#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Shared Constants
// ============================================================================

#include <whiteout/vector_types.h>

namespace WhiteoutDex {

using whiteout::Vector4f;

// --- Simulation ---
constexpr float kMaxSimulationDt         = 0.5f;   // engine clamps frame dt to [0, 0.5]
constexpr float kMinVisibilityThreshold  = 0.01f;  // below this, emitter is considered invisible
constexpr float kRibbonMinLifespan       = 0.25f;  // minimum ribbon edge lifespan in seconds

// --- Billboard math ---
constexpr float kVectorEpsilon           = 1e-6f;  // minimum vector length for normalization
constexpr float kBillboardDistThreshold  = 0.001f;  // minimum dist for billboard orientation

// --- Lighting: geoset pass ---
constexpr Vector4f kGeosetLightColor    = {0.50f, 0.50f, 0.48f, 1.0f};
constexpr Vector4f kGeosetAmbientColor  = {0.60f, 0.60f, 0.65f, 0.0f};

// --- Lighting: particle & ribbon pass ---
constexpr Vector4f kParticleLightColor   = {0.85f, 0.85f, 0.80f, 1.0f};
constexpr Vector4f kParticleAmbientBase  = {0.35f, 0.35f, 0.40f, 0.0f};

// --- Lighting: collision wireframe pass ---
constexpr Vector4f kCollisionLightColor  = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr Vector4f kCollisionAmbientColor = {1.0f, 1.0f, 1.0f, 0.0f};

// --- Lighting: ViewCube ---
constexpr Vector4f kViewCubeLightColor   = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr Vector4f kViewCubeAmbientColor = {0.5f, 0.5f, 0.5f, 1.0f};

// --- Shared light direction (normalized at use site) ---
constexpr Vector4f kDefaultLightDir      = {0.0f, -0.3f, -0.8f, 0.0f};
constexpr Vector4f kViewCubeLightDir      = {0.5f, 0.3f, -0.8f, 0.0f};

// --- Texture wrap flags ---
constexpr uint32_t kWrapFlagsMask = 0x3;

// --- ViewCube layout ---
constexpr float kViewCubeHomeOffset = 0.35f; // horizontal offset as fraction of cube size

} // namespace WhiteoutDex
