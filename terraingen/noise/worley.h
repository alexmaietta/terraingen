// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Worley cellular noise in 2D and 3D: feature-point distances and Voronoi cell values.
#pragma once

#include "terraingen/noise/hash.h"
#include "terraingen/noise/lattice.h"

namespace terraingen {

/// Distances from a point to its nearest and second-nearest feature points, and the random value
/// of the nearest point's Voronoi cell.
struct WorleyDistances {
    float f1 = 0.0f;     ///< Distance to the nearest feature point (F1), in lattice units.
    float f2 = 0.0f;     ///< Distance to the second-nearest feature point (F2), in lattice units.
    float value = 0.0f;  ///< Value of the nearest point's cell, uniform in [-1, 1).
};

namespace detail {

/// The squared gap along one axis between a fraction `f` of the center cell and the cell `d`
/// cells away.
inline float CellGapSquared(int32_t d, float f) {
    float gap = 0.0f;
    if (d > 0) {
        gap = float(d) - f;
    } else if (d < 0) {
        gap = f - float(d + 1);
    }
    return gap * gap;
}

}  // namespace detail

/// Worley cellular distances at a lattice cell plus fraction, one feature point per cell,
/// searching the 5 x 5 cells around it: F1 and the cell value are exact, F2 exact but for rare
/// configurations.
/// @param fx Fraction in x, in [0, 1).
/// @param fy Fraction in y, in [0, 1).
/// @return F1 in [0, sqrt(2)] and F2 >= F1.
inline WorleyDistances Worley2(int32_t ix, int32_t iy, float fx, float fy, uint32_t seed) {
    float d1 = 1e30f;
    float d2 = 1e30f;
    uint32_t nearest = 0u;
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
                nearest = h.x;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    const WorleyDistances distances = {sqrt(d1), sqrt(d2), 2.0f * HashToUnitFloat(nearest) - 1.0f};
    return distances;
}

/// Worley cellular distances at a point in lattice units.
/// @return F1 in [0, sqrt(2)] and F2 >= F1.
inline WorleyDistances Worley2(float x, float y, uint32_t seed) {
    const LatticePoint2 point = SplitLattice2(x, y);
    return Worley2(point.ix, point.iy, point.fx, point.fy, seed);
}

/// Worley cellular distances in 3D, one feature point per cell: the 3 x 3 x 3 cells around the
/// point, then the outer shell of the 5 x 5 x 5 cells where a cell could still be nearer than F2.
/// F1 and the cell value are exact, F2 exact but for rare configurations.
/// @return F1 in [0, sqrt(3)] and F2 >= F1.
inline WorleyDistances Worley3(LatticePoint3 point, uint32_t seed) {
    const uint32_t seedKey = PcgHash(seed);
    float d1 = 1e30f;
    float d2 = 1e30f;
    uint32_t nearest = 0u;
    for (int32_t shell = 0; shell < 2; ++shell) {
        for (int32_t dz = -2; dz <= 2; ++dz) {
            for (int32_t dy = -2; dy <= 2; ++dy) {
                for (int32_t dx = -2; dx <= 2; ++dx) {
                    const bool inner = abs(dx) <= 1 && abs(dy) <= 1 && abs(dz) <= 1;
                    if (inner != (shell == 0)) {
                        continue;
                    }
                    if (shell == 1 && detail::CellGapSquared(dx, point.fx) +
                                              detail::CellGapSquared(dy, point.fy) +
                                              detail::CellGapSquared(dz, point.fz) >=
                                          d2) {
                        continue;
                    }
                    const UVec3 h = Pcg3dHash(detail::CellularLatticeKey3(
                        uint32_t(point.ix + dx), uint32_t(point.iy + dy), uint32_t(point.iz + dz),
                        seedKey));
                    const float px = float(dx) + HashToUnitFloat(h.x) - point.fx;
                    const float py = float(dy) + HashToUnitFloat(h.y) - point.fy;
                    const float pz = float(dz) + HashToUnitFloat(h.z) - point.fz;
                    const float d = px * px + py * py + pz * pz;
                    if (d < d1) {
                        d2 = d1;
                        d1 = d;
                        nearest = PcgHash(h.x ^ h.y ^ h.z);
                    } else if (d < d2) {
                        d2 = d;
                    }
                }
            }
        }
    }
    const WorleyDistances distances = {sqrt(d1), sqrt(d2), 2.0f * HashToUnitFloat(nearest) - 1.0f};
    return distances;
}

/// Worley cellular distances in 3D at a point in lattice units.
/// @return F1 in [0, sqrt(3)] and F2 >= F1.
inline WorleyDistances Worley3(float x, float y, float z, uint32_t seed) {
    return Worley3(SplitLattice3(x, y, z), seed);
}

}  // namespace terraingen
