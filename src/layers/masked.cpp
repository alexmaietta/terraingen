// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/masked.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
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

/// The signed distance from (`px`, `py`) to the edge of `box`, east offsets scaled by `xScale`.
double BoxDistance(const Bounds2d& box, double px, double py, double xScale) {
    const double dx = xScale * std::fmax(box.minX - px, px - box.maxX);
    const double dy = std::fmax(box.minY - py, py - box.maxY);
    if (dx <= 0.0 && dy <= 0.0) {
        return std::fmax(dx, dy);
    }
    return std::hypot(std::fmax(dx, 0.0), std::fmax(dy, 0.0));
}

/// The squared distance from `p` to the segment from `a` to `b`, x scaled by `xScale`, and
/// whether the segment crosses the ray east of `p`.
struct EdgeProbe {
    double squared;
    bool crosses;
};
EdgeProbe ProbeEdge(Vec2d p, Vec2d a, Vec2d b, double xScale) {
    const bool crosses =
        (a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
    const double ex = xScale * (b.x - a.x);
    const double ey = b.y - a.y;
    const double wx = xScale * (p.x - a.x);
    const double wy = p.y - a.y;
    const double length = ex * ex + ey * ey;
    const double t = length > 0.0 ? std::clamp((wx * ex + wy * ey) / length, 0.0, 1.0) : 0.0;
    const double dx = wx - t * ex;
    const double dy = wy - t * ey;
    return {dx * dx + dy * dy, crosses};
}

/// The signed distance from `p` to the edges of `points` and `holes`, x scaled by `xScale`;
/// negative where a ray from `p` crosses them an odd number of times.
double PolygonDistance(const std::vector<Vec2d>& points,
                       const std::vector<std::vector<Vec2d>>& holes, Vec2d p, double xScale) {
    double squared = std::numeric_limits<double>::infinity();
    bool inside = false;
    const auto ring = [&](const std::vector<Vec2d>& vertices) {
        for (std::size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
            const EdgeProbe probe = ProbeEdge(p, vertices[j], vertices[i], xScale);
            squared = std::fmin(squared, probe.squared);
            inside = inside != probe.crosses;
        }
    };
    if (points.empty()) {
        return squared;
    }
    ring(points);
    for (const std::vector<Vec2d>& hole : holes) {
        if (!hole.empty()) {
            ring(hole);
        }
    }
    const double distance = std::sqrt(squared);
    return inside ? -distance : distance;
}

/// Twice the signed area of the polygon `vertices`.
double TwiceArea(const std::vector<Vec2d>& vertices) {
    double sum = 0.0;
    for (std::size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
        sum += (vertices[j].x - vertices[i].x) * (vertices[j].y + vertices[i].y);
    }
    return sum;
}

/// Checks one outline of a polygonal region, named `what` in errors.
/// @throws std::invalid_argument for fewer than 3 points, a point that is not finite, a
/// zero-length edge or no area.
void ValidateRing(const std::vector<Vec2d>& vertices, const std::string& what) {
    if (vertices.size() < 3) {
        throw std::invalid_argument("A polygonal region's " + what +
                                    " needs at least 3 points, not " +
                                    std::to_string(vertices.size()));
    }
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        if (!(std::isfinite(vertices[i].x) && std::isfinite(vertices[i].y))) {
            throw std::invalid_argument("A polygonal region's " + what + " has point " +
                                        std::to_string(i) + " not finite");
        }
    }
    for (std::size_t i = 0, j = vertices.size() - 1; i < vertices.size(); j = i++) {
        if (vertices[i] == vertices[j]) {
            throw std::invalid_argument(
                "A polygonal region's " + what + " has a zero-length edge: point " +
                std::to_string(i) + " repeats point " + std::to_string(j) +
                (i == 0 ? "; the outline closes itself, so do not repeat the first point" : ""));
        }
    }
    if (TwiceArea(vertices) == 0.0) {
        throw std::invalid_argument("A polygonal region's " + what +
                                    " has no area: its points are all on one line");
    }
}

}  // namespace

double RegionShape::DistanceOutside(double x, double y, const Surface& surface) const {
    const auto local = [&](double px, double xScale) {
        if (kind == Kind::Polygon) {
            return PolygonDistance(points, holes, {px, y}, xScale);
        }
        return BoxDistance(rectangle, px, y, xScale);
    };
    if (surface.IsBody()) {
        if (kind == Kind::Circle) {
            return surface.Distance(center, {x, y}) - radius;
        }
        const double cosLat = std::cos(y * kDegree);
        double best = std::numeric_limits<double>::infinity();
        for (const double shift : {-360.0, 0.0, 360.0}) {
            best = std::fmin(best, surface.UnitLength() * local(x + shift, cosLat));
        }
        return best;
    }
    if (kind == Kind::Circle) {
        return std::hypot(x - center.x, y - center.y) - radius;
    }
    return local(x, 1.0);
}

Bounds2d RegionShape::Bounds(const Surface& surface) const {
    if (kind == Kind::Circle) {
        return surface.Expand({center.x, center.y, center.x, center.y}, radius);
    }
    if (kind == Kind::Polygon) {
        if (points.empty()) {
            return {};
        }
        Bounds2d box{points.front().x, points.front().y, points.front().x, points.front().y};
        const auto grow = [&](const std::vector<Vec2d>& vertices) {
            for (const Vec2d& v : vertices) {
                box.minX = std::fmin(box.minX, v.x);
                box.minY = std::fmin(box.minY, v.y);
                box.maxX = std::fmax(box.maxX, v.x);
                box.maxY = std::fmax(box.maxY, v.y);
            }
        };
        grow(points);
        for (const std::vector<Vec2d>& hole : holes) {
            grow(hole);
        }
        return box;
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
    if (shape.kind == RegionShape::Kind::Polygon) {
        ValidateRing(shape.points, "outline");
        for (std::size_t h = 0; h < shape.holes.size(); ++h) {
            ValidateRing(shape.holes[h], "hole " + std::to_string(h));
        }
        return;
    }
    const Bounds2d& r = shape.rectangle;
    if (!(r.IsFinite() && r.minX <= r.maxX && r.minY <= r.maxY)) {
        throw std::invalid_argument("A rectangular region needs finite bounds with min <= max");
    }
}

MaskedLayer::MaskedLayer(std::shared_ptr<const Layer> inner, RegionShape shape, double feather,
                         BlendShape blend, TerrainBase base)
    : inner_(std::move(inner)), shape_(shape), feather_(feather), blend_(blend),
      surface_(base.surface) {
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
    for (std::size_t i = 0; i < all.boulders.Size(); ++i) {
        if (Holds(all.boulders.X()[i], all.boulders.Y()[i])) {
            held.boulders.Append(all.boulders.At(i));
        }
    }
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
