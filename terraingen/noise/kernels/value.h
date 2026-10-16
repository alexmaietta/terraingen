#pragma once

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/perlin.h"

namespace terraingen {

namespace detail {

/// The value in [-1, 1) that value noise gives the lattice point with `pcg3d` key `key`.
inline float LatticeValue(UVec3 key) {
    return 2.0f * HashToUnitFloat(Pcg3dHash(key).y) - 1.0f;
}

/// @brief The value of one corner of `point`'s cell in 3D value noise.
/// @param point The cell and the fraction within it.
/// @param corner The corner as bits: 1 for +x, 2 for +y and 4 for +z.
/// @param seedKey `PcgHash` of the noise seed.
/// @return The corner's value in [-1, 1).
inline float ValueCorner3(LatticePoint3 point, uint32_t corner, uint32_t seedKey) {
    return LatticeValue(ValueLatticeKey3(uint32_t(point.ix) + (corner & 1u),
                                         uint32_t(point.iy) + ((corner >> 1u) & 1u),
                                         uint32_t(point.iz) + (corner >> 2u), seedKey));
}

}  // namespace detail

/// 2D value noise at lattice cell (`ix`, `iy`) plus fractional offset (`fx`, `fy`) in [0, 1).
///
/// Each lattice point gets a pseudo-random value in [-1, 1) from `pcg3d`, and values are blended
/// with Perlin's quintic fade curve. Cheaper than gradient noise but blobbier, with visible
/// lattice alignment; mostly useful as a baseline or a soft mask.
///
/// Reference: D. R. Peachey, "Building Procedural Textures", in D. S. Ebert et al., Texturing and
/// Modeling: A Procedural Approach, 3rd ed., Morgan Kaufmann, 2003 (value noise).
///
/// @return Noise in [-1, 1].
inline float ValueNoise2(int32_t ix, int32_t iy, float fx, float fy, uint32_t seed) {
    const uint32_t x0 = uint32_t(ix);
    const uint32_t y0 = uint32_t(iy);
    const float v00 = detail::LatticeValue(detail::SeededLatticeKey2(x0, y0, seed));
    const float v10 = detail::LatticeValue(detail::SeededLatticeKey2(x0 + 1u, y0, seed));
    const float v01 = detail::LatticeValue(detail::SeededLatticeKey2(x0, y0 + 1u, seed));
    const float v11 = detail::LatticeValue(detail::SeededLatticeKey2(x0 + 1u, y0 + 1u, seed));
    const float u = detail::PerlinFade(fx);
    const float v = detail::PerlinFade(fy);
    return detail::Lerp(v, detail::Lerp(u, v00, v10), detail::Lerp(u, v01, v11));
}

/// 2D value noise at (`x`, `y`) in lattice units; see the cell-and-offset overload.
///
/// @return Noise in [-1, 1].
inline float ValueNoise2(float x, float y, uint32_t seed) {
    const LatticePoint2 point = SplitLattice2(x, y);
    return ValueNoise2(point.ix, point.iy, point.fx, point.fy, seed);
}

/// 3D value noise at lattice cell and fraction `point`, as the 2D overload, blended trilinearly
/// with the quintic fade.
///
/// @return Noise in [-1, 1].
inline float ValueNoise3(LatticePoint3 point, uint32_t seed) {
    const uint32_t key = PcgHash(seed);
    const float v000 = detail::ValueCorner3(point, 0u, key);
    const float v100 = detail::ValueCorner3(point, 1u, key);
    const float v010 = detail::ValueCorner3(point, 2u, key);
    const float v110 = detail::ValueCorner3(point, 3u, key);
    const float v001 = detail::ValueCorner3(point, 4u, key);
    const float v101 = detail::ValueCorner3(point, 5u, key);
    const float v011 = detail::ValueCorner3(point, 6u, key);
    const float v111 = detail::ValueCorner3(point, 7u, key);
    const float u = detail::PerlinFade(point.fx);
    const float v = detail::PerlinFade(point.fy);
    const float w = detail::PerlinFade(point.fz);
    const float near = detail::Lerp(v, detail::Lerp(u, v000, v100), detail::Lerp(u, v010, v110));
    const float far = detail::Lerp(v, detail::Lerp(u, v001, v101), detail::Lerp(u, v011, v111));
    return detail::Lerp(w, near, far);
}

}  // namespace terraingen
