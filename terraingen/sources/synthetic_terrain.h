/// @file
/// @brief Fully synthetic terrains made from stacks of noise layers.
#pragma once

#include "terraingen/noise/fractal.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// The summed noise of `layers` at surface (x, y): 2D relative to `origin` on a plane (keeping
/// single precision accurate), 3D at the point's 3D position on a sphere; 0 for no layers.
/// @param origin Where plane coordinates are measured from; unused on a sphere.
float NoiseLayersAt(const std::vector<NoiseLayer>& layers, const Vec2d& origin,
                    const Surface& surface, double x, double y);

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

/// Sets every cell of `raster` to the synthetic height in meters at its center, placed by its
/// `GeoTransform`, so tiles of a larger grid on the same lattice match it.
/// @throws std::invalid_argument as `ValidateSyntheticTerrain`.
void FillSyntheticTerrain(Raster<float>& raster, const SyntheticTerrainParams& params);

/// Generates the heights in meters of a north-up synthetic terrain centered on the world origin.
/// @param width Columns, at least 1.
/// @param height Rows, at least 1.
/// @param pixelSize Cell size in world units (meters), positive and finite.
/// @throws std::invalid_argument if `width` or `height` is zero, `pixelSize` is not positive and
/// finite, or `params` are invalid.
Raster<float> GenerateSyntheticTerrain(std::size_t width, std::size_t height, double pixelSize,
                                       const SyntheticTerrainParams& params);

}  // namespace terraingen
