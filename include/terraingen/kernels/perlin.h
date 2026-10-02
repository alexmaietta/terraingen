#pragma once

#include "terraingen/kernels/hash.h"

#include <cmath>
#include <cstdint>

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
inline float PerlinGradient2(std::uint32_t hash, float x, float y) {
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

inline std::uint32_t PerlinCornerHash(std::uint32_t ix, std::uint32_t iy, std::uint32_t seed) {
    return Pcg3dHash(UVec3{ix, iy, seed}).x;
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
inline float PerlinNoise2(std::int32_t ix, std::int32_t iy, float fx, float fy,
                          std::uint32_t seed) {
    const auto x0 = static_cast<std::uint32_t>(ix);
    const auto y0 = static_cast<std::uint32_t>(iy);
    const std::uint32_t x1 = x0 + 1u;
    const std::uint32_t y1 = y0 + 1u;

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
inline float PerlinNoise2(float x, float y, std::uint32_t seed) {
    const float cellX = std::floor(x);
    const float cellY = std::floor(y);
    return PerlinNoise2(static_cast<std::int32_t>(cellX), static_cast<std::int32_t>(cellY),
                        x - cellX, y - cellY, seed);
}

}  // namespace terraingen
