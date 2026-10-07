#include "terraingen/layers/noise_relief.h"

#include "terraingen/noise/kernels/layers.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

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
    if (surface_.IsSphere()) {
        const Vec3d p = surface_.Position({x, y});
        return EvaluateNoiseLayers3(p.x, p.y, p.z, params_.layers.data(), params_.layers.size());
    }
    return EvaluateNoiseLayers(static_cast<float>(x - origin_.x), static_cast<float>(y - origin_.y),
                               params_.layers.data(), params_.layers.size());
}

void NoiseRelief::Apply(Raster<float>& heights, const LayerContext& context,
                        TerrainFeatures& /*features*/) const {
    const Stopwatch stopwatch;
    ParallelFor(heights.Height(), context.threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            if (heights.IsNoData(cell)) {
                continue;
            }
            const Vec2d centre = heights.CellCenter(col, row);
            cell += NoiseAt(centre.x, centre.y);
        }
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
