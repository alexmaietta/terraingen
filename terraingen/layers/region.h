#pragma once

#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/layer.h"
#include "terraingen/util/geometry.h"

#include <memory>

namespace terraingen {

/// An area of the surface: a rectangle or a circle.
struct RegionShape {
    enum class Kind { Rectangle, Circle };

    Kind kind = Kind::Rectangle;
    Bounds2d rectangle{};  ///< For `Rectangle`.
    Vec2d centre{};        ///< For `Circle`.
    double radius = 0.0;   ///< For `Circle`.

    static RegionShape Rectangle(const Bounds2d& bounds) {
        return {Kind::Rectangle, bounds, {}, 0.0};
    }
    static RegionShape Circle(Vec2d centre, double radius) {
        return {Kind::Circle, {}, centre, radius};
    }

    /// How far (`x`, `y`) lies outside the shape; 0 inside it.
    double DistanceOutside(double x, double y) const;

    /// The smallest box holding the shape.
    Bounds2d Bounds() const;

    bool operator==(const RegionShape&) const = default;
};

/// @throws std::invalid_argument for a shape that is not finite, a rectangle with a min above its
/// max, or a circle with a negative radius.
void ValidateRegionShape(const RegionShape& shape);

/// Another layer applied only inside a region: its result inside `shape`, blended back to the
/// unchanged height over `feather` metres outside it along `blend`: to add a site's detail, confine
/// craters to an area, or flatten a landing pad with a `ClampLayer`.
///
/// The inner layer sees the same spacing, threads and ground as the region layer, and on grids is
/// applied only over the cells near the region. Of the features it places, those centred within the
/// region or its feather are kept, as a layer keeps those centred in its grid.
class RegionLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for a null `inner`, an invalid `shape`, or a negative or
    /// non-finite `feather`.
    RegionLayer(std::shared_ptr<const Layer> inner, RegionShape shape, double feather = 0.0,
                BlendShape blend = BlendShape::Smoothstep);

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// The inner layer's height at (`x`, `y`) blended with `height` by `Weight`, to the same bits
    /// as a cell centred there; the inner layer is not evaluated where the weight is 0.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    double MaxFeatureDiameter() const override {
        return inner_->MaxFeatureDiameter();
    }

    /// The inner layer's weight at (`x`, `y`): 1 inside the shape, 0 from `feather` outside it.
    double Weight(double x, double y) const;

    const Layer& Inner() const {
        return *inner_;
    }

  private:
    std::shared_ptr<const Layer> inner_;
    RegionShape shape_;
    double feather_;
    BlendShape blend_;
};

}  // namespace terraingen
