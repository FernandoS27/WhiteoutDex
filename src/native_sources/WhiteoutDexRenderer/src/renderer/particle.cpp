// ============================================================================
// WhiteoutDex Real-Time Renderer — Particle System Implementation
// ============================================================================

#include "particle.h"

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
    if (dt <= 0) dt = 1.0f / 60.0f;
    if (dt > 0.5f) dt = 0.5f;  // engine clamps to [0, 0.5]

    for (auto& [id, em] : emitters_) {
        // Remove dead particles
        auto& parts = em.particles;
        parts.erase(std::remove_if(parts.begin(), parts.end(),
            [](const Particle& p) { return p.lifeSpan <= 0; }), parts.end());

        // Squirt/burst mode: emit all at once on first tick, then stop
        if (em.config.squirt && !em.squirtDone) {
            int numToEmit = (int)em.state.emissionRate;
            for (int i = 0; i < numToEmit; ++i)
                SpawnParticle(em, dt);
            em.squirtDone = true;
        }
        // Normal emission
        else if (!em.config.squirt && em.state.visibility > 0.01f && em.state.emissionRate > 0) {
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

int ParticleSystem::BuildBillboards(float cameraPitch, float cameraYaw,
                                     std::vector<Vertex>& outVerts,
                                     std::vector<int>& outEmitterIds) const
{
    outVerts.clear();
    outEmitterIds.clear();

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
        if (em.state.visibility < 0.01f) continue;
        if (em.particles.empty()) continue;

        bool isHead = (em.config.particleType == 1 || em.config.particleType == 3);
        bool isTail = (em.config.particleType == 2 || em.config.particleType == 3);
        if (!isHead && !isTail) continue;

        int startIdx = (int)outVerts.size();

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

            if (alpha < 0.5f) continue;

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
                if (em.config.xyQuad) {
                    // XY-aligned quads: axis-aligned in local space, not camera-facing
                    // RE uses p.m_position (local coords) directly with +/-halfScale on X/Y
                    XMFLOAT3 localP = p.position;
                    corners[0] = {localP.x - halfScale, localP.y - halfScale, localP.z};
                    corners[1] = {localP.x + halfScale, localP.y - halfScale, localP.z};
                    corners[2] = {localP.x + halfScale, localP.y + halfScale, localP.z};
                    corners[3] = {localP.x - halfScale, localP.y + halfScale, localP.z};
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

                outVerts.push_back({corners[0], normal, vertColor, {u0, v1}});
                outVerts.push_back({corners[1], normal, vertColor, {u1, v1}});
                outVerts.push_back({corners[2], normal, vertColor, {u1, v0}});

                outVerts.push_back({corners[0], normal, vertColor, {u0, v1}});
                outVerts.push_back({corners[2], normal, vertColor, {u1, v0}});
                outVerts.push_back({corners[3], normal, vertColor, {u0, v0}});
            }

            // --- TAIL QUAD ---
            if (isTail) {
                float u0, v0, u1, v1;
                ComputeUV(em.config, lifeFactor, false, u0, v0, u1, v1);

                XMFLOAT3 worldVel = p.velocity;
                if (em.config.modelSpace) {
                    XMVECTOR wVel = XMVector3TransformNormal(XMLoadFloat3(&p.velocity), em.state.transform);
                    XMStoreFloat3(&worldVel, wVel);
                }
                float vx = worldVel.x, vy = worldVel.y, vz = worldVel.z;
                float vLen = sqrtf(vx*vx + vy*vy + vz*vz);
                XMFLOAT3 tailDir;
                if (vLen > 1e-6f) {
                    float inv = 1.0f / vLen;
                    tailDir = {vx * inv, vy * inv, vz * inv};
                } else {
                    tailDir = {0, 0, 1};
                }

                XMFLOAT3 tailEnd = {
                    worldPos.x - tailDir.x * em.config.tailLength,
                    worldPos.y - tailDir.y * em.config.tailLength,
                    worldPos.z - tailDir.z * em.config.tailLength
                };

                // Compute perpendicular for tail width (cross camera up with tail direction)
                XMFLOAT3 tailRight;
                float cx = camUp.y * tailDir.z - camUp.z * tailDir.y;
                float cy = camUp.z * tailDir.x - camUp.x * tailDir.z;
                float cz = camUp.x * tailDir.y - camUp.y * tailDir.x;
                float cLen = sqrtf(cx*cx + cy*cy + cz*cz);
                if (cLen > 1e-6f) {
                    float inv = 1.0f / cLen;
                    tailRight = {cx * inv, cy * inv, cz * inv};
                } else {
                    tailRight = camRight;
                }

                XMFLOAT3 corners[4];
                corners[0] = {worldPos.x - tailRight.x * halfScale,
                              worldPos.y - tailRight.y * halfScale,
                              worldPos.z - tailRight.z * halfScale};
                corners[1] = {worldPos.x + tailRight.x * halfScale,
                              worldPos.y + tailRight.y * halfScale,
                              worldPos.z + tailRight.z * halfScale};
                corners[2] = {tailEnd.x + tailRight.x * halfScale,
                              tailEnd.y + tailRight.y * halfScale,
                              tailEnd.z + tailRight.z * halfScale};
                corners[3] = {tailEnd.x - tailRight.x * halfScale,
                              tailEnd.y - tailRight.y * halfScale,
                              tailEnd.z - tailRight.z * halfScale};

                outVerts.push_back({corners[0], normal, vertColor, {u0, v0}});
                outVerts.push_back({corners[1], normal, vertColor, {u1, v0}});
                outVerts.push_back({corners[2], normal, vertColor, {u1, v1}});

                outVerts.push_back({corners[0], normal, vertColor, {u0, v0}});
                outVerts.push_back({corners[2], normal, vertColor, {u1, v1}});
                outVerts.push_back({corners[3], normal, vertColor, {u0, v1}});
            }
        }

        if ((int)outVerts.size() > startIdx) {
            outEmitterIds.push_back(id);
        }
    }

    return (int)outVerts.size();
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

bool ParticleSystem::GetEmitterVertexRange(int emitterId, int totalVerts,
                                            const std::vector<int>& emitterIds,
                                            int& outStart, int& outCount) const {
    int offset = 0;
    for (auto& [id, em] : emitters_) {
        int count = 0;
        for (auto& p : em.particles)
            if (p.lifeSpan > 0) count++;
        int quadsPerParticle = 0;
        bool isHead = (em.config.particleType == 1 || em.config.particleType == 3);
        bool isTail = (em.config.particleType == 2 || em.config.particleType == 3);
        if (isHead) quadsPerParticle++;
        if (isTail) quadsPerParticle++;
        int vertCount = count * quadsPerParticle * 6;

        if (id == emitterId) {
            outStart = offset;
            outCount = vertCount;
            return vertCount > 0;
        }
        if (em.state.visibility >= 0.01f && vertCount > 0)
            offset += vertCount;
    }
    return false;
}

// ============================================================================
// Private Helpers
// ============================================================================

void ParticleSystem::SpawnParticle(ParticleEmitter& em, float dt) {
    Particle p;
    auto& cfg = em.config;
    auto& st = em.state;

    constexpr float kPi = 3.14159265f;
    constexpr float kDegToRad = kPi / 180.0f;

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
    float rotLat = st.coneAngle * kDegToRad * RandF(-1.0f, 1.0f);
    float sinLat = sinf(rotLat), cosLat = cosf(rotLat);

    XMFLOAT3 localDir;
    if (cfg.lineEmitter) {
        // Line emitter: longitude = 0, no Y component
        localDir = {spd * sinLat, 0.0f, spd * cosLat};
    } else {
        float rotLon = st.longitude * kDegToRad * RandF(-1.0f, 1.0f);
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

    // Sub-frame age randomization: distribute births within the time step
    // RE sets p.age = elapsed * random[0,1), pre-advancing position/velocity
    float subAge = dt * RandF(0.0f, 1.0f);
    if (subAge > 0) {
        float az = -(em.state.gravity);
        p.position.x += p.velocity.x * subAge;
        p.position.y += p.velocity.y * subAge;
        p.position.z += p.velocity.z * subAge + 0.5f * az * subAge * subAge;
        p.velocity.z += az * subAge;
        p.lifeSpan   -= subAge;
    }

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

    int lifeStart, lifeEnd, lifeRepeat, decayStart, decayEnd, decayRepeat;
    if (isHead) {
        lifeStart = cfg.headLifeStart; lifeEnd = cfg.headLifeEnd; lifeRepeat = cfg.headLifeRepeat;
        decayStart = cfg.headDecayStart; decayEnd = cfg.headDecayEnd; decayRepeat = cfg.headDecayRepeat;
    } else {
        lifeStart = cfg.tailLifeStart; lifeEnd = cfg.tailLifeEnd; lifeRepeat = cfg.tailLifeRepeat;
        decayStart = cfg.tailDecayStart; decayEnd = cfg.tailDecayEnd; decayRepeat = cfg.tailDecayRepeat;
    }

    int nLifeFrames = (lifeEnd - lifeStart + 1) * lifeRepeat;
    int nDecayFrames = (decayEnd - decayStart + 1) * decayRepeat;
    int totalFrames = nLifeFrames + nDecayFrames;
    if (totalFrames <= 0) totalFrames = 1;

    int currentFrame = (int)(lifeFactor * totalFrames);
    if (currentFrame >= totalFrames) currentFrame = totalFrames - 1;
    int index;

    if (currentFrame < nLifeFrames) {
        int interval = lifeEnd - lifeStart + 1;
        if (interval <= 0) interval = 1;
        index = lifeStart + (currentFrame % interval);
    } else {
        int interval = decayEnd - decayStart + 1;
        if (interval <= 0) interval = 1;
        index = decayStart + ((currentFrame - nLifeFrames) % interval);
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
