// ============================================================================
// WhiteoutDex Real-Time Renderer — Particle System Implementation
// ============================================================================

#include "particle.h"
#include "sim_util.h"

namespace WhiteoutDex {

// ============================================================================
// Public API
// ============================================================================

void ParticleSystem::Clear() {
    emitters_.clear();
}

void ParticleSystem::AddEmitter(int id, const ParticleEmitterConfig& cfg) {
    emitters_[id].config = cfg;
    emitters_[id].particles.clear();
    emitters_[id].accumEmission = 0;
    emitters_[id].squirtDone = false;
}

void ParticleSystem::UpdateEmitterState(int id, const ParticleEmitterState& st) {
    auto it = emitters_.find(id);
    if (it != emitters_.end())
        it->second.state = st;
}

bool ParticleSystem::HasEmitters() const { return !emitters_.empty(); }
int  ParticleSystem::EmitterCount() const { return (int)emitters_.size(); }

// ============================================================================
// Simulation
// ============================================================================

void ParticleSystem::Simulate(float dt) {
    dt = ClampDeltaTime(dt);

    for (auto& [id, em] : emitters_) {
        // Remove dead particles
        auto& parts = em.particles;
        parts.erase(std::remove_if(parts.begin(), parts.end(),
            [](const Particle& p) { return p.lifeSpan <= 0; }), parts.end());

        // Squirt/burst mode: one-shot burst on each rising edge of emissionRate from 0
        if (em.config.squirt) {
            // Arm when emissionRate drops to 0 so the next spike re-triggers the burst
            if (em.state.emissionRate <= 0) {
                em.squirtDone = false;
            } else if (!em.squirtDone) {
                // Burst spawns the emissionRate
                int numToEmit = (int)(em.state.emissionRate);
                for (int i = 0; i < numToEmit; ++i)
                    SpawnParticle(em, dt);
                em.squirtDone = true;
            }
        }
        // Normal emission (non-squirt emitters only)
        else if (IsEmitterVisible(em.state.visibility) && em.state.emissionRate > 0) {
            em.accumEmission += em.state.emissionRate * dt;
            int numEmitted = 0;
            while (em.accumEmission >= 1.0f) {
                SpawnParticle(em, dt);
                em.accumEmission -= 1.0f;
                ++numEmitted;
            }
        }

        // Update existing particles (proper Euler: pos += vel*dt + 0.5*a*dt²)
        float az = -(em.state.gravity);
        for (auto& p : parts) {
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
            p.position.z += p.velocity.z * dt + 0.5f * az * dt * dt;
            p.velocity.z += az * dt;
            p.lifeSpan -= dt;
        }
    }
}

// ============================================================================
// Billboard Generation
// ============================================================================

ParticleSystem::BillboardResult ParticleSystem::BuildBillboards(float cameraPitch, float cameraYaw) const
{
    BillboardResult result;

    // Camera right/up vectors (Z-up, right-handed)
    float cosP = cosf(cameraPitch), sinP = sinf(cameraPitch);
    float cosY = cosf(cameraYaw),   sinY = sinf(cameraYaw);

    // Camera direction (toward target)
    XMFLOAT3 camFwd = {-cosP * cosY, -cosP * sinY, -sinP};

    // For Z-up: right = (-sinY, cosY, 0), up depends on pitch
    XMFLOAT3 camRight = {-sinY, cosY, 0};
    // Up = cross(right, forward)
    XMFLOAT3 camUp = {
        -sinP * cosY,
        -sinP * sinY,
        cosP
    };

    for (auto& [id, em] : emitters_) {
        if (!IsEmitterVisible(em.state.visibility)) continue;
        if (em.particles.empty()) continue;

        bool isHead = (em.config.particleType == 1 || em.config.particleType == 3);
        bool isTail = (em.config.particleType == 2 || em.config.particleType == 3);
        if (!isHead && !isTail) continue;

        int startIdx = (int)result.vertices.size();

        // Collect particle data + optional camera-space Z for sorting
        struct ParticleRef {
            const Particle* p;
            float camZ;
        };
        std::vector<ParticleRef> sortedParticles;
        sortedParticles.reserve(em.particles.size());
        for (auto& p : em.particles) {
            if (p.lifeSpan <= 0) continue;
            float cz = 0;
            if (em.config.sortZ) {
                if (em.config.modelSpace) {
                    XMVECTOR wPos = XMVector3Transform(XMLoadFloat3(&p.position), em.state.transform);
                    XMFLOAT3 wp; XMStoreFloat3(&wp, wPos);
                    cz = wp.x * camFwd.x + wp.y * camFwd.y + wp.z * camFwd.z;
                } else {
                    cz = p.position.x * camFwd.x + p.position.y * camFwd.y + p.position.z * camFwd.z;
                }
            }
            sortedParticles.push_back({&p, cz});
        }

        // Sort far-to-near if sortZ enabled (descending camZ for back-to-front)
        if (em.config.sortZ) {
            std::sort(sortedParticles.begin(), sortedParticles.end(),
                [](const ParticleRef& a, const ParticleRef& b) { return a.camZ > b.camZ; });
        }

        for (auto& pr : sortedParticles) {
            auto& p = *pr.p;

            float lifeFactor = (p.initLife > 0) ? (1.0f - p.lifeSpan / p.initLife) : 0;

            // Interpolate color, alpha, scale
            XMFLOAT3 color;
            float alpha, scale;
            Interpolate3Seg(em.config, lifeFactor, color, alpha, scale);

            if (alpha <= 0.0f) continue;

            XMFLOAT4 vertColor = {color.x, color.y, color.z, alpha / 255.0f};

            // Resolve world-space position for model-space particles
            XMFLOAT3 worldPos = p.position;
            if (em.config.modelSpace) {
                XMVECTOR wPos = XMVector3Transform(XMLoadFloat3(&p.position), em.state.transform);
                XMStoreFloat3(&worldPos, wPos);
            }

            float halfScale = scale * 0.5f;
            XMFLOAT3 normal = camFwd;

            // --- HEAD QUAD ---
            if (isHead) {
                float u0, v0, u1, v1;
                ComputeUV(em.config, lifeFactor, true, u0, v0, u1, v1);

                XMFLOAT3 corners[4];
                if (em.config.xyQuad && em.config.modelSpace) {
                    // XY-aligned in emitter local space, then transformed to world
                    XMFLOAT3 local[4] = {
                        {p.position.x - halfScale, p.position.y - halfScale, p.position.z},
                        {p.position.x + halfScale, p.position.y - halfScale, p.position.z},
                        {p.position.x + halfScale, p.position.y + halfScale, p.position.z},
                        {p.position.x - halfScale, p.position.y + halfScale, p.position.z},
                    };
                    for (int c = 0; c < 4; c++) {
                        XMVECTOR wc = XMVector3Transform(XMLoadFloat3(&local[c]), em.state.transform);
                        XMStoreFloat3(&corners[c], wc);
                    }
                } else if (em.config.xyQuad) {
                    // XY-aligned quads in world space (no emitter rotation)
                    corners[0] = {worldPos.x - halfScale, worldPos.y - halfScale, worldPos.z};
                    corners[1] = {worldPos.x + halfScale, worldPos.y - halfScale, worldPos.z};
                    corners[2] = {worldPos.x + halfScale, worldPos.y + halfScale, worldPos.z};
                    corners[3] = {worldPos.x - halfScale, worldPos.y + halfScale, worldPos.z};
                } else {
                    for (int c = 0; c < 4; c++) {
                        float sx = (c == 0 || c == 3) ? -halfScale : halfScale;
                        float sy = (c == 0 || c == 1) ? -halfScale : halfScale;
                        corners[c] = {
                            worldPos.x + camRight.x * sx + camUp.x * sy,
                            worldPos.y + camRight.y * sx + camUp.y * sy,
                            worldPos.z + camRight.z * sx + camUp.z * sy
                        };
                    }
                }

                result.vertices.push_back({corners[0], normal, vertColor, {u0, v1}});
                result.vertices.push_back({corners[1], normal, vertColor, {u1, v1}});
                result.vertices.push_back({corners[2], normal, vertColor, {u1, v0}});

                result.vertices.push_back({corners[0], normal, vertColor, {u0, v1}});
                result.vertices.push_back({corners[2], normal, vertColor, {u1, v0}});
                result.vertices.push_back({corners[3], normal, vertColor, {u0, v0}});
            }

            // --- TAIL QUAD ---
            if (isTail) {
                float u0, v0, u1, v1;
                ComputeUV(em.config, lifeFactor, false, u0, v0, u1, v1);

                // Tail: velocity-proportional (faster particles = longer tail)
                XMFLOAT3 worldVel = p.velocity;
                if (em.config.modelSpace) {
                    XMVECTOR wVel = XMVector3TransformNormal(XMLoadFloat3(&p.velocity), em.state.transform);
                    XMStoreFloat3(&worldVel, wVel);
                }
                float vx = worldVel.x, vy = worldVel.y, vz = worldVel.z;
                float vLen = sqrtf(vx*vx + vy*vy + vz*vz);
                if (vLen < kVectorEpsilon) continue; // skip zero-velocity tails

                float inv = 1.0f / vLen;
                XMFLOAT3 tailDir = {vx * inv, vy * inv, vz * inv};

                // Tail endpoint: normalized direction * tailLength (fixed length)
                XMFLOAT3 tailEnd = {
                    worldPos.x - tailDir.x * em.config.tailLength,
                    worldPos.y - tailDir.y * em.config.tailLength,
                    worldPos.z - tailDir.z * em.config.tailLength
                };

                // Width direction: camera-facing perpendicular to velocity.
                // cross(velocityDir, toCamera) gives a vector perpendicular to
                // both the tail and the view direction (matches reference).
                XMFLOAT3 tailMid = {
                    (worldPos.x + tailEnd.x) * 0.5f,
                    (worldPos.y + tailEnd.y) * 0.5f,
                    (worldPos.z + tailEnd.z) * 0.5f
                };
                XMFLOAT3 camSrc = {
                    tailMid.x + cosf(cameraPitch) * cosf(cameraYaw) * 1000.0f,
                    tailMid.y + cosf(cameraPitch) * sinf(cameraYaw) * 1000.0f,
                    tailMid.z + sinf(cameraPitch) * 1000.0f
                };
                float tcx = camSrc.x - tailMid.x, tcy = camSrc.y - tailMid.y, tcz = camSrc.z - tailMid.z;
                float tcLen = sqrtf(tcx*tcx + tcy*tcy + tcz*tcz);
                if (tcLen > kVectorEpsilon) { float ti = 1.0f/tcLen; tcx *= ti; tcy *= ti; tcz *= ti; }

                // cross(tailDir, toCam)
                float rx = tailDir.y * tcz - tailDir.z * tcy;
                float ry = tailDir.z * tcx - tailDir.x * tcz;
                float rz = tailDir.x * tcy - tailDir.y * tcx;
                float rLen = sqrtf(rx*rx + ry*ry + rz*rz);
                XMFLOAT3 tailRight;
                if (rLen > kVectorEpsilon) {
                    float ri = 1.0f / rLen;
                    tailRight = {rx * ri, ry * ri, rz * ri};
                } else {
                    // Fallback: velocity parallel to camera — use world up cross
                    XMFLOAT3 altUp = {0, 0, 1};
                    if (fabsf(tailDir.z) > 0.999f) altUp = {0, 1, 0};
                    rx = tailDir.y * altUp.z - tailDir.z * altUp.y;
                    ry = tailDir.z * altUp.x - tailDir.x * altUp.z;
                    rz = tailDir.x * altUp.y - tailDir.y * altUp.x;
                    rLen = sqrtf(rx*rx + ry*ry + rz*rz);
                    float ri = (rLen > kVectorEpsilon) ? 1.0f / rLen : 1.0f;
                    tailRight = {rx * ri, ry * ri, rz * ri};
                }

                XMFLOAT3 corners[4];
                // quad: tail-left, tail+right, head+right, head-left
                corners[0] = {tailEnd.x - tailRight.x * halfScale,
                              tailEnd.y - tailRight.y * halfScale,
                              tailEnd.z - tailRight.z * halfScale};
                corners[1] = {tailEnd.x + tailRight.x * halfScale,
                              tailEnd.y + tailRight.y * halfScale,
                              tailEnd.z + tailRight.z * halfScale};
                corners[2] = {worldPos.x + tailRight.x * halfScale,
                              worldPos.y + tailRight.y * halfScale,
                              worldPos.z + tailRight.z * halfScale};
                corners[3] = {worldPos.x - tailRight.x * halfScale,
                              worldPos.y - tailRight.y * halfScale,
                              worldPos.z - tailRight.z * halfScale};

                result.vertices.push_back({corners[0], normal, vertColor, {u0, v0}});
                result.vertices.push_back({corners[1], normal, vertColor, {u1, v0}});
                result.vertices.push_back({corners[2], normal, vertColor, {u1, v1}});

                result.vertices.push_back({corners[0], normal, vertColor, {u0, v0}});
                result.vertices.push_back({corners[2], normal, vertColor, {u1, v1}});
                result.vertices.push_back({corners[3], normal, vertColor, {u0, v1}});
            }
        }

        int emitterVertCount = (int)result.vertices.size() - startIdx;
        if (emitterVertCount > 0) {
            result.emitterIds.push_back(id);
            result.vertCounts.push_back(emitterVertCount);
        }
    }

    return result;
}

// ============================================================================
// Query Methods
// ============================================================================

const ParticleEmitterConfig* ParticleSystem::GetConfig(int id) const {
    auto it = emitters_.find(id);
    return (it != emitters_.end()) ? &it->second.config : nullptr;
}

int ParticleSystem::GetTotalParticleCount() const {
    int total = 0;
    for (auto& [id, em] : emitters_) total += (int)em.particles.size();
    return total;
}

// ============================================================================
// Private Helpers
// ============================================================================

void ParticleSystem::SpawnParticle(ParticleEmitter& em, float dt) {
    Particle p;
    auto& cfg = em.config;
    auto& st = em.state;

    constexpr float kPi = XM_PI;

    // Spawn position: random within width x length rectangle in XY plane
    float hw = st.width * 0.5f;
    float hl = st.length * 0.5f;
    XMFLOAT3 localPos = {RandF(-hw, hw), RandF(-hl, hl), 0};

    if (!cfg.modelSpace) {
        XMVECTOR posV = XMVector3Transform(XMLoadFloat3(&localPos), st.transform);
        XMStoreFloat3(&p.position, posV);
    } else {
        p.position = localPos;
    }

    // Emission direction: sequential Y/Z rotation matching RE pseudocode
    // Start along +Z, rotate by latitude around Y, then by longitude around Z
    float spd = st.speed * (1.0f + RandF(-st.variation, st.variation));
    float rotLat = st.coneAngle * RandF(-1.0f, 1.0f);
    float sinLat = sinf(rotLat), cosLat = cosf(rotLat);

    // Longitude is derived from LineEmitter flag (PE2 has no longitude field):
    //   lineEmitter = true  → longitude = 0 (spread in XZ plane only)
    //   lineEmitter = false → longitude = π (full circle)
    XMFLOAT3 localDir;
    if (cfg.lineEmitter) {
        localDir = {spd * sinLat, 0.0f, spd * cosLat};
    } else {
        float rotLon = kPi * RandF(-1.0f, 1.0f);
        float sinLon = sinf(rotLon), cosLon = cosf(rotLon);
        localDir = {spd * sinLat * cosLon, spd * sinLat * sinLon, spd * cosLat};
    }

    if (!cfg.modelSpace) {
        XMVECTOR dirV = XMVector3TransformNormal(XMLoadFloat3(&localDir), st.transform);
        XMStoreFloat3(&p.velocity, dirV);
    } else {
        p.velocity = localDir;
    }

    p.lifeSpan = cfg.lifeSpan;
    p.initLife = cfg.lifeSpan;

    // Sub-frame age: RE sets p.m_age = elapsed * random[0,1) without
    // pre-integrating position/velocity. We just subtract from lifeSpan
    // so the particle appears slightly aged (matches keyframe interpolation).
    float subAge = dt * RandF(0.0f, 1.0f);
    p.lifeSpan -= subAge;

    em.particles.push_back(p);
}

void ParticleSystem::Interpolate3Seg(const ParticleEmitterConfig& cfg, float t,
                                      XMFLOAT3& outColor, float& outAlpha, float& outScale)
{
    if (t < cfg.midTime) {
        float f = (cfg.midTime > 0) ? t / cfg.midTime : 0;
        outColor = Lerp3(cfg.startColor, cfg.midColor, f);
        outAlpha = cfg.startAlpha + f * (cfg.midAlpha - cfg.startAlpha);
        outScale = cfg.startScale + f * (cfg.midScale - cfg.startScale);
    } else {
        float range = 1.0f - cfg.midTime;
        float f = (range > 0) ? (t - cfg.midTime) / range : 0;
        outColor = Lerp3(cfg.midColor, cfg.endColor, f);
        outAlpha = cfg.midAlpha + f * (cfg.endAlpha - cfg.midAlpha);
        outScale = cfg.midScale + f * (cfg.endScale - cfg.midScale);
    }
}

void ParticleSystem::ComputeUV(const ParticleEmitterConfig& cfg, float lifeFactor,
                                bool isHead, float& u0, float& v0, float& u1, float& v1)
{
    float cellW = (cfg.cols > 0) ? 1.0f / cfg.cols : 1.0f;
    float cellH = (cfg.rows > 0) ? 1.0f / cfg.rows : 1.0f;

    // The engine has two key phases: life (key 0) and decay (key 1).
    // midTime separates them: [0..midTime) = life phase, [midTime..1] = decay phase.
    // Each phase has its own cell range and repeat count.
    int lifeStart, lifeEnd, lifeRepeat, decayStart, decayEnd, decayRepeat;
    if (isHead) {
        lifeStart = cfg.headLifeStart; lifeEnd = cfg.headLifeEnd; lifeRepeat = cfg.headLifeRepeat;
        decayStart = cfg.headDecayStart; decayEnd = cfg.headDecayEnd; decayRepeat = cfg.headDecayRepeat;
    } else {
        lifeStart = cfg.tailLifeStart; lifeEnd = cfg.tailLifeEnd; lifeRepeat = cfg.tailLifeRepeat;
        decayStart = cfg.tailDecayStart; decayEnd = cfg.tailDecayEnd; decayRepeat = cfg.tailDecayRepeat;
    }

    int index;
    if (lifeFactor < cfg.midTime) {
        // Life phase: interpolate within [lifeStart..lifeEnd] repeated lifeRepeat times
        int interval = lifeEnd - lifeStart + 1;
        if (interval <= 0) interval = 1;
        int totalFrames = interval * (std::max)(1, lifeRepeat);
        float phaseT = (cfg.midTime > 0) ? lifeFactor / cfg.midTime : 0;
        int currentFrame = (int)(phaseT * totalFrames);
        if (currentFrame >= totalFrames) currentFrame = totalFrames - 1;
        index = lifeStart + (currentFrame % interval);
    } else {
        // Decay phase: interpolate within [decayStart..decayEnd] repeated decayRepeat times
        int interval = decayEnd - decayStart + 1;
        if (interval <= 0) interval = 1;
        int totalFrames = interval * (std::max)(1, decayRepeat);
        float range = 1.0f - cfg.midTime;
        float phaseT = (range > 0) ? (lifeFactor - cfg.midTime) / range : 0;
        int currentFrame = (int)(phaseT * totalFrames);
        if (currentFrame >= totalFrames) currentFrame = totalFrames - 1;
        index = decayStart + (currentFrame % interval);
    }

    int row = (cfg.cols > 0) ? index / cfg.cols : 0;
    int col = (cfg.cols > 0) ? index % cfg.cols : 0;

    u0 = cellW * col;
    v0 = cellH * row;
    u1 = cellW * (col + 1);
    v1 = cellH * (row + 1);
}

XMFLOAT3 ParticleSystem::Lerp3(const XMFLOAT3& a, const XMFLOAT3& b, float t) {
    float inv = 1.0f - t;
    return {inv*a.x + t*b.x, inv*a.y + t*b.y, inv*a.z + t*b.z};
}

float ParticleSystem::RandF(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng_);
}

} // namespace WhiteoutDex
