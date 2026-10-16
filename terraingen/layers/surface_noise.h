/// @file
/// @brief Noise read at points of a `Surface`: 2D noise on a plane, 3D noise on a sphere.
#pragma once

#include "terraingen/noise/kernels/layers.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// @brief Two-octave patch noise: clamp(2.5 (0.7 n(p / size) + 0.3 n(4 p / size)), -1, 1).
///
/// n is 2D Perlin noise on a plane and 3D Perlin noise at the 3D point on a sphere; the finer
/// octave is seeded `seed` + 1. About half the ground lies near either end.
/// @param surface What the point lies on.
/// @param x Surface x of the point.
/// @param y Surface y of the point.
/// @param size Typical width of a patch, in metres; positive.
/// @param seed Seed of the coarser octave.
/// @return The patch value, in [-1, 1].
double PatchNoise(const Surface& surface, double x, double y, double size, std::uint32_t seed);

/// @brief A stack of noise layers at a point: `EvaluateNoiseLayers` relative to `origin` on a
/// plane (keeping single precision accurate), `EvaluateNoiseLayers3` at the 3D point on a sphere.
/// @param layers The noise layers; on a sphere each needs a basis with a 3D form.
/// @param origin Where plane coordinates are measured from; unused on a sphere.
/// @param surface What the point lies on.
/// @param x Surface x of the point.
/// @param y Surface y of the point.
/// @return The summed noise.
float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y);

}  // namespace terraingen
