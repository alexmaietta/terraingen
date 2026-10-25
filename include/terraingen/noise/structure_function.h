// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Structure functions of Perlin noise: the mean squared difference of values a lag apart.
#pragma once

namespace terraingen {

/// The structure function of `PerlinNoise2`: mean squared difference of values a lag `r` apart
/// (lattice units), over positions, directions and seeds; 0 for `r` <= 0.
double PerlinStructureFunction(double r);

/// The structure function of `PerlinNoise3` at a lag `r` (lattice units) in a random 3D
/// direction; 0 for `r` <= 0.
double Perlin3StructureFunction(double r);

}  // namespace terraingen
