#include "coordinate_system.h"

#include <array>
#include <cassert>
#include <cmath>

namespace WhiteoutDex {

// ============================================================================
// Space description: each entry gives this space's +X/+Y/+Z axes expressed in
// the reference (Blizzard) frame. Row i of BasisToRef(S) = axes[i].
// Must be kept in 1:1 correspondence with the CoordSpace enum.
//
// Verified against legacy `particle/coord_space.cpp` math (see static_asserts
// in DeriveBasisTables below).
// ============================================================================
namespace {

struct SpaceAxes {
    Vector3f xAxis;
    Vector3f yAxis;
    Vector3f zAxis;
};

constexpr SpaceAxes kSpaceAxes[] = {
    // [Blizzard] identity: +X = forward, +Y = left, +Z = up
    { {1.0f, 0.0f, 0.0f},  {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, 1.0f} },
    // [Max] Max's +X = Blizzard's +Y (both are "to the side");
    //       Max's +Y = Blizzard's -X (Max back = Blizzard behind-front);
    //       Max's +Z = Blizzard's +Z.
    // Derivation: legacy BlzToMax(v) = {v.y, -v.x, v.z}; its row-major matrix
    // is BasisFromRef(Max), so BasisToRef(Max) is its transpose — which gives
    // the three axis rows below.
    { {0.0f, 1.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f} },
};
constexpr size_t kSpaceCount = sizeof(kSpaceAxes) / sizeof(kSpaceAxes[0]);

// ----------------------------------------------------------------------------
// Basis-matrix tables, precomputed at static-init.
// ----------------------------------------------------------------------------
std::array<Matrix44f, kSpaceCount>                        g_toRef;
std::array<Matrix44f, kSpaceCount>                        g_fromRef;
std::array<std::array<Matrix44f, kSpaceCount>, kSpaceCount> g_basisChange;
std::array<std::array<float,    kSpaceCount>, kSpaceCount> g_detSign;   // +1 or -1

Matrix44f MakeToRef(const SpaceAxes& s) {
    Matrix44f m = Matrix44f::identity();
    m.data[0][0] = s.xAxis.x; m.data[0][1] = s.xAxis.y; m.data[0][2] = s.xAxis.z;
    m.data[1][0] = s.yAxis.x; m.data[1][1] = s.yAxis.y; m.data[1][2] = s.yAxis.z;
    m.data[2][0] = s.zAxis.x; m.data[2][1] = s.zAxis.y; m.data[2][2] = s.zAxis.z;
    return m;
}

Matrix44f Transpose3x3(const Matrix44f& m) {
    Matrix44f r = Matrix44f::identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.data[i][j] = m.data[j][i];
    return r;
}

float Det3x3(const Matrix44f& m) {
    return m.data[0][0] * (m.data[1][1] * m.data[2][2] - m.data[1][2] * m.data[2][1])
         - m.data[0][1] * (m.data[1][0] * m.data[2][2] - m.data[1][2] * m.data[2][0])
         + m.data[0][2] * (m.data[1][0] * m.data[2][1] - m.data[1][1] * m.data[2][0]);
}

bool InitBasisTables() {
    for (size_t s = 0; s < kSpaceCount; ++s) {
        g_toRef[s]   = MakeToRef(kSpaceAxes[s]);
        g_fromRef[s] = Transpose3x3(g_toRef[s]);
    }
    for (size_t f = 0; f < kSpaceCount; ++f) {
        for (size_t t = 0; t < kSpaceCount; ++t) {
            g_basisChange[f][t] = g_toRef[f] * g_fromRef[t];
            float d = Det3x3(g_basisChange[f][t]);
            g_detSign[f][t] = (d >= 0.0f) ? 1.0f : -1.0f;
        }
    }
    return true;
}

[[maybe_unused]] const bool g_basisInit = InitBasisTables();

inline size_t Ix(CoordSpace s) { return static_cast<size_t>(s); }

} // namespace

// ============================================================================
// Public basis accessors
// ============================================================================

const Matrix44f& CoordinateSystem::BasisToRef(CoordSpace s) {
    assert(Ix(s) < kSpaceCount);
    return g_toRef[Ix(s)];
}

const Matrix44f& CoordinateSystem::BasisFromRef(CoordSpace s) {
    assert(Ix(s) < kSpaceCount);
    return g_fromRef[Ix(s)];
}

const Matrix44f& CoordinateSystem::BasisChange(CoordSpace from, CoordSpace to) {
    assert(Ix(from) < kSpaceCount && Ix(to) < kSpaceCount);
    return g_basisChange[Ix(from)][Ix(to)];
}

// ============================================================================
// Typed converters
// ============================================================================

Vector3f CoordinateSystem::ConvertPoint(CoordSpace from, CoordSpace to, Vector3f v) {
    if (from == to) return v;
    return whiteout::transform_point(v, g_basisChange[Ix(from)][Ix(to)]);
}

Vector3f CoordinateSystem::ConvertDirection(CoordSpace from, CoordSpace to, Vector3f v) {
    if (from == to) return v;
    return whiteout::transform_normal(v, g_basisChange[Ix(from)][Ix(to)]);
}

// Scale components are magnitudes: permute axes but drop signs. Only valid for
// axis-aligned (permutation-only) bases — assert that here.
Vector3f CoordinateSystem::ConvertScale(CoordSpace from, CoordSpace to, Vector3f s) {
    if (from == to) return s;
    const Matrix44f& M = g_basisChange[Ix(from)][Ix(to)];
    // Component i of out = |M[i][out_axis]| * s.component(in_axis where M[i][?]!=0).
    // For axis-aligned swizzles each row has exactly one nonzero entry.
    float in[3]  = { s.x, s.y, s.z };
    float out[3] = { 0, 0, 0 };
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            float v = M.data[i][j];
            if (v != 0.0f) {
                assert(std::abs(std::abs(v) - 1.0f) < 1e-5f && "ConvertScale requires axis-aligned basis");
                out[j] = in[i] * std::abs(v);
            }
        }
    }
    return { out[0], out[1], out[2] };
}

Vector4f CoordinateSystem::ConvertTangent(CoordSpace from, CoordSpace to, Vector4f t) {
    if (from == to) return t;
    Vector3f xyz = ConvertDirection(from, to, Vector3f{ t.x, t.y, t.z });
    // Right-handed -> right-handed: det = +1, w unchanged. Handedness flip
    // (reflection) inverts w (bitangent orientation).
    float w = t.w * g_detSign[Ix(from)][Ix(to)];
    return { xyz.x, xyz.y, xyz.z, w };
}

Quaternion CoordinateSystem::ConvertQuaternion(CoordSpace from, CoordSpace to, Quaternion q) {
    if (from == to) return q;
    // A quaternion's (x,y,z) is (rotation-axis * sin(theta/2)) — transforms as
    // a direction. Scalar w stays under det=+1, flips under det=-1 (reflection
    // reverses rotation sense).
    Vector3f v = ConvertDirection(from, to, Vector3f{ q.x, q.y, q.z });
    float w    = q.w * g_detSign[Ix(from)][Ix(to)];
    return { v.x, v.y, v.z, w };
}

Matrix44f CoordinateSystem::ConvertTransform(CoordSpace from, CoordSpace to, const Matrix44f& M) {
    if (from == to) return M;
    // M_to = BasisChange(to, from) * M_from * BasisChange(from, to)
    // (row-major v*M: conjugate so that v_to * M_to produces the same point
    //  as v_from * M_from reinterpreted via the basis change.)
    return g_basisChange[Ix(to)][Ix(from)] * M * g_basisChange[Ix(from)][Ix(to)];
}

} // namespace WhiteoutDex
