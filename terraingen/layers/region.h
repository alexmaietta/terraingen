/// @file
/// @brief `RegionLayer`, another layer applied only inside a rectangle or circle.
#pragma once

#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/geometry.h"

#include <memory>
#include <optional>

namespace terraingen {

/// @brief An area of the surface: a rectangle or a circle.
struct RegionShape {
    /// @brief Which shape the region is.
    enum class Kind { Rectangle, Circle };

    Kind kind = Kind::Rectangle;  ///< Which of the fields below apply.
    Bounds2d rectangle{};         ///< For `Rectangle`; a longitude-latitude box on a sphere.
    Vec2d centre{};               ///< For `Circle`; longitude and latitude on a sphere.
    double radius = 0.0;          ///< For `Circle`, in metres.

    /// @brief A rectangular region.
    /// @param bounds The rectangle, in surface coordinates.
    /// @return The region.
    static RegionShape Rectangle(const Bounds2d& bounds) {
        return {Kind::Rectangle, bounds, {}, 0.0};
    }
    /// @brief A circular region.
    /// @param centre The centre, in surface coordinates.
    /// @param radius The radius, in metres.
    /// @return The region.
    static RegionShape Circle(Vec2d centre, double radius) {
        return {Kind::Circle, {}, centre, radius};
    }

    /// @brief How far a point lies outside the shape.
    ///
    /// On a sphere a rectangle's distance is measured in the point's local frame, east shortened
    /// by the cosine of latitude, an approximation for feathers small against the body.
    /// @param x Surface x of the point.
    /// @param y Surface y of the point.
    /// @param surface What the shape lies on.
    /// @return The distance in metres; 0 inside the shape.
    double DistanceOutside(double x, double y, const Surface& surface = {}) const;

    /// @brief The smallest box holding the shape.
    /// @param surface What the shape lies on.
    /// @return The box, in `surface`'s coordinates.
    Bounds2d Bounds(const Surface& surface = {}) const;

    bool operator==(const RegionShape&) const = default;
};

/// @brief Checks a region shape.
/// @param shape The shape to check.
/// @throws std::invalid_argument for a shape that is not finite, a rectangle with a min above its
/// max, or a circle with a negative radius.
void ValidateRegionShape(const RegionShape& shape);

/// @brief Another layer applied only inside a region, blended back to the unchanged height over
/// `feather` metres outside it.
///
/// The inner layer sees the same context and, on grids, is applied only to the cells near the
/// region. Of its features, those centred within the region or its feather (`Holds`) belong to it.
class RegionLayer final : public Layer {
  public:
    /// @brief Makes the layer.
    /// @param inner The layer to confine.
    /// @param shape The region.
    /// @param feather Width of the blend outside the region, in metres; non-negative.
    /// @param blend The curve of the blend.
    /// @param surface What the region lies on.
    /// @throws std::invalid_argument for a null `inner`, an invalid `shape`, or a negative or
    /// non-finite `feather`.
    RegionLayer(std::shared_ptr<const Layer> inner, RegionShape shape, double feather = 0.0,
                BlendShape blend = BlendShape::Smoothstep, Surface surface = {});

    /// @brief The inner layer's largest feature.
    /// @return Its diameter in world units.
    double MaxFeatureDiameter() const override {
        return inner_->MaxFeatureDiameter();
    }

    /// @brief The inner layer's weight at a point.
    /// @param x Surface x of the point.
    /// @param y Surface y of the point.
    /// @return 1 inside the shape, 0 from `feather` outside it, the blend between.
    double Weight(double x, double y) const;

    /// @brief The confined layer.
    /// @return The inner layer.
    const Layer& Inner() const {
        return *inner_;
    }

    /// Whether a feature centred at (x, y) belongs to the region: within it or its feather.
    bool Holds(double x, double y) const;

    /// The part of `grid` the inner layer is applied to, the cells near the region; nothing when no
    /// cell is.
    std::optional<RasterGrid> InnerGrid(const RasterGrid& grid) const;

  protected:
    /// Applies the inner layer to the samples near the region and blends it in by `Weight`.
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    void ApplyToGrid(Raster<float>& heights, const LayerContext& context) const;
    void ApplyToPoints(SampleBlock& samples, const LayerContext& context) const;
    PixelWindow InnerWindow(const RasterGrid& grid) const;

    std::shared_ptr<const Layer> inner_;
    RegionShape shape_;
    double feather_;
    BlendShape blend_;
    Surface surface_;
};

}  // namespace terraingen
