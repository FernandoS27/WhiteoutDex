// ============================================================================
// SplatService — implementation.
// ============================================================================

#include "splat_service.h"

#include "../../io/content_provider.h"
#include "../../gfx/gfx.h"
#include "../model_source_utils.h"   // DispatchTextureParser, ExtensionLower
#include "../texture_asset_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>   // QueryPerformanceCounter / Frequency for real-time aging

namespace WhiteoutDex::particle {

SplatService::SplatService()  = default;
SplatService::~SplatService() = default;

void SplatService::Configure(gfx::IGFXDevice*       gfx,
                             TextureAssetManager*   textures,
                             const IContentProvider* contentProvider) {
    std::lock_guard<std::mutex> lk(mutex_);
    gfx_      = gfx;
    textures_ = textures;
    content_  = contentProvider;
}

void SplatService::Tick() {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);

    std::lock_guard<std::mutex> lk(mutex_);

    // First call: prime the clock and skip aging this frame so we don't
    // burn an arbitrary amount of time-since-process-start onto every
    // splat that happens to be alive on the first Tick.
    if (lastTickQpc_ < 0) {
        lastTickQpc_ = now.QuadPart;
        return;
    }

    const double dtSec = (double)(now.QuadPart - lastTickQpc_) / (double)freq.QuadPart;
    lastTickQpc_ = now.QuadPart;

    // Clamp obscenely large gaps (e.g. process suspended in debugger)
    // so we don't kill every alive splat in one tick.
    const float dt = (float)std::min(dtSec, 0.5);
    if (dt <= 0.f) return;

    auto it = splats_.begin();
    while (it != splats_.end()) {
        it->age += dt;
        if (it->age >= it->total) {
            it = splats_.erase(it);
        } else {
            ++it;
        }
    }
}

void SplatService::Clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    splats_.clear();
    // Free the cached splat textures we created via gfx_->CreateTexture.
    // Dropping the handles without Destroy leaks the GPU-side resources
    // — the gfx device's slot-map keeps them alive until shutdown,
    // which on a long-running session gradually accumulates whatever
    // splat textures we touched. Iterate before clearing so the
    // unordered_map doesn't drop them mid-iteration.
    if (gfx_) {
        for (auto& [path, tex] : textureCache_) {
            if (tex != gfx::TextureHandle::Invalid) gfx_->Destroy(tex);
        }
    }
    textureCache_.clear();
    lastTickQpc_ = -1;   // re-prime on next Tick
}

int SplatService::Count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return (int)splats_.size();
}

void SplatService::BuildCorners(Vector3f corners[4],
                                const Vector3f& origin,
                                const Vector3f& right,
                                const Vector3f& forward) {
    // Corner order with paired UVs (BuildGeometry below):
    //   p0 = origin + (+right, +forward)   — UV (0,0)  "top-left"
    //   p1 = origin + (-right, +forward)   — UV (0,1)  "bottom-left"
    //   p2 = origin + (-right, -forward)   — UV (1,1)  "bottom-right"
    //   p3 = origin + (+right, -forward)   — UV (1,0)  "top-right"
    // Triangles emitted as 0-1-2 / 0-2-3.
    corners[0] = { origin.x + right.x + forward.x,
                   origin.y + right.y + forward.y,
                   origin.z + right.z + forward.z };
    corners[1] = { origin.x - right.x + forward.x,
                   origin.y - right.y + forward.y,
                   origin.z - right.z + forward.z };
    corners[2] = { origin.x - right.x - forward.x,
                   origin.y - right.y - forward.y,
                   origin.z - right.z - forward.z };
    corners[3] = { origin.x + right.x - forward.x,
                   origin.y + right.y - forward.y,
                   origin.z + right.z - forward.z };
}

void SplatService::SpawnSpl(const io::SplEntry& entry,
                            const Vector3f& worldOrigin,
                            const Vector3f& worldRight,
                            const Vector3f& worldForward) {
    Splat s;
    BuildCorners(s.corners, worldOrigin, worldRight, worldForward);
    s.texture   = GetOrLoadTexture(entry.file);
    s.blendMode = entry.blendMode;
    s.isUbr     = false;
    s.t0 = entry.lifespan;
    s.t1 = entry.decay;
    s.t2 = 0.f;
    s.total = s.t0 + s.t1;
    // Degenerate row (no lifespan, no decay) → skip-spawn entirely.
    // The previous safety `total = 1.f` floor caused these rows to
    // render the END colour for one wall-clock second, polluting the
    // ground with stale decals on models authored with placeholder
    // splat ids.
    if (s.total <= 0.f) return;
    std::memcpy(s.c[0], entry.startC, sizeof(float) * 4);
    std::memcpy(s.c[1], entry.midC,   sizeof(float) * 4);
    std::memcpy(s.c[2], entry.endC,   sizeof(float) * 4);
    s.columns        = std::max(1, entry.columns);
    s.rows           = std::max(1, entry.rows);
    s.uvLifeStart    = entry.uvLifeStart;
    s.uvLifeEnd      = entry.uvLifeEnd;
    s.lifespanRepeat = std::max(1, entry.lifespanRepeat);
    s.uvDecayStart   = entry.uvDecayStart;
    s.uvDecayEnd     = entry.uvDecayEnd;
    s.decayRepeat    = std::max(1, entry.decayRepeat);
    s.age            = 0.f;

    std::lock_guard<std::mutex> lk(mutex_);
    splats_.push_back(s);
}

void SplatService::SpawnUbr(const io::UbrEntry& entry,
                            const Vector3f& worldOrigin,
                            const Vector3f& worldRight,
                            const Vector3f& worldForward) {
    Splat s;
    BuildCorners(s.corners, worldOrigin, worldRight, worldForward);
    s.texture   = GetOrLoadTexture(entry.file);
    s.blendMode = entry.blendMode;
    s.isUbr     = true;
    s.t0 = entry.birthTime;
    s.t1 = entry.pauseTime;
    s.t2 = entry.decay;
    s.total = s.t0 + s.t1 + s.t2;
    // Same degenerate-row guard as SpawnSpl — skip rather than render
    // an END-colour decal for an arbitrary 1 second.
    if (s.total <= 0.f) return;
    std::memcpy(s.c[0], entry.c[0], sizeof(float) * 4);
    std::memcpy(s.c[1], entry.c[1], sizeof(float) * 4);
    std::memcpy(s.c[2], entry.c[2], sizeof(float) * 4);
    // No UV cell animation for UBR — single full-texture sample.
    s.columns = s.rows = 1;
    s.uvLifeStart = s.uvLifeEnd = 0;
    s.uvDecayStart = s.uvDecayEnd = 0;
    s.lifespanRepeat = s.decayRepeat = 1;
    s.age = 0.f;

    std::lock_guard<std::mutex> lk(mutex_);
    splats_.push_back(s);
}

namespace {
inline float ClampF(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

void Lerp4(float out[4], const float a[4], const float b[4], float t) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
    out[3] = a[3] + (b[3] - a[3]) * t;
}
} // namespace

void SplatService::EvaluateAt(const Splat& s,
                              float        outColor[4],
                              int&         outCellIdx) {
    outCellIdx = -1;
    const float age = ClampF(s.age, 0.f, s.total);

    if (s.isUbr) {
        // 3 phases: Birth (start→mid), Pause (mid hold), Decay (mid→end).
        if (age < s.t0 && s.t0 > 0.f) {
            Lerp4(outColor, s.c[0], s.c[1], age / s.t0);
        } else if (age < s.t0 + s.t1) {
            std::memcpy(outColor, s.c[1], sizeof(float) * 4);
        } else if (s.t2 > 0.f) {
            Lerp4(outColor, s.c[1], s.c[2],
                  (age - s.t0 - s.t1) / s.t2);
        } else {
            std::memcpy(outColor, s.c[2], sizeof(float) * 4);
        }
        return;
    }

    // SPL/FPT: 2 phases: Lifespan (start→mid) + Decay (mid→end).
    // UV cell is sampled per-phase via the SLK's start/end/repeat.
    auto cellOf = [](int start, int end, int repeat, float t) {
        // Matches PE2 ParticleKey::Interpolate's cell-step formula
        // (Previewd's authentic UV-cell sweep). The t-nudge avoids
        // overshooting the last frame at t==1.
        const float nudge = t * 0.99f + 0.005f;
        const float r     = (repeat < 1) ? 1.0f : (float)repeat;
        const int   delta = (end >= start) ? (end - start + 1) : (end - start - 1);
        const float effT  = (r == 1.0f) ? nudge : std::fmod(nudge * r, 1.0f);
        const float val   = (float)start + (float)delta * effT;
        return (int)val;
    };

    if (age < s.t0 && s.t0 > 0.f) {
        const float t = age / s.t0;
        Lerp4(outColor, s.c[0], s.c[1], t);
        outCellIdx = cellOf(s.uvLifeStart, s.uvLifeEnd, s.lifespanRepeat, t);
    } else {
        const float t = (s.t1 > 0.f) ? ((age - s.t0) / s.t1) : 1.f;
        Lerp4(outColor, s.c[1], s.c[2], t);
        outCellIdx = cellOf(s.uvDecayStart, s.uvDecayEnd, s.decayRepeat, t);
    }
}

void SplatService::CellToUV(int cellIdx, int columns, int rows,
                            float& u0, float& v0, float& u1, float& v1) {
    if (cellIdx < 0 || (columns <= 1 && rows <= 1)) {
        u0 = 0.f; v0 = 0.f; u1 = 1.f; v1 = 1.f;
        return;
    }
    columns = std::max(1, columns);
    rows    = std::max(1, rows);
    const int cells = columns * rows;
    const int idx   = ((cellIdx % cells) + cells) % cells;  // wrap negative
    const int cx    = idx % columns;
    const int cy    = idx / columns;
    const float du = 1.0f / (float)columns;
    const float dv = 1.0f / (float)rows;
    u0 = cx * du;       v0 = cy * dv;
    u1 = u0 + du;       v1 = v0 + dv;
}

void SplatService::BuildGeometry(std::vector<Vertex>&        outVertices,
                                 std::vector<SplatDrawList>& outDrawLists) const {
    std::lock_guard<std::mutex> lk(mutex_);
    outVertices.reserve(outVertices.size() + splats_.size() * 6);
    outDrawLists.reserve(outDrawLists.size() + splats_.size());

    for (const auto& s : splats_) {
        float color[4]; int cellIdx = -1;
        EvaluateAt(s, color, cellIdx);

        float u0, v0, u1, v1;
        CellToUV(cellIdx, s.columns, s.rows, u0, v0, u1, v1);

        const Vector3f n{0.f, 0.f, 1.f};      // ground-normal hint; PS ignores
        const Vector4f c{color[0], color[1], color[2], color[3]};

        // UVs paired with corners (see BuildCorners above):
        //   p0 → (u0,v0)  p1 → (u0,v1)  p2 → (u1,v1)  p3 → (u1,v0)
        // Index buffer 0-1-2 / 0-2-3.
        const int base = (int)outVertices.size();
        outVertices.push_back({ s.corners[0], n, c, {u0, v0} });
        outVertices.push_back({ s.corners[1], n, c, {u0, v1} });
        outVertices.push_back({ s.corners[2], n, c, {u1, v1} });
        outVertices.push_back({ s.corners[0], n, c, {u0, v0} });
        outVertices.push_back({ s.corners[2], n, c, {u1, v1} });
        outVertices.push_back({ s.corners[3], n, c, {u1, v0} });

        SplatDrawList dl;
        dl.vertexOffset = base;
        dl.vertexCount  = 6;
        dl.texture      = s.texture;
        dl.blendMode    = s.blendMode;
        outDrawLists.push_back(dl);
    }
}

gfx::TextureHandle SplatService::GetOrLoadTexture(const std::string& path) {
    if (path.empty() || !gfx_ || !content_) return gfx::TextureHandle::Invalid;

    // Cache lookup is the fast path — no decode, just a handle.
    {
        auto it = textureCache_.find(path);
        if (it != textureCache_.end()) return it->second;
    }

    std::string foundExt;
    auto data = content_->ReadFile(path, &foundExt);
    if (!data) {
        // Cache the miss so we don't re-poll the provider every frame.
        textureCache_.emplace(path, gfx::TextureHandle::Invalid);
        std::fprintf(stderr,
                     "[splat] ERR: tex read FAIL '%s'\n",
                     path.c_str());
        return gfx::TextureHandle::Invalid;
    }
    if (foundExt.empty()) foundExt = ExtensionLower(std::filesystem::path(path));

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!DecodeToRGBA8(*data, foundExt, rgba, w, h) || w <= 0 || h <= 0) {
        textureCache_.emplace(path, gfx::TextureHandle::Invalid);
        std::fprintf(stderr,
                     "[splat] ERR: tex decode FAIL '%s' ext='%s' bytes=%zu\n",
                     path.c_str(), foundExt.c_str(), data->size());
        return gfx::TextureHandle::Invalid;
    }

    gfx::TextureDesc td;
    td.width     = w;
    td.height    = h;
    td.mipLevels = 1;
    td.format    = gfx::Format::R8G8B8A8_UNORM;
    td.usage     = gfx::TextureUsage::ShaderResource;
    auto tex = gfx_->CreateTexture(td, rgba.data());
    textureCache_.emplace(path, tex);
    return tex;
}

} // namespace WhiteoutDex::particle
