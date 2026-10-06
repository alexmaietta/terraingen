#pragma once

#include "terraingen/layers/layer.h"

#include <functional>
#include <optional>

namespace terraingen {

/// Scales and shifts the heights so far: height * `scale` + `offset`, for vertical exaggeration or
/// a datum shift.
class ScaleLayer final : public Layer {
  public:
    /// @throws std::invalid_argument unless `scale` and `offset` are finite.
    ScaleLayer(double scale, double offset = 0.0);

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// NaN stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    double Scale() const {
        return scale_;
    }
    double Offset() const {
        return offset_;
    }

  private:
    double scale_;
    double offset_;
};

/// Clamps the heights so far to [`min`, `max`], either bound optional: to flatten a basin floor or
/// cap peaks.
class ClampLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for a bound that is not finite or `min` above `max`.
    ClampLayer(std::optional<double> min, std::optional<double> max);

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// NaN stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    std::optional<double> Min() const {
        return min_;
    }
    std::optional<double> Max() const {
        return max_;
    }

  private:
    std::optional<double> min_;
    std::optional<double> max_;
};

/// The new height at (`x`, `y`) sampled every `spacing` given the `height` so far; must be
/// deterministic and safe to call from several threads at once.
using HeightFunction = std::function<double(double x, double y, double spacing, double height)>;

/// A layer from a function of position, spacing and the height so far, for transforms of your own.
class FunctionLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for an empty `function`.
    explicit FunctionLayer(HeightFunction function);

    /// Cells are evaluated at their centres, at the larger of `context.spacing` and the cell size.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// NaN stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

  private:
    HeightFunction function_;
};

}  // namespace terraingen
