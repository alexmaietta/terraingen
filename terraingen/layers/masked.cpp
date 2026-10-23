// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/masked.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// `height` moved toward `inner` by `weight` in [0, 1].
float Blend(float height, float inner, double weight) {
    if (weight >= 1.0) {
        return inner;
    }
    return static_cast<float>(static_cast<double>(height) +
                              weight * (static_cast<double>(inner) - static_cast<double>(height)));
}

/// The smallest window holding every one of `windows`; empty when there is none.
PixelWindow MergeWindows(const std::vector<PixelWindow>& windows) {
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
    Raster<float> copy = Subgrid(RasterGrid::FromRaster(heights), window).MakeRaster();
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
    const auto outside = [&](double px, double cosLat) {
        const double dx = std::fmax(0.0, std::fmax(rectangle.minX - px, px - rectangle.maxX));
        const double dy = std::fmax(0.0, std::fmax(rectangle.minY - y, y - rectangle.maxY));
        return std::hypot(dx * cosLat, dy);
    };
    if (surface.IsBody()) {
        if (kind == Kind::Circle) {
            return std::fmax(0.0, surface.Distance(center, {x, y}) - radius);
        }
        const double cosLat = std::cos(y * kDegree);
        double best = std::numeric_limits<double>::infinity();
        for (const double shift : {-360.0, 0.0, 360.0}) {
            best = std::fmin(best, surface.UnitLength() * outside(x + shift, cosLat));
        }
        return best;
    }
    if (kind == Kind::Circle) {
        return std::fmax(0.0, std::hypot(x - center.x, y - center.y) - radius);
    }
    return outside(x, 1.0);
}

Bounds2d RegionShape::Bounds(const Surface& surface) const {
    if (kind == Kind::Circle) {
        return surface.Expand({center.x, center.y, center.x, center.y}, radius);
    }
    return rectangle;
}

void ValidateRegionShape(const RegionShape& shape) {
    if (shape.kind == RegionShape::Kind::Circle) {
        if (!(std::isfinite(shape.center.x) && std::isfinite(shape.center.y) &&
              IsNonNegative(shape.radius))) {
            throw std::invalid_argument("A circular region needs a finite center and radius >= 0");
        }
        return;
    }
    const Bounds2d& r = shape.rectangle;
    if (!(r.IsFinite() && r.minX <= r.maxX && r.minY <= r.maxY)) {
        throw std::invalid_argument("A rectangular region needs finite bounds with min <= max");
    }
}

MaskedLayer::MaskedLayer(std::shared_ptr<const Layer> inner, RegionShape shape, double feather,
                         BlendShape blend, Surface surface)
    : inner_(std::move(inner)), shape_(shape), feather_(feather), blend_(blend), surface_(surface) {
    if (!inner_) {
        throw std::invalid_argument("A masked layer needs a layer to apply");
    }
    ValidateRegionShape(shape_);
    RequireNonNegative(feather_, "Masked layer feather");
}

double MaskedLayer::Weight(double x, double y) const {
    const double distance = shape_.DistanceOutside(x, y, surface_);
    if (distance <= 0.0) {
        return 1.0;
    }
    if (distance >= feather_) {
        return 0.0;
    }
    return Falloff(distance, 0.0, feather_, blend_);
}

bool MaskedLayer::Holds(double x, double y) const {
    return shape_.DistanceOutside(x, y, surface_) <= feather_;
}

PixelWindow MaskedLayer::InnerWindow(const RasterGrid& grid) const {
    const Bounds2d reach = surface_.Expand(shape_.Bounds(surface_), feather_);
    return MergeWindows(PixelWindowsIn(grid, reach, surface_));
}

std::optional<RasterGrid> MaskedLayer::InnerGrid(const RasterGrid& grid) const {
    const PixelWindow window = InnerWindow(grid);
    if (window.Empty()) {
        return std::nullopt;
    }
    return Subgrid(grid, window);
}

CraterFeatures MaskedLayer::CratersOver(const RasterGrid& grid, double spacing) const {
    const auto* placer = dynamic_cast<const CraterPlacer*>(inner_.get());
    const std::optional<RasterGrid> inner = InnerGrid(grid);
    CraterFeatures held;
    if (placer == nullptr || !inner) {
        return held;
    }
    const CraterFeatures all = placer->CratersOver(*inner, spacing);
    for (std::size_t n = 0; n < all.craters.Size(); ++n) {
        const Crater crater = all.craters.At(n);
        if (Holds(crater.x, crater.y)) {
            held.craters.Append(crater);
            held.attributes.push_back(all.attributes[n]);
        }
    }
    return held;
}

BoulderFeatures MaskedLayer::BouldersOver(const RasterGrid& grid, double spacing,
                                          const Ground* ground) const {
    const auto* placer = dynamic_cast<const BoulderPlacer*>(inner_.get());
    const std::optional<RasterGrid> inner = InnerGrid(grid);
    BoulderFeatures held;
    if (placer == nullptr || !inner) {
        return held;
    }
    const BoulderFeatures all = placer->BouldersOver(*inner, spacing, ground);
    std::copy_if(all.boulders.begin(), all.boulders.end(), std::back_inserter(held.boulders),
                 [&](const Boulder& b) { return Holds(b.x, b.y); });
    std::copy_if(all.models.begin(), all.models.end(), std::back_inserter(held.models),
                 [&](const BoulderModel& m) { return Holds(m.boulder.x, m.boulder.y); });
    return held;
}

void MaskedLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    if (Raster<float>* grid = samples.Grid()) {
        ApplyToGrid(*grid, context);
    } else {
        ApplyToPoints(samples, context);
    }
}

void MaskedLayer::ApplyToPoints(SampleBlock& samples, const LayerContext& context) const {
    std::vector<std::size_t> indices;
    std::vector<Vec2d> positions;
    std::vector<float> heights;
    std::vector<double> weights;
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        const Vec2d p = samples.Position(i);
        const double weight = samples.IsNoData(i) ? 0.0 : Weight(p.x, p.y);
        if (weight > 0.0) {
            indices.push_back(i);
            positions.push_back(p);
            heights.push_back(samples.Height(i));
            weights.push_back(weight);
        }
    }
    SampleBlock inside(positions, heights);
    inner_->Apply(inside, context);
    for (std::size_t k = 0; k < indices.size(); ++k) {
        float& height = samples.Height(indices[k]);
        height = Blend(height, heights[k], weights[k]);
    }
}

void MaskedLayer::ApplyToGrid(Raster<float>& heights, const LayerContext& context) const {
    const PixelWindow window = InnerWindow(RasterGrid::FromRaster(heights));
    if (window.Empty()) {
        return;
    }
    Raster<float> inner = CopyWindow(heights, window);
    inner_->Apply(inner, context);
    ParallelFor(window.rowEnd - window.rowBegin, context.threads, [&](std::size_t r) {
        const std::size_t row = window.rowBegin + r;
        for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
            float& cell = heights(col, row);
            if (heights.IsNoData(cell)) {
                continue;
            }
            const Vec2d center = heights.CellCenter(col, row);
            const double weight = Weight(center.x, center.y);
            if (weight > 0.0) {
                cell = Blend(cell, inner(col - window.colBegin, r), weight);
            }
        }
    });
}

}  // namespace terraingen
