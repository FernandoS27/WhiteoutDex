#pragma once
// ============================================================================
// Renderer-wide coordinate-system utility.
//
// The renderer has a single "native" coordinate space picked at compile time
// via WDX_DEFAULT_COORD_SPACE (default: Blizzard). All asset adapters tag
// incoming data with its source space and route it through
// `CoordinateSystem::ToDefault(from, ...)`, which is a no-op when the source
// already matches the renderer-native space.
//
// Supported spaces today:
//   Blizzard — MDX native: +X forward, +Y left, +Z up, right-handed.
//   Max      — 3ds Max:    +X right,   +Y back, +Z up, right-handed.
//
// To add a new space (e.g. Blender, Unity):
//   1. Append it to the `CoordSpace` enum below.
//   2. Append one `SpaceAxes` row to `kSpaceAxes` in coordinate_system.cpp
//      giving its +X/+Y/+Z axes expressed in the reference (Blizzard) frame.
// No other files need to change — every conversion routes through the table.
//
// All math assumes row-major matrices with v*M semantics, matching
// `whiteout::transform_point` and the legacy `particle/coord_space.cpp`.
// ============================================================================

#include "types.h"

namespace WhiteoutDex {

enum class CoordSpace {
    Blizzard,   // MDX native; reference frame
    Max,        // 3ds Max
    // Blender, Unity, Unreal, ... — append here + one row in kSpaceAxes.
};

// ---- Compile-time default, driven by WDX_DEFAULT_COORD_SPACE ------------
#ifndef WDX_DEFAULT_COORD_SPACE
  #define WDX_DEFAULT_COORD_SPACE Blizzard
#endif
inline constexpr CoordSpace kDefaultCoordSpace = CoordSpace::WDX_DEFAULT_COORD_SPACE;

class CoordinateSystem {
public:
    static constexpr CoordSpace Default() { return kDefaultCoordSpace; }

    // ---- Raw basis access ------------------------------------------------
    // BasisToRef(S):    v_ref = v_s   * M
    // BasisFromRef(S):  v_s   = v_ref * M   ( = transpose of BasisToRef )
    // BasisChange(f,t): v_t   = v_f   * M
    static const Matrix44f& BasisToRef  (CoordSpace s);
    static const Matrix44f& BasisFromRef(CoordSpace s);
    static const Matrix44f& BasisChange (CoordSpace from, CoordSpace to);

    // ---- Typed converters (all no-op when from == to) -------------------
    static Vector3f   ConvertPoint     (CoordSpace from, CoordSpace to, Vector3f v);
    static Vector3f   ConvertDirection (CoordSpace from, CoordSpace to, Vector3f v);
    static Vector3f   ConvertScale     (CoordSpace from, CoordSpace to, Vector3f s);
    static Vector4f   ConvertTangent   (CoordSpace from, CoordSpace to, Vector4f t);
    static Quaternion ConvertQuaternion(CoordSpace from, CoordSpace to, Quaternion q);
    static Matrix44f  ConvertTransform (CoordSpace from, CoordSpace to, const Matrix44f& M);

    // ---- Convenience: 'to' = Default() ---------------------------------
    // Adapters call these: data arrives tagged with its source space and
    // is converted into the renderer-native default. No-op when from == Default().
    static Vector3f   ToDefault(CoordSpace from, Vector3f v)              { return ConvertPoint    (from, Default(), v); }
    static Vector3f   ToDefaultDir(CoordSpace from, Vector3f v)           { return ConvertDirection(from, Default(), v); }
    static Vector3f   ToDefaultScale(CoordSpace from, Vector3f s)         { return ConvertScale    (from, Default(), s); }
    static Vector4f   ToDefaultTangent(CoordSpace from, Vector4f t)       { return ConvertTangent  (from, Default(), t); }
    static Quaternion ToDefault(CoordSpace from, Quaternion q)            { return ConvertQuaternion(from, Default(), q); }
    static Matrix44f  ToDefault(CoordSpace from, const Matrix44f& M)      { return ConvertTransform(from, Default(), M); }
};

} // namespace WhiteoutDex
