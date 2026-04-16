#include "coord_space.h"

namespace WhiteoutDex::particle {

// Convention (whiteout::Matrix44f): row-major data[row][col]; transform_point
// treats the vector as a row on the left -- v * M. Row 0..2 hold the basis
// vectors of the new axes in old-space coordinates.
//
// Blizzard -> Max sends +X -> -Y, +Y -> +X, +Z -> +Z. So the rows are:
//   row 0: image of +X = (0, -1, 0)
//   row 1: image of +Y = (1,  0, 0)
//   row 2: image of +Z = (0,  0, 1)
Matrix44f BlzToMaxBasis() {
    Matrix44f m = Matrix44f::identity();
    m.data[0][0] = 0.0f; m.data[0][1] = -1.0f; m.data[0][2] = 0.0f;
    m.data[1][0] = 1.0f; m.data[1][1] =  0.0f; m.data[1][2] = 0.0f;
    // row 2 and 3 remain identity
    return m;
}

// Inverse: Max -> Blizzard sends +X -> +Y, +Y -> -X, +Z -> +Z.
Matrix44f MaxToBlzBasis() {
    Matrix44f m = Matrix44f::identity();
    m.data[0][0] =  0.0f; m.data[0][1] = 1.0f; m.data[0][2] = 0.0f;
    m.data[1][0] = -1.0f; m.data[1][1] = 0.0f; m.data[1][2] = 0.0f;
    return m;
}

Matrix44f MaxToBlzTransform(const Matrix44f& M_max) {
    // Row-major v*M: M_blz = BlzToMaxBasis * M_max * MaxToBlzBasis
    return BlzToMaxBasis() * M_max * MaxToBlzBasis();
}

} // namespace WhiteoutDex::particle
