/// @file
/// @brief Improved Perlin gradient noise in 2D and 3D.
#pragma once

#include "terraingen/noise/hash.h"
#include "terraingen/noise/lattice.h"

namespace terraingen {

namespace detail {

/// @brief Perlin's quintic fade 6t^5 - 15t^4 + 10t^3, C^2 across cell edges.
/// @param t The fraction within a cell, in [0, 1].
/// @return The faded fraction, in [0, 1].
constexpr float PerlinFade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/// @brief Linear interpolation.
/// @param t The weight of `b`.
/// @param a The value at `t` = 0.
/// @param b The value at `t` = 1.
/// @return a + t (b - a).
constexpr float Lerp(float t, float a, float b) {
    return a + t * (b - a);
}

/// @brief Dot product of an offset with one of eight unit gradients: the axis directions and the
/// diagonals.
/// @param hash Corner hash; its low three bits pick the gradient.
/// @param x Offset x from the corner.
/// @param y Offset y from the corner.
/// @return The dot product.
inline float PerlinGradient2(uint32_t hash, float x, float y) {
    constexpr float kInvSqrt2 = 0.707106781186547524f;
    switch (hash & 7u) {
    case 0u:
        return x;
    case 1u:
        return -x;
    case 2u:
        return y;
    case 3u:
        return -y;
    case 4u:
        return (x + y) * kInvSqrt2;
    case 5u:
        return (y - x) * kInvSqrt2;
    case 6u:
        return (x - y) * kInvSqrt2;
    default:
        return -(x + y) * kInvSqrt2;
    }
}

/// @brief The hash of a 2D lattice corner, shared by Perlin and simplex noise.
/// @param ix Corner x, wrapped to 32 bits.
/// @param iy Corner y, wrapped to 32 bits.
/// @param seed The noise seed.
/// @return The hash.
inline uint32_t PerlinCornerHash(uint32_t ix, uint32_t iy, uint32_t seed) {
    return Pcg3dHash(SeededLatticeKey2(ix, iy, seed)).x;
}

/// @brief Dot product of an offset with one of Perlin's twelve cube-edge gradients (sixteen with
/// repeats).
/// @param hash Corner hash; its low four bits pick the gradient.
/// @param x Offset x from the corner.
/// @param y Offset y from the corner.
/// @param z Offset z from the corner.
/// @return The dot product.
inline float PerlinGradient3(uint32_t hash, float x, float y, float z) {
    const uint32_t h = hash & 15u;
    const float u = h < 8u ? x : y;
    const float v = h < 4u ? y : (h == 12u || h == 14u ? x : z);
    return ((h & 1u) == 0u ? u : -u) + ((h & 2u) == 0u ? v : -v);
}

/// @brief The hash of a 3D Perlin lattice corner.
/// @param ix Corner x, wrapped to 32 bits.
/// @param iy Corner y, wrapped to 32 bits.
/// @param iz Corner z, wrapped to 32 bits.
/// @param seed The noise seed.
/// @return The hash.
inline uint32_t PerlinCornerHash3(uint32_t ix, uint32_t iy, uint32_t iz, uint32_t seed) {
    return Pcg3dHash(PerlinLatticeKey3(ix, iy, iz, seed)).x;
}

/// @brief The gradient term of one corner of `point`'s cell.
/// @param point The cell and the fraction within it.
/// @param corner The corner as bits: 1 for +x, 2 for +y and 4 for +z.
/// @param seed The noise seed.
/// @return The corner's gradient dotted with the offset from it.
inline float PerlinCorner3(LatticePoint3 point, uint32_t corner, uint32_t seed) {
    const uint32_t dx = corner & 1u;
    const uint32_t dy = (corner >> 1u) & 1u;
    const uint32_t dz = corner >> 2u;
    const uint32_t hash = PerlinCornerHash3(uint32_t(point.ix) + dx, uint32_t(point.iy) + dy,
                                            uint32_t(point.iz) + dz, seed);
    return PerlinGradient3(hash, point.fx - float(dx), point.fy - float(dy), point.fz - float(dz));
}

}  // namespace detail

/// @brief 2D improved Perlin gradient noise at a lattice cell plus fraction; zero at lattice
/// points. Passing the cell separately keeps precision far from the origin.
/// @param ix Lattice cell x.
/// @param iy Lattice cell y.
/// @param fx Fraction in x, in [0, 1).
/// @param fy Fraction in y, in [0, 1).
/// @param seed The noise seed.
/// @return Noise in [-1, 1].
inline float PerlinNoise2(int32_t ix, int32_t iy, float fx, float fy, uint32_t seed) {
    const uint32_t x0 = uint32_t(ix);
    const uint32_t y0 = uint32_t(iy);
    const uint32_t x1 = x0 + 1u;
    const uint32_t y1 = y0 + 1u;

    const float n00 = detail::PerlinGradient2(detail::PerlinCornerHash(x0, y0, seed), fx, fy);
    const float n10 =
        detail::PerlinGradient2(detail::PerlinCornerHash(x1, y0, seed), fx - 1.0f, fy);
    const float n01 =
        detail::PerlinGradient2(detail::PerlinCornerHash(x0, y1, seed), fx, fy - 1.0f);
    const float n11 =
        detail::PerlinGradient2(detail::PerlinCornerHash(x1, y1, seed), fx - 1.0f, fy - 1.0f);

    const float u = detail::PerlinFade(fx);
    const float v = detail::PerlinFade(fy);

    constexpr float kSqrt2 = 1.41421356237309505f;
    return kSqrt2 * detail::Lerp(v, detail::Lerp(u, n00, n10), detail::Lerp(u, n01, n11));
}

/// @brief 2D improved Perlin gradient noise at a point in lattice units.
/// @param x Lattice x.
/// @param y Lattice y.
/// @param seed The noise seed.
/// @return Noise in [-1, 1].
inline float PerlinNoise2(float x, float y, uint32_t seed) {
    const LatticePoint2 point = SplitLattice2(x, y);
    return PerlinNoise2(point.ix, point.iy, point.fx, point.fy, seed);
}

/// @brief 2D improved Perlin gradient noise at a double-precision point in lattice units, split
/// into cell and fraction before narrowing so it stays exact far from the origin.
/// @param x Lattice x.
/// @param y Lattice y.
/// @param seed The noise seed.
/// @return Noise in [-1, 1].
#ifndef __SLANG__
inline float PerlinNoise2Precise(double x, double y, uint32_t seed) {
    const LatticePoint2 point = SplitLatticePrecise2(x, y);
    return PerlinNoise2(point.ix, point.iy, point.fx, point.fy, seed);
}
#endif

/// @brief 3D improved Perlin gradient noise at a lattice cell and fraction; zero at lattice
/// points and clamped to [-1, 1].
/// @param point The cell and the fraction within it.
/// @param seed The noise seed.
/// @return Noise in [-1, 1].
inline float PerlinNoise3(LatticePoint3 point, uint32_t seed) {
    const float c000 = detail::PerlinCorner3(point, 0u, seed);
    const float c100 = detail::PerlinCorner3(point, 1u, seed);
    const float c010 = detail::PerlinCorner3(point, 2u, seed);
    const float c110 = detail::PerlinCorner3(point, 3u, seed);
    const float c001 = detail::PerlinCorner3(point, 4u, seed);
    const float c101 = detail::PerlinCorner3(point, 5u, seed);
    const float c011 = detail::PerlinCorner3(point, 6u, seed);
    const float c111 = detail::PerlinCorner3(point, 7u, seed);
    const float u = detail::PerlinFade(point.fx);
    const float v = detail::PerlinFade(point.fy);
    const float w = detail::PerlinFade(point.fz);
    const float near = detail::Lerp(v, detail::Lerp(u, c000, c100), detail::Lerp(u, c010, c110));
    const float far = detail::Lerp(v, detail::Lerp(u, c001, c101), detail::Lerp(u, c011, c111));
    return clamp(detail::Lerp(w, near, far), -1.0f, 1.0f);
}

/// @brief 3D improved Perlin gradient noise at a double-precision point in lattice units, split
/// into cell and fraction before narrowing so it stays exact on a planet's surface.
/// @param x Lattice x.
/// @param y Lattice y.
/// @param z Lattice z.
/// @param seed The noise seed.
/// @return Noise in [-1, 1].
#ifndef __SLANG__
inline float PerlinNoise3Precise(double x, double y, double z, uint32_t seed) {
    return PerlinNoise3(SplitLatticePrecise3(x, y, z), seed);
}
#endif

/// @brief 3D improved Perlin gradient noise at a point in lattice units.
/// @param x Lattice x.
/// @param y Lattice y.
/// @param z Lattice z.
/// @param seed The noise seed.
/// @return Noise in [-1, 1].
inline float PerlinNoise3(float x, float y, float z, uint32_t seed) {
    return PerlinNoise3(SplitLattice3(x, y, z), seed);
}

}  // namespace terraingen
