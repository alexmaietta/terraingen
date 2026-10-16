#pragma once

#include "terraingen/noise/kernels/fractal.h"
#include "terraingen/noise/kernels/layers.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/raster.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// A synthetic terrain: a stack of noise layers, from 1 to `kMaxNoiseLayers`, evaluated with
/// `EvaluateNoiseLayers`. The default is a single Perlin fBm layer.
struct SyntheticTerrainParams {
    std::vector<NoiseLayer> layers{NoiseLayer{}};

    bool operator==(const SyntheticTerrainParams&) const = default;
};

/// Checks the layer count and every layer (`ValidateNoiseLayer`).
/// @throws std::invalid_argument naming the first problem.
void ValidateSyntheticTerrain(const SyntheticTerrainParams& params);

/// Sets every cell of `raster` to the synthetic terrain height at the cell's centre, in metres.
///
/// Cell centres come from the raster's `GeoTransform`, so rasters that tile a larger grid on the
/// same lattice produce the same values as the larger grid.
/// @throws std::invalid_argument if `params` are invalid.
void FillSyntheticTerrain(Raster<float>& raster, const SyntheticTerrainParams& params);

/// Generates a `width` x `height` north-up synthetic terrain centred on the world origin, with
/// square pixels of `pixelSize` world units (metres).
/// @throws std::invalid_argument if `width` or `height` is zero, `pixelSize` is not positive and
/// finite, or `params` are invalid.
Raster<float> GenerateSyntheticTerrain(std::size_t width, std::size_t height, double pixelSize,
                                       const SyntheticTerrainParams& params);

}  // namespace terraingen
