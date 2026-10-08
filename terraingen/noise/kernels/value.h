#pragma once

#include "terraingen/noise/kernels/hash.h"
#include "terraingen/noise/kernels/perlin.h"

namespace terraingen {

namespace detail {

/// The value in [-1, 1) at lattice point (`x`, `y`, `z`) of value noise.
inline float LatticeValue(uint32_t x, uint32_t y, uint32_t z) {
    const UVec3 point = {x, y, z};
    return 2.0f * HashToUnitFloat(Pcg3dHash(point).y) - 1.0f;
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
    const float v00 = detail::LatticeValue(x0, y0, seed);
    const float v10 = detail::LatticeValue(x0 + 1u, y0, seed);
    const float v01 = detail::LatticeValue(x0, y0 + 1u, seed);
    const float v11 = detail::LatticeValue(x0 + 1u, y0 + 1u, seed);
    const float u = detail::PerlinFade(fx);
    const float v = detail::PerlinFade(fy);
    return detail::Lerp(v, detail::Lerp(u, v00, v10), detail::Lerp(u, v01, v11));
}

/// 2D value noise at (`x`, `y`) in lattice units; see the cell-and-offset overload.
///
/// @return Noise in [-1, 1].
inline float ValueNoise2(float x, float y, uint32_t seed) {
    const float cellX = floor(x);
    const float cellY = floor(y);
    return ValueNoise2(int32_t(cellX), int32_t(cellY), x - cellX, y - cellY, seed);
}

/// 3D value noise at lattice cell (`ix`, `iy`, `iz`) plus fractional offset (`fx`, `fy`, `fz`) in
/// [0, 1), as the 2D overload, blended trilinearly with the quintic fade.
///
/// @return Noise in [-1, 1].
inline float ValueNoise3(int32_t ix, int32_t iy, int32_t iz, float fx, float fy, float fz,
                         uint32_t seed) {
    const uint32_t x0 = uint32_t(ix);
    const uint32_t y0 = uint32_t(iy);
    const uint32_t z0 = uint32_t(iz);
    const uint32_t key = PcgHash(seed);
    const float v000 = detail::LatticeValue(x0, y0, z0 ^ key);
    const float v100 = detail::LatticeValue(x0 + 1u, y0, z0 ^ key);
    const float v010 = detail::LatticeValue(x0, y0 + 1u, z0 ^ key);
    const float v110 = detail::LatticeValue(x0 + 1u, y0 + 1u, z0 ^ key);
    const float v001 = detail::LatticeValue(x0, y0, (z0 + 1u) ^ key);
    const float v101 = detail::LatticeValue(x0 + 1u, y0, (z0 + 1u) ^ key);
    const float v011 = detail::LatticeValue(x0, y0 + 1u, (z0 + 1u) ^ key);
    const float v111 = detail::LatticeValue(x0 + 1u, y0 + 1u, (z0 + 1u) ^ key);
    const float u = detail::PerlinFade(fx);
    const float v = detail::PerlinFade(fy);
    const float w = detail::PerlinFade(fz);
    const float near = detail::Lerp(v, detail::Lerp(u, v000, v100), detail::Lerp(u, v010, v110));
    const float far = detail::Lerp(v, detail::Lerp(u, v001, v101), detail::Lerp(u, v011, v111));
    return detail::Lerp(w, near, far);
}

}  // namespace terraingen
