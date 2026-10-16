/// @file
/// @brief 2D simplex noise.
#pragma once

#include "terraingen/noise/kernels/perlin.h"

namespace terraingen {

namespace detail {

/// @brief The contribution (0.5 - r^2)^4 dot(g, d) of one simplex corner.
/// @param ci Corner x on the skewed lattice, wrapped to 32 bits.
/// @param cj Corner y on the skewed lattice, wrapped to 32 bits.
/// @param dx Offset x from the corner, unskewed.
/// @param dy Offset y from the corner, unskewed.
/// @param seed The noise seed.
/// @return The contribution; 0 for r^2 >= 0.5.
inline float SimplexCorner(uint32_t ci, uint32_t cj, float dx, float dy, uint32_t seed) {
    const float t = 0.5f - dx * dx - dy * dy;
    if (t <= 0.0f) {
        return 0.0f;
    }
    const float t2 = t * t;
    return t2 * t2 * PerlinGradient2(PerlinCornerHash(ci, cj, seed), dx, dy);
}

}  // namespace detail

/// @brief 2D simplex noise at a point in lattice units, with the gradients and corner hash of
/// `PerlinNoise2`.
/// @param x Lattice x.
/// @param y Lattice y.
/// @param seed The noise seed.
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

}  // namespace terraingen
