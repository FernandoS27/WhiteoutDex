// ============================================================================
// WhiteoutDex Real-Time Renderer — Ribbon System Implementation
//
// Mirrors CRibbonEmitter (BlizzPartRE/pseudocode/CRibbonEmitter.cpp). Each
// edge is two explicit world-space vertices (top/bot). New edges are emitted
// along the prev→curr path with Hermite-style blending, then aged + sagged
// in a unified update loop. The head edge at the current emitter position
// is excluded from the update loop (it gets overwritten next frame).
// ============================================================================

#include "ribbon.h"
#include "sim_util.h"

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

    Vector3f newPos  = whiteout::transform_point(Vector3f{0,0,0}, st.transform);
    Vector3f newDir   = whiteout::transform_normal(Vector3f{0,0,1}, st.transform).normalized();
    Vector3f newVert  = whiteout::transform_normal(Vector3f{0,1,0}, st.transform).normalized();

    if (em.posSet) {
        em.prevPos      = em.currPos;
        em.prevDir      = em.currDir;
        em.prevVertical = em.currVertical;
    } else {
        em.prevPos      = newPos;
        em.prevDir      = newDir;
        em.prevVertical = newVert;
        em.startTime    = 0;
        em.posSet       = true;
    }
    em.currPos      = newPos;
    em.currDir      = newDir;
    em.currVertical = newVert;
}

bool RibbonSystem::HasEmitters() const { return !emitters_.empty(); }

// ============================================================================
// Simulation — mirrors CRibbonEmitter::Update()
// ============================================================================

void RibbonSystem::Simulate(float dt) {
    dt = ClampDeltaTime(dt);

    for (auto& [id, em] : emitters_) {
        float lifeSpan = em.config.life;
        if (lifeSpan < kRibbonMinLifespan) lifeSpan = kRibbonMinLifespan;

        // dt exceeds lifespan → all edges are dead, reset and continue with dt=0
        if (dt >= lifeSpan) {
            em.segments.clear();
            em.prevPos      = em.currPos;
            em.prevDir      = em.currDir;
            em.prevVertical = em.currVertical;
            em.startTime    = 0;
            dt = 0;
        }

        // Retire dead segments. Ages are monotonically non-increasing from
        // front (oldest) to back (newest), so dead ones are always a prefix.
        // An edge is dead if its age after this update would reach lifespan.
        em.segments.erase(em.segments.begin(),
            std::find_if(em.segments.begin(), em.segments.end(),
                [lifeSpan, dt](const RibbonSegment& s) {
                    return s.age < (lifeSpan - dt);
                }));

        // Emit new edges + head. Gated on dt>0 so a paused parent doesn't
        // accumulate duplicate tip segments at a frozen position every frame.
        bool emittedHead = false;
        if (dt > 0 && IsEmitterVisible(em.state.visibility) &&
            em.config.emission > 0 && em.posSet)
        {
            float edgesPerSec = em.config.emission;
            float endTime     = em.startTime + dt * edgesPerSec;
            float newEdgeTime = 1.0f;

            if (endTime >= 1.0f) {
                int   numNew  = (int)floorf(endTime - newEdgeTime) + 1;
                float ooDenom = (endTime - em.startTime > kVectorEpsilon)
                                ? 1.0f / (endTime - em.startTime) : 1.0f;

                // Interpolation deltas — matches InitInterpDeltas().
                // The "scaled direction" controls the Hermite tangent; the
                // engine multiplies the unit emitter dir by the segment length.
                float dx = em.currPos.x - em.prevPos.x;
                float dy = em.currPos.y - em.prevPos.y;
                float dz = em.currPos.z - em.prevPos.z;
                float dist = sqrtf(dx*dx + dy*dy + dz*dz);

                Vector3f prevDirS = {em.prevDir.x*dist, em.prevDir.y*dist, em.prevDir.z*dist};
                Vector3f currDirS = {em.currDir.x*dist, em.currDir.y*dist, em.currDir.z*dist};

                Vector3f above0 = {em.prevPos.x + em.prevVertical.x * em.state.above,
                                   em.prevPos.y + em.prevVertical.y * em.state.above,
                                   em.prevPos.z + em.prevVertical.z * em.state.above};
                Vector3f above1 = {em.currPos.x + em.currVertical.x * em.state.above,
                                   em.currPos.y + em.currVertical.y * em.state.above,
                                   em.currPos.z + em.currVertical.z * em.state.above};
                Vector3f below0 = {em.prevPos.x - em.prevVertical.x * em.state.below,
                                   em.prevPos.y - em.prevVertical.y * em.state.below,
                                   em.prevPos.z - em.prevVertical.z * em.state.below};
                Vector3f below1 = {em.currPos.x - em.currVertical.x * em.state.below,
                                   em.currPos.y - em.currVertical.y * em.state.below,
                                   em.currPos.z - em.currVertical.z * em.state.below};

                for (int i = 0; i < numNew; ++i) {
                    float t = (newEdgeTime - em.startTime) * ooDenom;
                    if (t < 0) t = 0;
                    if (t > 1) t = 1;
                    float omt = 1.0f - t;

                    RibbonSegment seg;

                    // Hermite-style blend (matches InterpEdge):
                    //   v = (p0 + prevDirS*t)*(1-t) + (p1 - currDirS*(1-t))*t
                    seg.bot = {
                        (below0.x + prevDirS.x*t)*omt + (below1.x - currDirS.x*omt)*t,
                        (below0.y + prevDirS.y*t)*omt + (below1.y - currDirS.y*omt)*t,
                        (below0.z + prevDirS.z*t)*omt + (below1.z - currDirS.z*omt)*t
                    };
                    seg.top = {
                        (above0.x + prevDirS.x*t)*omt + (above1.x - currDirS.x*omt)*t,
                        (above0.y + prevDirS.y*t)*omt + (above1.y - currDirS.y*omt)*t,
                        (above0.z + prevDirS.z*t)*omt + (above1.z - currDirS.z*omt)*t
                    };

                    // Initial age = -dt*t. The unified update loop adds dt,
                    // giving a final age of dt*(1-t): smaller t (older spatially,
                    // closer to prevPos) → older temporally. This matches the
                    // engine's edges[] ring buffer ordering.
                    seg.age = -dt * t;
                    em.segments.push_back(seg);
                    newEdgeTime += 1.0f;
                }
            }

            em.startTime = endTime - floorf(endTime);

            // Head edge at current emitter pose (t=1, age 0). Excluded from
            // the update loop below — it stays "fresh" until the next frame
            // overwrites this slot with a new interpolated edge.
            RibbonSegment head;
            head.top = {em.currPos.x + em.currVertical.x * em.state.above,
                        em.currPos.y + em.currVertical.y * em.state.above,
                        em.currPos.z + em.currVertical.z * em.state.above};
            head.bot = {em.currPos.x - em.currVertical.x * em.state.below,
                        em.currPos.y - em.currVertical.y * em.state.below,
                        em.currPos.z - em.currVertical.z * em.state.below};
            head.age = 0;
            em.segments.push_back(head);
            emittedHead = true;
        }

        // Unified update: gravity sag + age increment for every edge except
        // the head (matches the engine's `pos != writePos` loop boundary).
        size_t updateEnd = emittedHead ? em.segments.size() - 1
                                       : em.segments.size();
        for (size_t i = 0; i < updateEnd; ++i) {
            auto& seg = em.segments[i];
            // Parabolic sag: dz = g*dt^2 + 2*g*age*dt. Sign is negated from
            // the reference because our MDX loader stores ribbon gravity with
            // the same convention as particle emitters (positive = downward).
            float dz = em.config.gravity * dt * dt
                     + 2.0f * em.config.gravity * seg.age * dt;
            seg.top.z -= dz;
            seg.bot.z -= dz;
            seg.age   += dt;
        }
    }
}

// ============================================================================
// Geometry Generation — mirrors CRibbonEmitter::Render() vertex layout
// ============================================================================

RibbonSystem::StripResult RibbonSystem::BuildStrips() const
{
    StripResult result;

    for (auto& [id, em] : emitters_) {
        if (em.state.visibility <= 0.0f) continue;
        if (em.segments.size() < 2)      continue;

        int  startIdx = (int)result.vertices.size();
        auto& segs    = em.segments;
        int   numSegs = (int)segs.size();

        // Texture slot rect (matches ConvertTexSlotToTexCoords with
        // texBox = (0,0,1,1) — the standard MDX ribbon texture region).
        float cellW = (em.config.cols > 0) ? 1.0f / em.config.cols : 1.0f;
        float cellH = (em.config.rows > 0) ? 1.0f / em.config.rows : 1.0f;
        int   slotRow = (em.config.cols > 0) ? em.state.slot / em.config.cols : 0;
        int   slotCol = (em.config.cols > 0) ? em.state.slot % em.config.cols : 0;
        float texL = cellW * slotCol;
        float texT = cellH * slotRow;
        float texR = texL + cellW;
        float texB = texT + cellH;
        float texDU = texR - texL;

        float lifeSpan = em.config.life;
        if (lifeSpan < 0.25f) lifeSpan = 0.25f;
        float ooLife = 1.0f / lifeSpan;

        // Uniform color per emitter (engine writes diffuseClr to localMat
        // before draw — every vertex sees the same color).
        Vector4f vertColor = {em.state.color.x, em.state.color.y, em.state.color.z,
                              em.state.alpha};

        // Engine has a single dummy normal (1,0,0) — ribbons are unshaded
        // in practice (the unshaded flag drives the shader path).
        Vector3f normal = {1, 0, 0};

        // segs[0] is oldest, segs[N-1] is newest (head). Build a quad per
        // adjacent pair. Per-edge U animates from texL (age 0) to texR
        // (age = lifespan), matching the engine's per-edge tex U formula.
        for (int i = 0; i < numSegs - 1; i++) {
            const auto& s0 = segs[i];      // older (larger age, larger U)
            const auto& s1 = segs[i + 1];  // newer (smaller age, smaller U)

            float u0 = texDU * s0.age * ooLife + texL;
            float u1 = texDU * s1.age * ooLife + texL;

            result.vertices.push_back({s0.top, normal, vertColor, {u0, texT}});
            result.vertices.push_back({s0.bot, normal, vertColor, {u0, texB}});
            result.vertices.push_back({s1.top, normal, vertColor, {u1, texT}});

            result.vertices.push_back({s0.bot, normal, vertColor, {u0, texB}});
            result.vertices.push_back({s1.bot, normal, vertColor, {u1, texB}});
            result.vertices.push_back({s1.top, normal, vertColor, {u1, texT}});
        }

        if ((int)result.vertices.size() > startIdx)
            result.emitterIds.push_back(id);
    }

    return result;
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

} // namespace WhiteoutDex
