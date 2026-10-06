#include "terraingen/layers/region.h"

#include "terraingen/raster/grid.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

float Blend(float height, float inner, double weight) {
    if (weight >= 1.0) {
        return inner;
    }
    return static_cast<float>(static_cast<double>(height) +
                              weight * (static_cast<double>(inner) - static_cast<double>(height)));
}

}  // namespace

double RegionShape::DistanceOutside(double x, double y) const {
    if (kind == Kind::Circle) {
        return std::fmax(0.0, std::hypot(x - centre.x, y - centre.y) - radius);
    }
    const double dx = std::fmax(0.0, std::fmax(rectangle.minX - x, x - rectangle.maxX));
    const double dy = std::fmax(0.0, std::fmax(rectangle.minY - y, y - rectangle.maxY));
    return std::hypot(dx, dy);
}

Bounds2d RegionShape::Bounds() const {
    if (kind == Kind::Circle) {
        return {centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius};
    }
    return rectangle;
}

void ValidateRegionShape(const RegionShape& shape) {
    if (shape.kind == RegionShape::Kind::Circle) {
        if (!(std::isfinite(shape.centre.x) && std::isfinite(shape.centre.y) &&
              std::isfinite(shape.radius) && shape.radius >= 0.0)) {
            throw std::invalid_argument("A circular region needs a finite centre and radius >= 0");
        }
        return;
    }
    const Bounds2d& r = shape.rectangle;
    if (!(std::isfinite(r.minX) && std::isfinite(r.minY) && std::isfinite(r.maxX) &&
          std::isfinite(r.maxY) && r.minX <= r.maxX && r.minY <= r.maxY)) {
        throw std::invalid_argument("A rectangular region needs finite bounds with min <= max");
    }
}

RegionLayer::RegionLayer(std::shared_ptr<const Layer> inner, RegionShape shape, double feather,
                         BlendShape blend)
    : inner_(std::move(inner)), shape_(shape), feather_(feather), blend_(blend) {
    if (!inner_) {
        throw std::invalid_argument("A region layer needs a layer to apply");
    }
    ValidateRegionShape(shape_);
    if (!(std::isfinite(feather_) && feather_ >= 0.0)) {
        throw std::invalid_argument("Region feather must be finite and not negative");
    }
}

double RegionLayer::Weight(double x, double y) const {
    const double distance = shape_.DistanceOutside(x, y);
    if (distance <= 0.0) {
        return 1.0;
    }
    if (distance >= feather_) {
        return 0.0;
    }
    return CraterBlendWeight(distance, CraterBlend{0.0, feather_, blend_});
}

void RegionLayer::Apply(Raster<float>& heights, const LayerContext& context,
                        TerrainFeatures& features) const {
    const Bounds2d reach = shape_.Bounds().Expanded(feather_);
    const GeoTransform& transform = heights.Transform();
    double minCol = std::numeric_limits<double>::infinity();
    double minRow = minCol;
    double maxCol = -minCol;
    double maxRow = -minCol;
    for (const auto& [x, y] : std::array<Vec2d, 4>{{{reach.minX, reach.minY},
                                                    {reach.minX, reach.maxY},
                                                    {reach.maxX, reach.minY},
                                                    {reach.maxX, reach.maxY}}}) {
        const Vec2d pixel = transform.WorldToPixel(x, y);
        minCol = std::fmin(minCol, pixel.x);
        maxCol = std::fmax(maxCol, pixel.x);
        minRow = std::fmin(minRow, pixel.y);
        maxRow = std::fmax(maxRow, pixel.y);
    }
    const auto width = static_cast<double>(heights.Width());
    const auto height = static_cast<double>(heights.Height());
    const auto col0 = static_cast<std::size_t>(std::clamp(std::floor(minCol), 0.0, width));
    const auto col1 = static_cast<std::size_t>(std::clamp(std::ceil(maxCol), 0.0, width));
    const auto row0 = static_cast<std::size_t>(std::clamp(std::floor(minRow), 0.0, height));
    const auto row1 = static_cast<std::size_t>(std::clamp(std::ceil(maxRow), 0.0, height));
    if (col0 >= col1 || row0 >= row1) {
        return;
    }

    Raster<float> window(col1 - col0, row1 - row0);
    GeoTransform windowTransform = transform;
    const Vec2d origin =
        transform.PixelToWorld(static_cast<double>(col0), static_cast<double>(row0));
    windowTransform.originX = origin.x;
    windowTransform.originY = origin.y;
    window.SetTransform(windowTransform);
    window.SetNoData(heights.NoData());
    for (std::size_t row = row0; row < row1; ++row) {
        for (std::size_t col = col0; col < col1; ++col) {
            window(col - col0, row - row0) = heights(col, row);
        }
    }
    TerrainFeatures placed;
    inner_->Apply(window, context, placed);
    ParallelFor(row1 - row0, context.threads, [&](std::size_t r) {
        const std::size_t row = row0 + r;
        for (std::size_t col = col0; col < col1; ++col) {
            float& cell = heights(col, row);
            if (heights.IsNoData(cell)) {
                continue;
            }
            const Vec2d centre = heights.CellCenter(col, row);
            const double weight = Weight(centre.x, centre.y);
            if (weight > 0.0) {
                cell = Blend(cell, window(col - col0, r), weight);
            }
        }
    });

    for (std::size_t n = 0; n < placed.craters.Size(); ++n) {
        const Crater crater = placed.craters.At(n);
        if (shape_.DistanceOutside(crater.x, crater.y) <= feather_) {
            features.craters.Append(crater);
            if (n < placed.craterAttributes.size()) {
                features.craterAttributes.push_back(placed.craterAttributes[n]);
            }
        }
    }
    for (const Rock& rock : placed.rocks) {
        if (shape_.DistanceOutside(rock.x, rock.y) <= feather_) {
            features.rocks.push_back(rock);
        }
    }
}

float RegionLayer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    const double weight = Weight(x, y);
    if (weight <= 0.0 || std::isnan(height)) {
        return height;
    }
    return Blend(height, inner_->ApplyAt(height, x, y, context), weight);
}

}  // namespace terraingen
