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
// Single ribbon edge: explicit above/below world-space vertices.
// Matches CRibbonEmitter::gxVertices[2*pos+0/1] storage.
// ============================================================================
struct RibbonSegment {
    Vector3f top = {0,0,0};   // above-center vertex (currPos + vertical*above)
    Vector3f bot = {0,0,0};   // below-center vertex (currPos - vertical*below)
    float    age = 0;         // seconds since creation
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
    Matrix44f transform = Matrix44f::identity();
    float above      = 20.0f;
    float below      = 20.0f;
    float alpha      = 1.0f;     // 0-1
    Vector3f color   = {1,1,1};  // vertex color RGB (0-1)
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
    Vector3f                    prevPos       = {0,0,0};
    Vector3f                    currPos       = {0,0,0};
    Vector3f                    prevDir       = {0,0,1};
    Vector3f                    currDir       = {0,0,1};
    Vector3f                    prevVertical  = {0,1,0};
    Vector3f                    currVertical  = {0,1,0};
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

    struct StripResult {
        std::vector<Vertex> vertices;
        std::vector<int> emitterIds;
    };

    StripResult BuildStrips() const;

    const RibbonEmitterConfig* GetConfig(int id) const;
    int GetTotalSegmentCount() const;
    int GetEmitterVertCount(int emitterId) const;

private:
    std::unordered_map<int, RibbonEmitter> emitters_;
};

} // namespace WhiteoutDex
