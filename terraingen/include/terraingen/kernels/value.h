#pragma once

#include "terraingen/kernels/hash.h"
#include "terraingen/kernels/perlin.h"

#include <cmath>
#include <cstdint>

namespace terraingen {

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
inline float ValueNoise2(std::int32_t ix, std::int32_t iy, float fx, float fy, std::uint32_t seed) {
    const auto x0 = static_cast<std::uint32_t>(ix);
    const auto y0 = static_cast<std::uint32_t>(iy);
    const auto value = [seed](std::uint32_t x, std::uint32_t y) {
        return 2.0f * HashToUnitFloat(Pcg3dHash(UVec3{x, y, seed}).y) - 1.0f;
    };
    const float u = detail::PerlinFade(fx);
    const float v = detail::PerlinFade(fy);
    return detail::Lerp(v, detail::Lerp(u, value(x0, y0), value(x0 + 1u, y0)),
                        detail::Lerp(u, value(x0, y0 + 1u), value(x0 + 1u, y0 + 1u)));
}

/// 2D value noise at (`x`, `y`) in lattice units; see the cell-and-offset overload.
///
/// @return Noise in [-1, 1].
inline float ValueNoise2(float x, float y, std::uint32_t seed) {
    const float cellX = std::floor(x);
    const float cellY = std::floor(y);
    return ValueNoise2(static_cast<std::int32_t>(cellX), static_cast<std::int32_t>(cellY),
                       x - cellX, y - cellY, seed);
}

}  // namespace terraingen
