/// @file
/// @brief Simplex noise in 2D and 3D.
#pragma once

#include "terraingen/noise/perlin.h"

namespace terraingen {

namespace detail {

/// The contribution (0.5 - r^2)^4 dot(g, d) of one simplex corner; 0 for r^2 >= 0.5.
/// @param ci Corner x on the skewed lattice, wrapped to 32 bits.
/// @param cj Corner y on the skewed lattice, wrapped to 32 bits.
/// @param dx Offset x from the corner, unskewed.
/// @param dy Offset y from the corner, unskewed.
inline float SimplexCorner(uint32_t ci, uint32_t cj, float dx, float dy, uint32_t seed) {
    const float t = 0.5f - dx * dx - dy * dy;
    if (t <= 0.0f) {
        return 0.0f;
    }
    const float t2 = t * t;
    return t2 * t2 * PerlinGradient2(PerlinCornerHash2(ci, cj, seed), dx, dy);
}

/// The contribution (0.5 - r^2)^4 dot(g, d) of one corner of a 3D simplex, with `PerlinNoise3`'s
/// gradients and corner hash; 0 for r^2 >= 0.5.
inline float SimplexCorner3(uint32_t ci, uint32_t cj, uint32_t ck, float dx, float dy, float dz,
                            uint32_t seed) {
    const float t = 0.5f - dx * dx - dy * dy - dz * dz;
    if (t <= 0.0f) {
        return 0.0f;
    }
    const float t2 = t * t;
    return t2 * t2 * PerlinGradient3(PerlinCornerHash3(ci, cj, ck, seed), dx, dy, dz);
}

}  // namespace detail

/// 2D simplex noise at a point in lattice units, with the gradients and corner hash of
/// `PerlinNoise2`.
/// @return Noise in [-1, 1].
inline float SimplexNoise2(float x, float y, uint32_t seed) {
    constexpr float kSkew = 0.366025403784438647f;    // (sqrt(3) - 1) / 2
    constexpr float kUnskew = 0.211324865405187118f;  // (3 - sqrt(3)) / 6
    constexpr float kScale = 99.0f;

    const float skew = (x + y) * kSkew;
    const float cellX = floor(x + skew);
    const float cellY = floor(y + skew);
    const float unskew = (cellX + cellY) * kUnskew;
    const float x0 = x - (cellX - unskew);
    const float y0 = y - (cellY - unskew);

    const uint32_t stepX = x0 > y0 ? 1u : 0u;
    const uint32_t stepY = 1u - stepX;
    const float x1 = x0 - float(stepX) + kUnskew;
    const float y1 = y0 - float(stepY) + kUnskew;
    const float x2 = x0 - 1.0f + 2.0f * kUnskew;
    const float y2 = y0 - 1.0f + 2.0f * kUnskew;

    const uint32_t i = uint32_t(int32_t(cellX));
    const uint32_t j = uint32_t(int32_t(cellY));
    return kScale * (detail::SimplexCorner(i, j, x0, y0, seed) +
                     detail::SimplexCorner(i + stepX, j + stepY, x1, y1, seed) +
                     detail::SimplexCorner(i + 1u, j + 1u, x2, y2, seed));
}

/// 3D simplex noise (Perlin 2001, Gustavson 2005) at a lattice cell and fraction, on the
/// tetrahedral lattice skewed by (x + y + z) / 3. The skew is split into integer and fractional
/// parts, so points far from the origin keep the precision of their fraction.
/// @return Noise in [-1, 1].
inline float SimplexNoise3(LatticePoint3 point, uint32_t seed) {
    constexpr float kScale = 76.0f;
    const int32_t cellSum = int32_t(uint32_t(point.ix) + uint32_t(point.iy) + uint32_t(point.iz));
    int32_t third = cellSum / 3;
    if (cellSum - 3 * third < 0) {
        third -= 1;
    }
    const float remainder = float(cellSum - 3 * third);
    const float skew = (remainder + point.fx + point.fy + point.fz) / 3.0f;
    const float stepX = floor(point.fx + skew);
    const float stepY = floor(point.fy + skew);
    const float stepZ = floor(point.fz + skew);
    const float unskew = (remainder + stepX + stepY + stepZ) / 6.0f;
    const float x0 = point.fx - stepX + unskew;
    const float y0 = point.fy - stepY + unskew;
    const float z0 = point.fz - stepZ + unskew;

    const uint32_t i1 = x0 >= y0 && x0 >= z0 ? 1u : 0u;
    const uint32_t j1 = y0 > x0 && y0 >= z0 ? 1u : 0u;
    const uint32_t k1 = z0 > x0 && z0 > y0 ? 1u : 0u;
    const uint32_t i2 = x0 >= y0 || x0 >= z0 ? 1u : 0u;
    const uint32_t j2 = y0 > x0 || y0 >= z0 ? 1u : 0u;
    const uint32_t k2 = z0 > x0 || z0 > y0 ? 1u : 0u;
    constexpr float kUnskew = 1.0f / 6.0f;
    const float x1 = x0 - float(i1) + kUnskew;
    const float y1 = y0 - float(j1) + kUnskew;
    const float z1 = z0 - float(k1) + kUnskew;
    const float x2 = x0 - float(i2) + 2.0f * kUnskew;
    const float y2 = y0 - float(j2) + 2.0f * kUnskew;
    const float z2 = z0 - float(k2) + 2.0f * kUnskew;
    const float x3 = x0 - 1.0f + 3.0f * kUnskew;
    const float y3 = y0 - 1.0f + 3.0f * kUnskew;
    const float z3 = z0 - 1.0f + 3.0f * kUnskew;

    const uint32_t i = uint32_t(point.ix) + uint32_t(third) + uint32_t(int32_t(stepX));
    const uint32_t j = uint32_t(point.iy) + uint32_t(third) + uint32_t(int32_t(stepY));
    const uint32_t k = uint32_t(point.iz) + uint32_t(third) + uint32_t(int32_t(stepZ));
    const float sum = detail::SimplexCorner3(i, j, k, x0, y0, z0, seed) +
                      detail::SimplexCorner3(i + i1, j + j1, k + k1, x1, y1, z1, seed) +
                      detail::SimplexCorner3(i + i2, j + j2, k + k2, x2, y2, z2, seed) +
                      detail::SimplexCorner3(i + 1u, j + 1u, k + 1u, x3, y3, z3, seed);
    return clamp(kScale * sum, -1.0f, 1.0f);
}

/// 3D simplex noise at a point in lattice units.
/// @return Noise in [-1, 1].
inline float SimplexNoise3(float x, float y, float z, uint32_t seed) {
    return SimplexNoise3(SplitLattice3(x, y, z), seed);
}

}  // namespace terraingen
