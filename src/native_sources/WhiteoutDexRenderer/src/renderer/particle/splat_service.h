#pragma once
// ============================================================================
// SplatService — world-space frozen-quad decals for MDX EventObjects.
//
// Drives SPL / FPT / UBR firings: each emission is a single decal whose
// 4 corners are fixed in world space at spawn time (no camera billboarding,
// no velocity reorientation). Color and UV-cell animation interpolate over
// the decal's lifetime.
//
// SPL/FPT use 2 color stops (Start→Mid over Lifespan, Mid→End over Decay).
// UBR uses 3 stops (Birth, Pause, Decay phases). Both formats compress
// into the same Splat struct with `total = sum of phases`.
//
// Textures are loaded on demand via the content provider and cached
// globally — multiple splats / multiple actors sharing the same .blp
// dedupe through the texture key.
//
// The render path piggy-backs on RenderParticlesBls: SplatService produces
// vertices in the same Vertex layout as PE2 emitters and a parallel draw
// list that carries a pre-resolved gfx::TextureHandle. The single global
// VB used for PE2 is shared.
// ============================================================================

#include "../../io/event_data.h"   // SplEntry / UbrEntry
#include "../types.h"               // Vertex, Vector3f, Matrix44f
#include "../model_types.h"         // FilterMode
#include "../../gfx/gfx.h"          // gfx::TextureHandle, gfx::IGFXDevice

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex {

class IContentProvider;
class TextureAssetManager;

namespace particle {

// One decal currently alive in the world. Sized so the hot path (Tick)
// fits in cache-line strides.
struct Splat {
    // Frozen on emission. Quad corners in world space, traversal order:
    // 0=lower-left, 1=lower-right, 2=upper-right, 3=upper-left
    // (matches the typical decal triangle pair 0-1-2 / 0-2-3).
    Vector3f corners[4];

    gfx::TextureHandle texture = gfx::TextureHandle::Invalid;
    int                blendMode = 0;          // SLK BlendMode (0..6)
    bool               isUbr     = false;

    // SPL: phase 0 = Lifespan (start→mid), phase 1 = Decay (mid→end).
    // UBR: phase 0 = BirthTime (start→mid), phase 1 = PauseTime
    //      (mid hold), phase 2 = Decay (mid→end).
    // Phase durations in seconds; zero means "skip that phase".
    float t0 = 0.f, t1 = 0.f, t2 = 0.f;
    float total = 0.f;

    // 3 color stops in linear space (start, mid, end). Each entry is RGBA.
    float c[3][4] = {{1,1,1,1},{1,1,1,1},{1,1,1,1}};

    // UV cell animation (SPL only; UBR sets columns=rows=1 implicitly).
    int columns        = 1;
    int rows           = 1;
    int uvLifeStart    = 0, uvLifeEnd    = 0, lifespanRepeat = 1;
    int uvDecayStart   = 0, uvDecayEnd   = 0, decayRepeat    = 1;

    // Lifecycle.
    float age = 0.f;
};

// One per alive splat — produced by BuildGeometry, consumed by the
// renderer's draw loop.
struct SplatDrawList {
    int                vertexOffset = 0;
    int                vertexCount  = 0;
    gfx::TextureHandle texture      = gfx::TextureHandle::Invalid;
    int                blendMode    = 0;
};

class SplatService {
public:
    SplatService();
    ~SplatService();

    // Wire the texture-load dependencies. Called once from RenderService
    // after gfx_/textures_/content provider are all alive. Splats spawned
    // before this fires get a magenta fallback texture.
    void Configure(gfx::IGFXDevice*       gfx,
                   TextureAssetManager*   textures,
                   const IContentProvider* contentProvider);

    // Per-frame age + cull. Aging runs on a real-time clock (internal
    // QueryPerformanceCounter), NOT the parent animation dt — splats
    // are ground decals and should keep fading even when the model's
    // animation is paused or being scrubbed.
    void Tick();

    // Drop everything (called from RenderService::ShutdownDevice).
    void Clear();

    // Spawn a SPL/FPT decal. `worldRotXY[0]/[1]` are the decal's right
    // and forward vectors at spawn time, scaled by `entry.scale`. The
    // quad sits on the local XY plane. `worldOrigin` is the firing
    // node's world position.
    void SpawnSpl(const io::SplEntry& entry,
                  const Vector3f& worldOrigin,
                  const Vector3f& worldRight,
                  const Vector3f& worldForward);

    // Spawn a UBR decal. cols=rows=1 implicit (no UV cell animation).
    void SpawnUbr(const io::UbrEntry& entry,
                  const Vector3f& worldOrigin,
                  const Vector3f& worldRight,
                  const Vector3f& worldForward);

    // Append vertices for every alive splat into `out` and emit one
    // SplatDrawList per splat (in the same order). Each splat produces
    // 6 vertices — two CCW triangles (0-1-2, 0-2-3).
    void BuildGeometry(std::vector<Vertex>&        outVertices,
                       std::vector<SplatDrawList>& outDrawLists) const;

    int  Count() const;

private:
    // Resolve and cache a CASC-relative texture path. Returns Invalid
    // until Configure() has been called. Decoded via DecodeToRGBA8.
    gfx::TextureHandle GetOrLoadTexture(const std::string& path);

    // Construct the four world corners from spawn frame.
    static void BuildCorners(Vector3f corners[4],
                             const Vector3f& origin,
                             const Vector3f& right,
                             const Vector3f& forward);

    // Sample the active color/UV stops for the current `age`. Writes
    // RGBA into outColor and the UV-cell index into outCellIdx
    // (-1 → no cell animation; caller uses the full texture).
    static void EvaluateAt(const Splat& s,
                           float        outColor[4],
                           int&         outCellIdx);

    // Map our internal cell index to UV offsets within the cols×rows sheet.
    static void CellToUV(int cellIdx, int columns, int rows,
                         float& u0, float& v0, float& u1, float& v1);

    mutable std::mutex      mutex_;
    std::vector<Splat>      splats_;

    gfx::IGFXDevice*        gfx_       = nullptr;
    TextureAssetManager*    textures_  = nullptr;
    const IContentProvider* content_   = nullptr;

    std::unordered_map<std::string, gfx::TextureHandle> textureCache_;

    // Real-time aging clock — opaque LARGE_INTEGER counter snapshot
    // taken on the previous Tick. -1 sentinel until the first call,
    // which primes without aging.
    int64_t lastTickQpc_ = -1;
};

} // namespace particle
} // namespace WhiteoutDex
