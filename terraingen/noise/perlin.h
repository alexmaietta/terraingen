/// @file
/// @brief Improved Perlin gradient noise in 2D and 3D.
#pragma once

#include "terraingen/noise/hash.h"
#include "terraingen/noise/lattice.h"

namespace terraingen {

namespace detail {

/// Dot product of an offset with one of eight unit gradients: the axis directions and the
/// diagonals.
/// @param hash Corner hash; its low three bits pick the gradient.
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

/// The hash of a 2D lattice corner, shared by Perlin and simplex noise.
/// @param ix Corner x, wrapped to 32 bits.
/// @param iy Corner y, wrapped to 32 bits.
inline uint32_t PerlinCornerHash2(uint32_t ix, uint32_t iy, uint32_t seed) {
    return Pcg3dHash(SeededLatticeKey2(ix, iy, seed)).x;
}

/// Dot product of an offset with one of Perlin's twelve cube-edge gradients (sixteen with
/// repeats).
/// @param hash Corner hash; its low four bits pick the gradient.
inline float PerlinGradient3(uint32_t hash, float x, float y, float z) {
    const uint32_t h = hash & 15u;
    const float u = h < 8u ? x : y;
    const float v = h < 4u ? y : (h == 12u || h == 14u ? x : z);
    return ((h & 1u) == 0u ? u : -u) + ((h & 2u) == 0u ? v : -v);
}

/// The hash of a 3D Perlin lattice corner.
/// @param ix Corner x, wrapped to 32 bits.
/// @param iy Corner y, wrapped to 32 bits.
/// @param iz Corner z, wrapped to 32 bits.
inline uint32_t PerlinCornerHash3(uint32_t ix, uint32_t iy, uint32_t iz, uint32_t seed) {
    return Pcg3dHash(PerlinLatticeKey3(ix, iy, iz, seed)).x;
}

/// The gradient term of one corner of `point`'s cell: its gradient dotted with the offset from
/// it.
/// @param corner The corner as bits: 1 for +x, 2 for +y and 4 for +z.
inline float PerlinCorner3(LatticePoint3 point, uint32_t corner, uint32_t seed) {
    const uint32_t dx = corner & 1u;
    const uint32_t dy = (corner >> 1u) & 1u;
    const uint32_t dz = corner >> 2u;
    const uint32_t hash = PerlinCornerHash3(uint32_t(point.ix) + dx, uint32_t(point.iy) + dy,
                                            uint32_t(point.iz) + dz, seed);
    return PerlinGradient3(hash, point.fx - float(dx), point.fy - float(dy), point.fz - float(dz));
}

}  // namespace detail

/// 2D improved Perlin gradient noise at a lattice cell plus fraction; zero at lattice
/// points. Passing the cell separately keeps precision far from the origin.
/// @param fx Fraction in x, in [0, 1).
/// @param fy Fraction in y, in [0, 1).
/// @return Noise in [-1, 1].
inline float PerlinNoise2(int32_t ix, int32_t iy, float fx, float fy, uint32_t seed) {
    const uint32_t x0 = uint32_t(ix);
    const uint32_t y0 = uint32_t(iy);
    const uint32_t x1 = x0 + 1u;
    const uint32_t y1 = y0 + 1u;

    const float n00 = detail::PerlinGradient2(detail::PerlinCornerHash2(x0, y0, seed), fx, fy);
    const float n10 =
        detail::PerlinGradient2(detail::PerlinCornerHash2(x1, y0, seed), fx - 1.0f, fy);
    const float n01 =
        detail::PerlinGradient2(detail::PerlinCornerHash2(x0, y1, seed), fx, fy - 1.0f);
    const float n11 =
        detail::PerlinGradient2(detail::PerlinCornerHash2(x1, y1, seed), fx - 1.0f, fy - 1.0f);

    const float u = detail::PerlinFade(fx);
    const float v = detail::PerlinFade(fy);

    constexpr float kSqrt2f = 1.41421356237309505f;
    return kSqrt2f * detail::Lerp(v, detail::Lerp(u, n00, n10), detail::Lerp(u, n01, n11));
}

/// 2D improved Perlin gradient noise at a point in lattice units.
/// @return Noise in [-1, 1].
inline float PerlinNoise2(float x, float y, uint32_t seed) {
    const LatticePoint2 point = SplitLattice2(x, y);
    return PerlinNoise2(point.ix, point.iy, point.fx, point.fy, seed);
}

/// 2D improved Perlin gradient noise at a double-precision point in lattice units, split
/// into cell and fraction before narrowing so it stays exact far from the origin.
/// @return Noise in [-1, 1].
#ifndef __SLANG__
inline float PerlinNoise2Precise(double x, double y, uint32_t seed) {
    const LatticePoint2 point = SplitLatticePrecise2(x, y);
    return PerlinNoise2(point.ix, point.iy, point.fx, point.fy, seed);
}
#endif

/// 3D improved Perlin gradient noise at a lattice cell and fraction; zero at lattice
/// points and clamped to [-1, 1].
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

/// 3D improved Perlin gradient noise at a double-precision point in lattice units, split
/// into cell and fraction before narrowing so it stays exact on a planet's surface.
/// @return Noise in [-1, 1].
#ifndef __SLANG__
inline float PerlinNoise3Precise(double x, double y, double z, uint32_t seed) {
    return PerlinNoise3(SplitLatticePrecise3(x, y, z), seed);
}
#endif

/// 3D improved Perlin gradient noise at a point in lattice units.
/// @return Noise in [-1, 1].
inline float PerlinNoise3(float x, float y, float z, uint32_t seed) {
    return PerlinNoise3(SplitLattice3(x, y, z), seed);
}

}  // namespace terraingen
