/// @file
/// @brief `NoiseRelief`: noise layers added over grids and at points.
#include "terraingen/layers/noise_relief.h"

#include "terraingen/layers/surface_noise.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/log.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

NoiseRelief::NoiseRelief(SyntheticTerrainParams params, Vec2d origin, Surface surface)
    : params_(std::move(params)), origin_(origin), surface_(surface) {
    ValidateSyntheticTerrain(params_);
    if (surface_.IsSphere()) {
        for (const NoiseLayer& layer : params_.layers) {
            if (!HasBasisNoise3(layer.fractal.basis)) {
                throw std::invalid_argument(
                    "Noise on a sphere needs a basis with a 3D form: perlin or value");
            }
        }
    }
}

float NoiseRelief::NoiseAt(double x, double y) const {
    return NoiseLayersAt(params_.layers, origin_, surface_, x, y);
}

void NoiseRelief::Apply(Raster<float>& heights, const LayerContext& context,
                        TerrainFeatures& /*features*/) const {
    const Stopwatch stopwatch;
    MapCells(heights, context.threads, [&](float height, const Vec2d& centre) {
        return height + NoiseAt(centre.x, centre.y);
    });
    LogInfo("layers", "Added ", params_.layers.size(),
            params_.layers.size() == 1 ? " noise layer" : " noise layers", " to ", heights.Width(),
            "x", heights.Height(), " cells in ", FormatDuration(stopwatch.Milliseconds()));
}

float NoiseRelief::ApplyAt(float height, double x, double y,
                           const LayerContext& /*context*/) const {
    if (std::isnan(height)) {
        return height;
    }
    return height + NoiseAt(x, y);
}

}  // namespace terraingen
