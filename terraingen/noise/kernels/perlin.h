#pragma once

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/lattice.h"

namespace terraingen {

namespace detail {

/// Quintic fade curve 6t^5 - 15t^4 + 10t^3, which has zero first and second derivatives at t = 0
/// and t = 1, so noise is C^2 continuous across cell edges.
///
/// Reference: K. Perlin, "Improving Noise", ACM Transactions on Graphics 21(3):681-682, 2002
/// (SIGGRAPH 2002). https://doi.org/10.1145/566654.566636
constexpr float PerlinFade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

constexpr float Lerp(float t, float a, float b) {
    return a + t * (b - a);
}

/// Dot product of the offset (x, y) with one of eight unit gradients: the four axis directions and
/// the four diagonals.
///
/// Perlin (2002) replaced random gradients with a small fixed set to avoid clumping; this is the 2D
/// analogue of his twelve cube-edge gradients, normalized so every gradient has unit length.
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

inline uint32_t PerlinCornerHash(uint32_t ix, uint32_t iy, uint32_t seed) {
    return Pcg3dHash(SeededLatticeKey2(ix, iy, seed)).x;
}

/// Dot product of the offset (x, y, z) with one of Perlin's (2002) twelve gradients, the vectors
/// from the centre of a cube to the midpoints of its edges, chosen by the low four bits of `hash`
/// with four of them repeated to make sixteen, as in his reference implementation.
inline float PerlinGradient3(uint32_t hash, float x, float y, float z) {
    const uint32_t h = hash & 15u;
    const float u = h < 8u ? x : y;
    const float v = h < 4u ? y : (h == 12u || h == 14u ? x : z);
    return ((h & 1u) == 0u ? u : -u) + ((h & 2u) == 0u ? v : -v);
}

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

/// 2D improved Perlin gradient noise at lattice cell (`ix`, `iy`) plus fractional offset
/// (`fx`, `fy`) in [0, 1).
///
/// Taking the integer cell separately lets callers keep sub-cell precision far from the origin,
/// where a single `float` coordinate would run out of mantissa bits.
///
/// Gradient noise (Perlin 1985) interpolates, with the quintic fade of Perlin (2002), the dot
/// products of pseudo-random unit gradients at the four cell corners with the offsets to those
/// corners. Perlin's permutation table is replaced by a stateless `pcg3d` hash of (cell, seed)
/// (Jarzynski and Olano 2020), so any seed works without tables and the function ports directly to
/// GPU shading languages.
///
/// Noise is zero at every lattice point. With unit gradients its magnitude is at most sqrt(2)/2,
/// reached at cell centres, so the result is scaled by sqrt(2) to span [-1, 1].
///
/// References:
/// - K. Perlin, "An Image Synthesizer", SIGGRAPH '85, Computer Graphics 19(3):287-296, 1985.
///   https://doi.org/10.1145/325165.325247
/// - K. Perlin, "Improving Noise", ACM Transactions on Graphics 21(3):681-682, 2002.
///   https://doi.org/10.1145/566654.566636
/// - M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020.
///
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

/// 2D improved Perlin gradient noise at (`x`, `y`) in lattice units; see the cell-and-offset
/// overload for the method and references.
///
/// @return Noise in [-1, 1].
inline float PerlinNoise2(float x, float y, uint32_t seed) {
    const LatticePoint2 point = SplitLattice2(x, y);
    return PerlinNoise2(point.ix, point.iy, point.fx, point.fy, seed);
}

/// 2D improved Perlin gradient noise at (`x`, `y`) in lattice units given in double precision,
/// split into an integer cell and a fraction before narrowing, so it stays exact far from the
/// origin.
///
/// @return Noise in [-1, 1].
#ifndef __SLANG__
inline float PerlinNoise2Precise(double x, double y, uint32_t seed) {
    const LatticePoint2 point = SplitLatticePrecise2(x, y);
    return PerlinNoise2(point.ix, point.iy, point.fx, point.fy, seed);
}
#endif

/// 3D improved Perlin gradient noise at lattice cell and fraction `point`.
///
/// Perlin's (2002) improved noise: the dot products of the offsets to the cell's eight corners with
/// gradients from his set of twelve cube-edge vectors, interpolated with the quintic fade. As in
/// `PerlinNoise2`, his permutation table is replaced by a stateless `pcg3d` hash of (cell, seed)
/// (Jarzynski and Olano 2020), so any seed works and the function ports to GPU shading languages.
/// Noise is zero at every lattice point. Over 2e7 random samples it stays within 0.9995 of zero,
/// with an RMS of 0.27; it is clamped to [-1, 1] so that no rare corner case can leave it.
///
/// References:
/// - K. Perlin, "Improving Noise", ACM Transactions on Graphics 21(3):681-682, 2002.
///   https://doi.org/10.1145/566654.566636
/// - K. Perlin, "Improved Noise reference implementation", 2002.
/// https://mrl.cs.nyu.edu/~perlin/noise/
/// - M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020.
///
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

/// 3D improved Perlin gradient noise at (`x`, `y`, `z`) in lattice units given in double precision,
/// split into an integer cell and a fraction before narrowing, so it stays exact far from the
/// origin, such as on a planet's surface.
///
/// @return Noise in [-1, 1].
#ifndef __SLANG__
inline float PerlinNoise3Precise(double x, double y, double z, uint32_t seed) {
    return PerlinNoise3(SplitLatticePrecise3(x, y, z), seed);
}
#endif

/// 3D improved Perlin gradient noise at (`x`, `y`, `z`) in lattice units; see the cell-and-offset
/// overload for the method and references.
///
/// @return Noise in [-1, 1].
inline float PerlinNoise3(float x, float y, float z, uint32_t seed) {
    return PerlinNoise3(SplitLattice3(x, y, z), seed);
}

}  // namespace terraingen
