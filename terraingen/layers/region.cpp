/// @file
/// @brief Region shapes and `RegionLayer`: confining a layer to an area with a feathered edge.
#include "terraingen/layers/region.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// `height` moved towards `inner` by `weight` in [0, 1].
float Blend(float height, float inner, double weight) {
    if (weight >= 1.0) {
        return inner;
    }
    return static_cast<float>(static_cast<double>(height) +
                              weight * (static_cast<double>(inner) - static_cast<double>(height)));
}

/// The smallest window holding every one of `windows`; empty when there is none.
PixelWindow UnionOf(const std::vector<PixelWindow>& windows) {
    if (windows.empty()) {
        return {};
    }
    PixelWindow joined = windows.front();
    for (const PixelWindow& window : windows) {
        joined.colBegin = std::min(joined.colBegin, window.colBegin);
        joined.colEnd = std::max(joined.colEnd, window.colEnd);
        joined.rowBegin = std::min(joined.rowBegin, window.rowBegin);
        joined.rowEnd = std::max(joined.rowEnd, window.rowEnd);
    }
    return joined;
}

/// The cells of `window` of `heights` as a raster of their own, on the same lattice.
Raster<float> CopyWindow(const Raster<float>& heights, const PixelWindow& window) {
    Raster<float> copy(window.colEnd - window.colBegin, window.rowEnd - window.rowBegin);
    GeoTransform transform = heights.Transform();
    const Vec2d origin = transform.PixelToWorld(static_cast<double>(window.colBegin),
                                                static_cast<double>(window.rowBegin));
    transform.originX = origin.x;
    transform.originY = origin.y;
    copy.SetTransform(transform);
    copy.SetNoData(heights.NoData());
    for (std::size_t row = window.rowBegin; row < window.rowEnd; ++row) {
        for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
            copy(col - window.colBegin, row - window.rowBegin) = heights(col, row);
        }
    }
    return copy;
}

}  // namespace

double RegionShape::DistanceOutside(double x, double y, const Surface& surface) const {
    if (surface.IsSphere()) {
        if (kind == Kind::Circle) {
            return std::fmax(0.0, surface.Distance(centre, {x, y}) - radius);
        }
        double best = std::numeric_limits<double>::infinity();
        for (const double shift : {-360.0, 0.0, 360.0}) {
            const double lon = x + shift;
            const double dx = std::fmax(0.0, std::fmax(rectangle.minX - lon, lon - rectangle.maxX));
            const double dy = std::fmax(0.0, std::fmax(rectangle.minY - y, y - rectangle.maxY));
            const double cosLat = std::cos(y * std::numbers::pi / 180.0);
            best = std::fmin(best, surface.UnitLength() * std::hypot(dx * cosLat, dy));
        }
        return best;
    }
    if (kind == Kind::Circle) {
        return std::fmax(0.0, std::hypot(x - centre.x, y - centre.y) - radius);
    }
    const double dx = std::fmax(0.0, std::fmax(rectangle.minX - x, x - rectangle.maxX));
    const double dy = std::fmax(0.0, std::fmax(rectangle.minY - y, y - rectangle.maxY));
    return std::hypot(dx, dy);
}

Bounds2d RegionShape::Bounds(const Surface& surface) const {
    if (kind == Kind::Circle) {
        if (surface.IsSphere()) {
            return surface.Expand({centre.x, centre.y, centre.x, centre.y}, radius);
        }
        return {centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius};
    }
    return rectangle;
}

void ValidateRegionShape(const RegionShape& shape) {
    if (shape.kind == RegionShape::Kind::Circle) {
        if (!(std::isfinite(shape.centre.x) && std::isfinite(shape.centre.y) &&
              IsNonNegative(shape.radius))) {
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
                         BlendShape blend, Surface surface)
    : inner_(std::move(inner)), shape_(shape), feather_(feather), blend_(blend), surface_(surface) {
    if (!inner_) {
        throw std::invalid_argument("A region layer needs a layer to apply");
    }
    ValidateRegionShape(shape_);
    RequireNonNegative(feather_, "Region feather");
}

double RegionLayer::Weight(double x, double y) const {
    const double distance = shape_.DistanceOutside(x, y, surface_);
    if (distance <= 0.0) {
        return 1.0;
    }
    if (distance >= feather_) {
        return 0.0;
    }
    return Falloff(distance, 0.0, feather_, blend_);
}

void RegionLayer::Apply(Raster<float>& heights, const LayerContext& context,
                        TerrainFeatures& features) const {
    const Bounds2d reach = surface_.Expand(shape_.Bounds(surface_), feather_);
    const PixelWindow window = UnionOf(WindowsAround(heights, reach, surface_));
    if (window.Empty()) {
        return;
    }
    Raster<float> inner = CopyWindow(heights, window);
    TerrainFeatures placed;
    inner_->Apply(inner, context, placed);
    ParallelFor(window.rowEnd - window.rowBegin, context.threads, [&](std::size_t r) {
        const std::size_t row = window.rowBegin + r;
        for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
            float& cell = heights(col, row);
            if (heights.IsNoData(cell)) {
                continue;
            }
            const Vec2d centre = heights.CellCenter(col, row);
            const double weight = Weight(centre.x, centre.y);
            if (weight > 0.0) {
                cell = Blend(cell, inner(col - window.colBegin, r), weight);
            }
        }
    });
    KeepFeaturesInside(placed, features);
}

void RegionLayer::KeepFeaturesInside(const TerrainFeatures& placed,
                                     TerrainFeatures& features) const {
    for (std::size_t n = 0; n < placed.craters.Size(); ++n) {
        const Crater crater = placed.craters.At(n);
        if (shape_.DistanceOutside(crater.x, crater.y, surface_) <= feather_) {
            features.craters.Append(crater);
            if (n < placed.craterAttributes.size()) {
                features.craterAttributes.push_back(placed.craterAttributes[n]);
            }
        }
    }
    for (const Boulder& boulder : placed.boulders) {
        if (shape_.DistanceOutside(boulder.x, boulder.y, surface_) <= feather_) {
            features.boulders.push_back(boulder);
        }
    }
    for (const BoulderModel& model : placed.boulderModels) {
        if (shape_.DistanceOutside(model.boulder.x, model.boulder.y, surface_) <= feather_) {
            features.boulderModels.push_back(model);
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
