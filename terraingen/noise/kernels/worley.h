#pragma once

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/lattice.h"

namespace terraingen {

/// Distances from a point to its nearest (F1) and second-nearest (F2) feature points, in lattice
/// units.
struct WorleyDistances {
    float f1 = 0.0f;
    float f2 = 0.0f;
};

/// Worley cellular distances at lattice cell (`ix`, `iy`) plus fractional offset (`fx`, `fy`).
///
/// Every lattice cell holds one feature point at a pseudo-random position from `pcg3d`. The 5 x 5
/// cells around the point are searched, which makes F1 exact (it never exceeds sqrt(2), and points
/// outside the window are at least 2 away) and F2 exact except in rare configurations where the
/// second-nearest point lies more than two cells away.
///
/// Reference: S. Worley, "A Cellular Texture Basis Function", SIGGRAPH '96, 291-294, 1996.
/// https://doi.org/10.1145/237170.237267
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

/// Worley cellular distances at (`x`, `y`) in lattice units; see the cell-and-offset overload.
inline WorleyDistances Worley2(float x, float y, uint32_t seed) {
    const LatticePoint2 point = SplitLattice2(x, y);
    return Worley2(point.ix, point.iy, point.fx, point.fy, seed);
}

}  // namespace terraingen
