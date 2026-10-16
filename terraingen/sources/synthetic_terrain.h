/// @file
/// @brief Fully synthetic terrains made from stacks of noise layers.
#pragma once

#include "terraingen/noise/kernels/fractal.h"
#include "terraingen/noise/kernels/layers.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/raster.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// @brief A synthetic terrain: a stack of noise layers evaluated with `EvaluateNoiseLayers`.
struct SyntheticTerrainParams {
    std::vector<NoiseLayer> layers{NoiseLayer{}};  ///< 1 to `kMaxNoiseLayers`; one Perlin fBm.

    bool operator==(const SyntheticTerrainParams&) const = default;
};

/// @brief Checks the layer count and every layer (`ValidateNoiseLayer`).
/// @param params The terrain to check.
/// @throws std::invalid_argument naming the first problem.
void ValidateSyntheticTerrain(const SyntheticTerrainParams& params);

/// @brief Sets every cell of `raster` to the synthetic height at its centre, placed by its
/// `GeoTransform`, so tiles of a larger grid on the same lattice match it.
/// @param raster Raster to fill, heights in metres.
/// @param params The terrain.
/// @throws std::invalid_argument if `params` are invalid.
void FillSyntheticTerrain(Raster<float>& raster, const SyntheticTerrainParams& params);

/// @brief Generates a north-up synthetic terrain centred on the world origin.
/// @param width Columns, at least 1.
/// @param height Rows, at least 1.
/// @param pixelSize Cell size in world units (metres), positive and finite.
/// @param params The terrain.
/// @return The heights in metres.
/// @throws std::invalid_argument if `width` or `height` is zero, `pixelSize` is not positive and
/// finite, or `params` are invalid.
Raster<float> GenerateSyntheticTerrain(std::size_t width, std::size_t height, double pixelSize,
                                       const SyntheticTerrainParams& params);

}  // namespace terraingen
