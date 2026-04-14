#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Shared Constants
// ============================================================================

#include <DirectXMath.h>

namespace WhiteoutDex {

// --- Simulation ---
constexpr float kMaxSimulationDt         = 0.5f;   // engine clamps frame dt to [0, 0.5]
constexpr float kMinVisibilityThreshold  = 0.01f;  // below this, emitter is considered invisible
constexpr float kRibbonMinLifespan       = 0.25f;  // minimum ribbon edge lifespan in seconds

// --- Billboard math ---
constexpr float kVectorEpsilon           = 1e-6f;  // minimum vector length for normalization
constexpr float kBillboardDistThreshold  = 0.001f;  // minimum dist for billboard orientation

// --- Lighting: geoset pass ---
constexpr DirectX::XMFLOAT4 kGeosetLightColor    = {0.50f, 0.50f, 0.48f, 1.0f};
constexpr DirectX::XMFLOAT4 kGeosetAmbientColor  = {0.60f, 0.60f, 0.65f, 0.0f};

// --- Lighting: particle & ribbon pass ---
constexpr DirectX::XMFLOAT4 kParticleLightColor   = {0.85f, 0.85f, 0.80f, 1.0f};
constexpr DirectX::XMFLOAT4 kParticleAmbientBase  = {0.35f, 0.35f, 0.40f, 0.0f};

// --- Lighting: collision wireframe pass ---
constexpr DirectX::XMFLOAT4 kCollisionLightColor  = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr DirectX::XMFLOAT4 kCollisionAmbientColor = {1.0f, 1.0f, 1.0f, 0.0f};

// --- Lighting: ViewCube ---
constexpr DirectX::XMFLOAT4 kViewCubeLightColor   = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr DirectX::XMFLOAT4 kViewCubeAmbientColor = {0.5f, 0.5f, 0.5f, 1.0f};

// --- Shared light direction (normalized at use site) ---
constexpr DirectX::XMFLOAT4 kDefaultLightDir      = {0.0f, -0.3f, -0.8f, 0.0f};
constexpr DirectX::XMFLOAT4 kViewCubeLightDir      = {0.5f, 0.3f, -0.8f, 0.0f};

// --- Texture wrap flags ---
constexpr uint32_t kWrapFlagsMask = 0x3;

// --- ViewCube layout ---
constexpr float kViewCubeHomeOffset = 0.35f; // horizontal offset as fraction of cube size

} // namespace WhiteoutDex
