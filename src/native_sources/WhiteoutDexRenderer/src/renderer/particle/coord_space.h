#pragma once
// ============================================================================
// Coordinate-space helpers for PE2 (and any future) particle service.
//
// The renderer runs in 3ds-Max space but PE2 simulation is authored in
// Blizzard-native space (X-forward, Y-left, Z-up). The importer normally
// applies M_max = C^-1 * M_mdx * C on node TRS at load time (see
// mdx_model_adapter.cpp). PE2 emitters bypass that swizzle so the simulation
// math matches CPlaneParticleEmitter verbatim; BuildGeometry folds the basis
// change back in on the output side.
//
// See docs/PARTICLEEMITTERS2.md §3.8.
// ============================================================================

#include "types.h"

namespace WhiteoutDex::particle {

enum class CoordSpace {
    Blizzard,   // MDX native; default for PE2
    Max         // Renderer native; for directly-authored emitters
};

// C = 90° rotation around +Z that sends Blizzard +X -> Max -Y, +Y -> +X.
// Positions: (x,y,z) -> (y, -x, z) under C^-1 (the "swiz" in the adapter).
// Helpers return 4x4 matrices so they compose cleanly with our Matrix44f pipeline.
Matrix44f BlzToMaxBasis();   // C^-1: Blizzard -> Max (used by BuildGeometry)
Matrix44f MaxToBlzBasis();   // C    : Max -> Blizzard (used to undo adapter swiz)

// Cheap component-level helpers for single points / directions. Match
// mdx_model_adapter.cpp::swizPos / swizQuat exactly.
inline Vector3f BlzToMax(Vector3f v) { return {v.y, -v.x, v.z}; }
inline Vector3f MaxToBlz(Vector3f v) { return {-v.y, v.x, v.z}; }

// Transform-matrix conjugation: given M_max that maps Max-local to Max-world
// (applied as v * M), returns M_blz that maps Blz-local to Blz-world:
//   M_blz = BlzToMaxBasis * M_max * MaxToBlzBasis
// This is the basis-change used by the service when the MDX adapter has
// already swizzled the per-frame transform into Max space (Option B in
// docs/PARTICLEEMITTERS2.md §3.8).
Matrix44f MaxToBlzTransform(const Matrix44f& M_max);

} // namespace WhiteoutDex::particle
