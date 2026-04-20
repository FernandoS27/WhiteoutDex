#pragma once
// ============================================================================
// Particle geometry builder — ports CParticleEmitter2::Render / RenderSort /
// RenderParticle to produce view-space billboard vertices.
//
// The build runs per-emitter. The output vertex stream is already in view
// space; the draw code should bind an identity view matrix (see
// docs/PARTICLEEMITTERS2.md §3.5).
// ============================================================================

#include "particle2_emitter.h"
#include "types.h"

#include <functional>
#include <vector>

namespace WhiteoutDex::particle {

// Fog sampler hook. worldPos is the particle's position in Blizzard-space
// (matching RE semantics). Returns an opaque CImVector; default sampler
// returns white so CombineColors is a no-op.
using FogSampler = std::function<ImVector(const Vector3f& /*worldPos*/)>;

struct BuildGeometryInput {
    const Matrix44f* worldToView = nullptr;   // Max-space view matrix
    bool             fogEnabled  = false;
    FogSampler       fogSampler  = nullptr;   // may be null when fogEnabled=false
};

// Appends this emitter's verts to `out`. Returns the number appended.
int BuildEmitterGeometry(const Emitter2& emitter,
                         const BuildGeometryInput& in,
                         std::vector<Vertex>& out);

} // namespace WhiteoutDex::particle
