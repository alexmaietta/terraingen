#include "terraingen/layers/transforms.h"

#include "terraingen/raster/grid.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Sets each cell of `heights` holding data to `map(height, centre)`, rows over up to `threads`
/// threads.
template <typename Map> void MapCells(Raster<float>& heights, unsigned threads, const Map& map) {
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            float& cell = heights(col, row);
            if (!heights.IsNoData(cell)) {
                cell = map(cell, heights.CellCenter(col, row));
            }
        }
    });
}

float Scaled(float height, double scale, double offset) {
    return static_cast<float>(static_cast<double>(height) * scale + offset);
}

float Clamped(float height, const std::optional<double>& min, const std::optional<double>& max) {
    double value = height;
    if (min) {
        value = std::max(value, *min);
    }
    if (max) {
        value = std::min(value, *max);
    }
    return static_cast<float>(value);
}

void CheckSpacing(double spacing) {
    if (!(std::isfinite(spacing) && spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
}

}  // namespace

ScaleLayer::ScaleLayer(double scale, double offset) : scale_(scale), offset_(offset) {
    if (!(std::isfinite(scale) && std::isfinite(offset))) {
        throw std::invalid_argument("Scale and offset must be finite");
    }
}

void ScaleLayer::Apply(Raster<float>& heights, const LayerContext& context,
                       TerrainFeatures& /*features*/) const {
    MapCells(heights, context.threads,
             [&](float height, const Vec2d&) { return Scaled(height, scale_, offset_); });
}

float ScaleLayer::ApplyAt(float height, double /*x*/, double /*y*/,
                          const LayerContext& /*context*/) const {
    return std::isnan(height) ? height : Scaled(height, scale_, offset_);
}

ClampLayer::ClampLayer(std::optional<double> min, std::optional<double> max)
    : min_(min), max_(max) {
    if ((min && !std::isfinite(*min)) || (max && !std::isfinite(*max))) {
        throw std::invalid_argument("Clamp bounds must be finite");
    }
    if (min && max && *min > *max) {
        throw std::invalid_argument("Clamp min must not exceed max");
    }
}

void ClampLayer::Apply(Raster<float>& heights, const LayerContext& context,
                       TerrainFeatures& /*features*/) const {
    MapCells(heights, context.threads,
             [&](float height, const Vec2d&) { return Clamped(height, min_, max_); });
}

float ClampLayer::ApplyAt(float height, double /*x*/, double /*y*/,
                          const LayerContext& /*context*/) const {
    return std::isnan(height) ? height : Clamped(height, min_, max_);
}

FunctionLayer::FunctionLayer(HeightFunction function) : function_(std::move(function)) {
    if (!function_) {
        throw std::invalid_argument("A function layer needs a function");
    }
}

void FunctionLayer::Apply(Raster<float>& heights, const LayerContext& context,
                          TerrainFeatures& /*features*/) const {
    const double spacing = std::max(context.spacing, RasterGrid::Of(heights).PixelSize());
    MapCells(heights, context.threads, [&](float height, const Vec2d& centre) {
        return static_cast<float>(function_(centre.x, centre.y, spacing, height));
    });
}

float FunctionLayer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    CheckSpacing(context.spacing);
    if (std::isnan(height)) {
        return height;
    }
    return static_cast<float>(function_(x, y, context.spacing, height));
}

}  // namespace terraingen
