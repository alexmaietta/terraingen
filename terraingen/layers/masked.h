// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `MaskedLayer`, another layer applied only inside a rectangle, circle or polygon.
#pragma once

#include "terraingen/layers/boulders/boulder_placer.h"
#include "terraingen/layers/craters/crater_placer.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/geometry.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace terraingen {

/// An area of the surface: a rectangle, a circle or a polygon.
struct RegionShape {
    /// Which shape the region is.
    enum class Kind { Rectangle, Circle, Polygon };

    Kind kind = Kind::Rectangle;  ///< Which of the fields below apply.
    Bounds2d rectangle{};         ///< For `Rectangle`; a longitude-latitude box on a sphere.
    Vec2d center{};               ///< For `Circle`; longitude and latitude on a sphere.
    double radius = 0.0;          ///< For `Circle`, in meters.
    std::vector<Vec2d>
        points{};  ///< For `Polygon`, its outline; longitude and latitude on a sphere.
    std::vector<std::vector<Vec2d>> holes{};  ///< For `Polygon`, outlines cut out of it.

    /// A rectangular region of `bounds` (surface coordinates).
    static RegionShape Rectangle(const Bounds2d& bounds) {
        RegionShape shape;
        shape.rectangle = bounds;
        return shape;
    }
    /// A circular region around `center` (surface coordinates).
    /// @param radius In meters.
    static RegionShape Circle(Vec2d center, double radius) {
        RegionShape shape;
        shape.kind = Kind::Circle;
        shape.center = center;
        shape.radius = radius;
        return shape;
    }
    /// A polygonal region outlined by `points` (surface coordinates, x east and y north), less
    /// the polygons `holes`.
    ///
    /// The outline is closed from the last point back to the first and may be concave. A point
    /// is inside when a ray from it crosses the outline and holes an odd number of times.
    static RegionShape Polygon(std::vector<Vec2d> points,
                               std::vector<std::vector<Vec2d>> holes = {}) {
        RegionShape shape;
        shape.kind = Kind::Polygon;
        shape.points = std::move(points);
        shape.holes = std::move(holes);
        return shape;
    }

    /// The signed distance in meters from surface (x, y) to the shape's edge: positive outside,
    /// negative inside, 0 on the edge. A polygon's holes are outside it.
    ///
    /// On a sphere a rectangle's or polygon's distance is measured in the point's local frame,
    /// east shortened by the cosine of latitude, an approximation for feathers small against the
    /// body.
    double DistanceOutside(double x, double y, const Surface& surface = {}) const;

    /// The smallest box holding the shape, in `surface`'s coordinates.
    Bounds2d Bounds(const Surface& surface = {}) const;

    bool operator==(const RegionShape& other) const {
        return kind == other.kind && rectangle.minX == other.rectangle.minX &&
               rectangle.minY == other.rectangle.minY && rectangle.maxX == other.rectangle.maxX &&
               rectangle.maxY == other.rectangle.maxY && center == other.center &&
               radius == other.radius && points == other.points && holes == other.holes;
    }
    bool operator!=(const RegionShape& other) const {
        return !(*this == other);
    }
};

/// Checks a region shape.
/// @throws std::invalid_argument for a shape that is not finite, a rectangle with a min above its
/// max, a circle with a negative radius, or a polygon or hole with fewer than 3 points, a
/// zero-length edge or no area.
void ValidateRegionShape(const RegionShape& shape);

/// Another layer applied only inside a region, blended back to the unchanged height over
/// `feather` meters outside it.
///
/// The inner layer sees the same context and, on grids, is applied only to the cells near the
/// region. Of its features, those centered within the region or its feather (`Holds`) belong to it.
class MaskedLayer final : public Layer, public CraterPlacer, public BoulderPlacer {
  public:
    /// @param feather Width of the blend outside the region, in meters; non-negative.
    /// @throws std::invalid_argument for a null `inner`, an invalid `shape`, or a negative or
    /// non-finite `feather`.
    MaskedLayer(std::shared_ptr<const Layer> inner, RegionShape shape, double feather = 0.0,
                BlendShape blend = BlendShape::Smoothstep, TerrainBase base = {});

    /// The inner layer's weight at surface (x, y): 1 inside the shape, 0 from `feather` outside
    /// it, the blend between.
    double Weight(double x, double y) const;

    /// The confined layer.
    const Layer& Inner() const {
        return *inner_;
    }

    std::optional<Surface> LayerSurface() const override {
        return surface_;
    }

    /// The layers the confined layer reads.
    std::vector<const Layer*> Dependencies() const override {
        return inner_->Dependencies();
    }

    /// Whether a feature centered at (x, y) belongs to the region: within it or its feather.
    bool Holds(double x, double y) const;

    /// The inner layer's craters over its part of `grid` that the region `Holds`; none when the
    /// inner layer places no craters.
    CraterFeatures CratersOver(const RasterGrid& grid, double spacing) const override;

    /// The inner layer's boulders over its part of `grid` that the region `Holds`; none when the
    /// inner layer places no boulders.
    BoulderFeatures BouldersOver(const RasterGrid& grid, double spacing,
                                 const Ground* ground) const override;

  protected:
    /// Applies the inner layer to the samples near the region and blends it in by `Weight`.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    void ApplyToGrid(Raster<float>& heights, const LayerContext& context) const;
    void ApplyToPoints(SampleBlock& samples, const LayerContext& context) const;
    PixelWindow InnerWindow(const RasterGrid& grid) const;
    /// The part of `grid` the inner layer is applied to; nothing when no cell is near the region.
    std::optional<RasterGrid> InnerGrid(const RasterGrid& grid) const;

    std::shared_ptr<const Layer> inner_;
    RegionShape shape_;
    double feather_;
    BlendShape blend_;
    Surface surface_;
};

}  // namespace terraingen
