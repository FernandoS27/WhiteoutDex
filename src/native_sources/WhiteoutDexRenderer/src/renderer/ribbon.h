#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Ribbon System (BlizRibbon)
// Phase 5b: Trail/ribbon effects that follow bone movement
//
// Ribbons track the emitter position over time and connect positions
// with textured quad strips. Each segment has a lifespan and fades out.
// ============================================================================

#include "types.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace WhiteoutDex {

// ============================================================================
// Single ribbon segment (recorded position in time)
// ============================================================================
struct RibbonSegment {
    XMFLOAT3 position = {0,0,0};  // world position when created
    XMFLOAT3 up       = {0,0,1};  // local up direction at creation
    float    above     = 20.0f;    // height above at creation
    float    below     = 20.0f;    // height below at creation
    float    age       = 0;        // time since creation
    float    initLife  = 0;        // original lifespan
};

// ============================================================================
// Static ribbon emitter configuration (sent once)
// ============================================================================
struct RibbonEmitterConfig {
    int   textureId  = -1;
    int   filterMode = 0;    // Wc3Material convention: 0=None,...,3=Additive, etc.
    int   rows = 1, cols = 1;
    bool  unshaded   = false;
    bool  twoSided   = true;
    float emission   = 10.0f;   // segments per second
    float life       = 1.0f;    // segment lifespan
    float gravity    = 0.0f;
};

// ============================================================================
// Per-frame ribbon emitter state (animatable values + transform)
// ============================================================================
struct RibbonEmitterState {
    XMMATRIX transform = XMMatrixIdentity();
    float above      = 20.0f;
    float below      = 20.0f;
    float alpha      = 1.0f;     // 0-1
    XMFLOAT3 color   = {1,1,1};  // vertex color RGB (0-1)
    float visibility = 1.0f;
    int   slot       = 0;        // texture slot for atlas
};

// ============================================================================
// Ribbon emitter instance
// ============================================================================
struct RibbonEmitter {
    RibbonEmitterConfig         config;
    RibbonEmitterState          state;
    std::vector<RibbonSegment>  segments;
    float                       accumEmission = 0;
    float                       startTime     = 0;  // fractional edge time accumulator
    bool                        posSet        = false;
    // Previous/current frame transform history (for Hermite interpolation)
    XMFLOAT3                    prevPos       = {0,0,0};
    XMFLOAT3                    currPos       = {0,0,0};
    XMFLOAT3                    prevDir       = {0,0,1};
    XMFLOAT3                    currDir       = {0,0,1};
    XMFLOAT3                    prevVertical  = {0,1,0};
    XMFLOAT3                    currVertical  = {0,1,0};
};

// ============================================================================
// Ribbon System — manages all ribbon emitters
// ============================================================================
class RibbonSystem {
public:
    void Clear();
    void AddEmitter(int id, const RibbonEmitterConfig& cfg);
    void UpdateEmitterState(int id, const RibbonEmitterState& st);

    bool HasEmitters() const;

    void Simulate(float dt);

    int BuildStrips(std::vector<Vertex>& outVerts,
                    std::vector<int>& outEmitterIds) const;

    const RibbonEmitterConfig* GetConfig(int id) const;
    int GetTotalSegmentCount() const;
    int GetEmitterVertCount(int emitterId) const;

private:
    void SpawnInterpolatedSegment(RibbonEmitter& em, float t, float omt,
                                   const XMFLOAT3& prevDirScaled,
                                   const XMFLOAT3& currDirScaled,
                                   float age);

    void SpawnSegmentAtCurrent(RibbonEmitter& em);

    std::unordered_map<int, RibbonEmitter> emitters_;
};

} // namespace WhiteoutDex
