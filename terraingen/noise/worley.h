/// @file
/// @brief 2D Worley cellular noise distances.
#pragma once

#include "terraingen/noise/hash.h"
#include "terraingen/noise/lattice.h"

namespace terraingen {

/// @brief Distances from a point to its nearest and second-nearest feature points.
struct WorleyDistances {
    float f1 = 0.0f;  ///< Distance to the nearest feature point (F1), in lattice units.
    float f2 = 0.0f;  ///< Distance to the second-nearest feature point (F2), in lattice units.
};

/// @brief Worley cellular distances at a lattice cell plus fraction, one feature point per cell,
/// searching the 5 x 5 cells around it: F1 is exact, F2 exact but for rare configurations.
/// @param ix Lattice cell x.
/// @param iy Lattice cell y.
/// @param fx Fraction in x, in [0, 1).
/// @param fy Fraction in y, in [0, 1).
/// @param seed The noise seed.
/// @return F1 in [0, sqrt(2)] and F2 >= F1.
inline WorleyDistances Worley2(int32_t ix, int32_t iy, float fx, float fy, uint32_t seed) {
    float d1 = 1e30f;
    float d2 = 1e30f;
    for (int32_t dy = -2; dy <= 2; ++dy) {
        for (int32_t dx = -2; dx <= 2; ++dx) {
            const UVec3 h =
                Pcg3dHash(detail::SeededLatticeKey2(uint32_t(ix + dx), uint32_t(iy + dy), seed));
            const float px = float(dx) + HashToUnitFloat(h.y) - fx;
            const float py = float(dy) + HashToUnitFloat(h.z) - fy;
            const float d = px * px + py * py;
            if (d < d1) {
                d2 = d1;
                d1 = d;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    const WorleyDistances distances = {sqrt(d1), sqrt(d2)};
    return distances;
}

/// @brief Worley cellular distances at a point in lattice units.
/// @param x Lattice x.
/// @param y Lattice y.
/// @param seed The noise seed.
/// @return F1 in [0, sqrt(2)] and F2 >= F1.
inline WorleyDistances Worley2(float x, float y, uint32_t seed) {
    const LatticePoint2 point = SplitLattice2(x, y);
    return Worley2(point.ix, point.iy, point.fx, point.fy, seed);
}

}  // namespace terraingen
