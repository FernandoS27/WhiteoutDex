// ============================================================================
// WhiteoutDex Real-Time Renderer — Ribbon System Implementation
// ============================================================================

#include "ribbon.h"

namespace WhiteoutDex {

// ============================================================================
// Public API
// ============================================================================

void RibbonSystem::Clear() { emitters_.clear(); }

void RibbonSystem::AddEmitter(int id, const RibbonEmitterConfig& cfg) {
    auto& em = emitters_[id];
    em.config = cfg;
    em.segments.clear();
    em.accumEmission = 0;
    em.startTime = 0;
    em.posSet = false;
}

void RibbonSystem::UpdateEmitterState(int id, const RibbonEmitterState& st) {
    auto it = emitters_.find(id);
    if (it == emitters_.end()) return;
    auto& em = it->second;

    em.state = st;

    // Extract position and orientation from transform matrix
    XMVECTOR posV = XMVector3Transform(XMVectorSet(0,0,0,1), st.transform);
    XMVECTOR dirV = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0,0,1,0), st.transform));
    XMVECTOR vertV = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0,1,0,0), st.transform));

    XMFLOAT3 newPos, newDir, newVert;
    XMStoreFloat3(&newPos, posV);
    XMStoreFloat3(&newDir, dirV);
    XMStoreFloat3(&newVert, vertV);

    if (em.posSet) {
        em.prevPos = em.currPos;
        em.prevDir = em.currDir;
        em.prevVertical = em.currVertical;
    } else {
        em.prevPos = newPos;
        em.prevDir = newDir;
        em.prevVertical = newVert;
        em.startTime = 0;
        em.posSet = true;
    }
    em.currPos = newPos;
    em.currDir = newDir;
    em.currVertical = newVert;
}

bool RibbonSystem::HasEmitters() const { return !emitters_.empty(); }

// ============================================================================
// Simulation
// ============================================================================

void RibbonSystem::Simulate(float dt) {
    if (dt <= 0) dt = 1.0f / 60.0f;
    if (dt > 0.5f) dt = 0.5f;

    for (auto& [id, em] : emitters_) {
        float lifeSpan = em.config.life;
        if (lifeSpan < 0.25f) lifeSpan = 0.25f;

        // If dt exceeds lifespan, all edges are dead — reset
        if (dt >= lifeSpan) {
            em.segments.clear();
            em.prevPos = em.currPos;
            em.prevDir = em.currDir;
            em.prevVertical = em.currVertical;
            em.startTime = 0;
            continue;
        }

        // Retire old segments
        em.segments.erase(
            std::remove_if(em.segments.begin(), em.segments.end(),
                [lifeSpan](const RibbonSegment& s) { return s.age >= lifeSpan; }),
            em.segments.end());

        // Apply gravity and advance age for existing segments
        for (auto& seg : em.segments) {
            // Engine formula: dz = g*dt^2 + 2*g*age*dt (parabolic sag)
            float dz = em.config.gravity * dt * dt + 2.0f * em.config.gravity * seg.age * dt;
            seg.position.z -= dz;
            seg.age += dt;
        }

        // Emit new edges with Hermite interpolation between prev and curr
        if (em.state.visibility > 0.01f && em.config.emission > 0 && em.posSet) {
            float edgesPerSec = em.config.emission;
            float endTime = em.startTime + dt * edgesPerSec;
            float newEdgeTime = 1.0f;

            if (endTime >= 1.0f) {
                int numNew = (int)floorf(endTime - newEdgeTime) + 1;
                float ooDenom = (endTime - em.startTime > 1e-6f)
                                ? 1.0f / (endTime - em.startTime) : 1.0f;

                // Compute interpolation deltas (Hermite-like)
                float dx = em.currPos.x - em.prevPos.x;
                float dy = em.currPos.y - em.prevPos.y;
                float dz = em.currPos.z - em.prevPos.z;
                float dist = sqrtf(dx*dx + dy*dy + dz*dz);

                XMFLOAT3 prevDirScaled = {em.prevDir.x * dist, em.prevDir.y * dist, em.prevDir.z * dist};
                XMFLOAT3 currDirScaled = {em.currDir.x * dist, em.currDir.y * dist, em.currDir.z * dist};

                for (int i = 0; i < numNew; ++i) {
                    float interpTime = (newEdgeTime - em.startTime) * ooDenom;
                    interpTime = (std::max)(0.0f, (std::min)(1.0f, interpTime));
                    float omt = 1.0f - interpTime;

                    SpawnInterpolatedSegment(em, interpTime, omt,
                                             prevDirScaled, currDirScaled, -(dt * interpTime));
                    newEdgeTime += 1.0f;
                }
            }

            em.startTime = endTime - floorf(endTime);

            // Place segment at current position (t=1.0)
            SpawnSegmentAtCurrent(em);
        }
    }
}

// ============================================================================
// Geometry Generation
// ============================================================================

int RibbonSystem::BuildStrips(std::vector<Vertex>& outVerts,
                               std::vector<int>& outEmitterIds) const
{
    outVerts.clear();
    outEmitterIds.clear();

    for (auto& [id, em] : emitters_) {
        if (em.state.visibility < 0.01f) continue;
        if (em.segments.size() < 2) continue;

        int startIdx = (int)outVerts.size();
        auto& segs = em.segments;
        int numSegs = (int)segs.size();

        // Compute texture slot UV rect
        float cellW = (em.config.cols > 0) ? 1.0f / em.config.cols : 1.0f;
        float cellH = (em.config.rows > 0) ? 1.0f / em.config.rows : 1.0f;
        int slotRow = (em.config.cols > 0) ? em.state.slot / em.config.cols : 0;
        int slotCol = (em.config.cols > 0) ? em.state.slot % em.config.cols : 0;
        float texL = cellW * slotCol;
        float texT = cellH * slotRow;
        float texR = cellW * (slotCol + 1);
        float texB = cellH * (slotRow + 1);
        float texDU = texR - texL;
        float lifeSpan = em.config.life;
        if (lifeSpan < 0.25f) lifeSpan = 0.25f;
        float ooLife = 1.0f / lifeSpan;

        // Uniform color — engine applies diffuseClr uniformly to all edges
        XMFLOAT4 vertColor = {em.state.color.x, em.state.color.y, em.state.color.z,
                              em.state.alpha};

        for (int i = 0; i < numSegs - 1; i++) {
            auto& s0 = segs[i];      // older
            auto& s1 = segs[i + 1];  // newer

            XMFLOAT3 top0 = {s0.position.x + s0.up.x * s0.above,
                              s0.position.y + s0.up.y * s0.above,
                              s0.position.z + s0.up.z * s0.above};
            XMFLOAT3 bot0 = {s0.position.x - s0.up.x * s0.below,
                              s0.position.y - s0.up.y * s0.below,
                              s0.position.z - s0.up.z * s0.below};
            XMFLOAT3 top1 = {s1.position.x + s1.up.x * s1.above,
                              s1.position.y + s1.up.y * s1.above,
                              s1.position.z + s1.up.z * s1.above};
            XMFLOAT3 bot1 = {s1.position.x - s1.up.x * s1.below,
                              s1.position.y - s1.up.y * s1.below,
                              s1.position.z - s1.up.z * s1.below};

            float u0 = texDU * s0.age * ooLife + texL;
            float u1 = texDU * s1.age * ooLife + texL;

            XMFLOAT3 normal = {0, 0, 1};  // simplified

            outVerts.push_back({top0, normal, vertColor, {u0, texT}});
            outVerts.push_back({bot0, normal, vertColor, {u0, texB}});
            outVerts.push_back({top1, normal, vertColor, {u1, texT}});

            outVerts.push_back({bot0, normal, vertColor, {u0, texB}});
            outVerts.push_back({bot1, normal, vertColor, {u1, texB}});
            outVerts.push_back({top1, normal, vertColor, {u1, texT}});
        }

        if ((int)outVerts.size() > startIdx)
            outEmitterIds.push_back(id);
    }

    return (int)outVerts.size();
}

// ============================================================================
// Query Methods
// ============================================================================

const RibbonEmitterConfig* RibbonSystem::GetConfig(int id) const {
    auto it = emitters_.find(id);
    return (it != emitters_.end()) ? &it->second.config : nullptr;
}

int RibbonSystem::GetTotalSegmentCount() const {
    int total = 0;
    for (auto& [id, em] : emitters_) total += (int)em.segments.size();
    return total;
}

int RibbonSystem::GetEmitterVertCount(int emitterId) const {
    auto it = emitters_.find(emitterId);
    if (it == emitters_.end()) return 0;
    int segs = (int)it->second.segments.size();
    return (segs > 1) ? (segs - 1) * 6 : 0;
}

// ============================================================================
// Private Helpers
// ============================================================================

void RibbonSystem::SpawnInterpolatedSegment(RibbonEmitter& em, float t, float omt,
                                             const XMFLOAT3& prevDirScaled,
                                             const XMFLOAT3& currDirScaled,
                                             float age)
{
    RibbonSegment seg;

    XMFLOAT3 above0 = {
        em.prevPos.x + em.prevVertical.x * em.state.above,
        em.prevPos.y + em.prevVertical.y * em.state.above,
        em.prevPos.z + em.prevVertical.z * em.state.above
    };
    XMFLOAT3 above1 = {
        em.currPos.x + em.currVertical.x * em.state.above,
        em.currPos.y + em.currVertical.y * em.state.above,
        em.currPos.z + em.currVertical.z * em.state.above
    };
    XMFLOAT3 below0 = {
        em.prevPos.x - em.prevVertical.x * em.state.below,
        em.prevPos.y - em.prevVertical.y * em.state.below,
        em.prevPos.z - em.prevVertical.z * em.state.below
    };
    XMFLOAT3 below1 = {
        em.currPos.x - em.currVertical.x * em.state.below,
        em.currPos.y - em.currVertical.y * em.state.below,
        em.currPos.z - em.currVertical.z * em.state.below
    };

    // Hermite blend for the above (top) vertex
    XMFLOAT3 a0 = {
        above0.x + prevDirScaled.x * t,
        above0.y + prevDirScaled.y * t,
        above0.z + prevDirScaled.z * t
    };
    XMFLOAT3 a1 = {
        above1.x - currDirScaled.x * omt,
        above1.y - currDirScaled.y * omt,
        above1.z - currDirScaled.z * omt
    };
    XMFLOAT3 topPos = {
        a0.x * omt + a1.x * t,
        a0.y * omt + a1.y * t,
        a0.z * omt + a1.z * t
    };

    // Hermite blend for the below (bottom) vertex
    XMFLOAT3 b0 = {
        below0.x + prevDirScaled.x * t,
        below0.y + prevDirScaled.y * t,
        below0.z + prevDirScaled.z * t
    };
    XMFLOAT3 b1 = {
        below1.x - currDirScaled.x * omt,
        below1.y - currDirScaled.y * omt,
        below1.z - currDirScaled.z * omt
    };
    XMFLOAT3 botPos = {
        b0.x * omt + b1.x * t,
        b0.y * omt + b1.y * t,
        b0.z * omt + b1.z * t
    };

    // Center position and up direction from top/bot
    seg.position = {
        (topPos.x + botPos.x) * 0.5f,
        (topPos.y + botPos.y) * 0.5f,
        (topPos.z + botPos.z) * 0.5f
    };

    // Up = direction from center to top, normalized
    float ux = topPos.x - seg.position.x;
    float uy = topPos.y - seg.position.y;
    float uz = topPos.z - seg.position.z;
    float uLen = sqrtf(ux*ux + uy*uy + uz*uz);
    if (uLen > 1e-6f) {
        float inv = 1.0f / uLen;
        seg.up = {ux * inv, uy * inv, uz * inv};
    } else {
        seg.up = {
            em.prevVertical.x * omt + em.currVertical.x * t,
            em.prevVertical.y * omt + em.currVertical.y * t,
            em.prevVertical.z * omt + em.currVertical.z * t
        };
    }

    seg.above    = em.state.above;
    seg.below    = em.state.below;
    seg.age      = (age < 0) ? -age : 0;
    seg.initLife = em.config.life;

    em.segments.push_back(seg);
}

void RibbonSystem::SpawnSegmentAtCurrent(RibbonEmitter& em) {
    RibbonSegment seg;

    seg.position = em.currPos;
    seg.up       = em.currVertical;
    seg.above    = em.state.above;
    seg.below    = em.state.below;
    seg.age      = 0;
    seg.initLife = em.config.life;

    if (!em.segments.empty() && em.segments.back().age <= 0) {
        em.segments.back() = seg;
    } else {
        em.segments.push_back(seg);
    }
}

} // namespace WhiteoutDex
