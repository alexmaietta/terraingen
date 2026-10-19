/// @file
/// @brief Fully synthetic terrains made from stacks of noise layers.
#pragma once

#include "terraingen/noise/fractal.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/raster.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// A synthetic terrain: a stack of noise layers evaluated with `EvaluateNoiseLayers`.
struct SyntheticTerrainParams {
    std::vector<NoiseLayer> layers{NoiseLayer{}};  ///< 1 to `kMaxNoiseLayers`; one Perlin fBm.

    /// Memberwise equality.
    bool operator==(const SyntheticTerrainParams& other) const {
        return layers == other.layers;
    }
    bool operator!=(const SyntheticTerrainParams& other) const {
        return !(*this == other);
    }
};

/// Checks the layer count and every layer (`ValidateNoiseLayer`).
/// @throws std::invalid_argument naming the first problem.
void ValidateSyntheticTerrain(const SyntheticTerrainParams& params);

/// Sets every cell of `raster` to the synthetic height in metres at its centre, placed by its
/// `GeoTransform`, so tiles of a larger grid on the same lattice match it.
/// @throws std::invalid_argument as `ValidateSyntheticTerrain`.
void FillSyntheticTerrain(Raster<float>& raster, const SyntheticTerrainParams& params);

/// Generates the heights in metres of a north-up synthetic terrain centred on the world origin.
/// @param width Columns, at least 1.
/// @param height Rows, at least 1.
/// @param pixelSize Cell size in world units (metres), positive and finite.
/// @throws std::invalid_argument if `width` or `height` is zero, `pixelSize` is not positive and
/// finite, or `params` are invalid.
Raster<float> GenerateSyntheticTerrain(std::size_t width, std::size_t height, double pixelSize,
                                       const SyntheticTerrainParams& params);

}  // namespace terraingen
