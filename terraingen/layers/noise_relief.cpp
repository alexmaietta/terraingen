#include "terraingen/layers/noise_relief.h"

#include "terraingen/noise/kernels/layers.h"
#include "terraingen/util/log.h"

#include <cmath>
#include <utility>

namespace terraingen {

NoiseRelief::NoiseRelief(SyntheticTerrainParams params, Vec2d origin)
    : params_(std::move(params)), origin_(origin) {
    ValidateSyntheticTerrain(params_);
}

void NoiseRelief::Apply(Raster<float>& heights, double /*spacing*/,
                        TerrainFeatures& /*features*/) const {
    const Stopwatch stopwatch;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            if (heights.IsNoData(cell)) {
                continue;
            }
            const Vec2d centre = heights.CellCenter(col, row);
            cell += EvaluateNoiseLayers(static_cast<float>(centre.x - origin_.x),
                                        static_cast<float>(centre.y - origin_.y),
                                        params_.layers.data(), params_.layers.size());
        }
    }
    LogInfo("layers", "Added ", params_.layers.size(),
            params_.layers.size() == 1 ? " noise layer" : " noise layers", " to ", heights.Width(),
            "x", heights.Height(), " cells in ", FormatDuration(stopwatch.Milliseconds()));
}

float NoiseRelief::ApplyAt(float height, double x, double y, double /*spacing*/) const {
    if (std::isnan(height)) {
        return height;
    }
    return height + EvaluateNoiseLayers(static_cast<float>(x - origin_.x),
                                        static_cast<float>(y - origin_.y), params_.layers.data(),
                                        params_.layers.size());
}

}  // namespace terraingen
