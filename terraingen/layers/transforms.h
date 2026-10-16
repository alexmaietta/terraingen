/// @file
/// @brief Height transforms as layers: scale and offset, clamp, and a function of your own.
#pragma once

#include "terraingen/layers/layer.h"

#include <functional>
#include <optional>

namespace terraingen {

/// @brief Scales and shifts the heights so far: height * `scale` + `offset`.
class ScaleLayer final : public Layer {
  public:
    /// @brief Makes the layer.
    /// @param scale Factor on the heights (vertical exaggeration).
    /// @param offset Added after scaling, in metres.
    /// @throws std::invalid_argument unless `scale` and `offset` are finite.
    ScaleLayer(double scale, double offset = 0.0);

    /// @brief Scales and shifts every cell of `heights`.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Unused.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The height scaled and shifted.
    /// @param height The height before the layer; NaN stays NaN.
    /// @param x Unused.
    /// @param y Unused.
    /// @param context Unused.
    /// @return The height after the layer.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// @brief The factor on the heights.
    /// @return The scale.
    double Scale() const {
        return scale_;
    }
    /// @brief The shift after scaling.
    /// @return The offset, in metres.
    double Offset() const {
        return offset_;
    }

  private:
    double scale_;
    double offset_;
};

/// @brief Clamps the heights so far to [`min`, `max`], either bound optional.
class ClampLayer final : public Layer {
  public:
    /// @brief Makes the layer.
    /// @param min The lowest height kept, in metres; none for no floor.
    /// @param max The highest height kept, in metres; none for no cap.
    /// @throws std::invalid_argument for a bound that is not finite or `min` above `max`.
    ClampLayer(std::optional<double> min, std::optional<double> max);

    /// @brief Clamps every cell of `heights`.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Unused.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The height clamped.
    /// @param height The height before the layer; NaN stays NaN.
    /// @param x Unused.
    /// @param y Unused.
    /// @param context Unused.
    /// @return The height after the layer.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// @brief The floor.
    /// @return The lowest height kept, if any.
    std::optional<double> Min() const {
        return min_;
    }
    /// @brief The cap.
    /// @return The highest height kept, if any.
    std::optional<double> Max() const {
        return max_;
    }

  private:
    std::optional<double> min_;
    std::optional<double> max_;
};

/// @brief The new height at (`x`, `y`) sampled every `spacing` given the `height` so far; must be
/// deterministic and thread-safe.
using HeightFunction = std::function<double(double x, double y, double spacing, double height)>;

/// @brief A layer from a function of position, spacing and the height so far.
class FunctionLayer final : public Layer {
  public:
    /// @brief Makes the layer.
    /// @param function The new height as a function of position, spacing and old height.
    /// @throws std::invalid_argument for an empty `function`.
    explicit FunctionLayer(HeightFunction function);

    /// @brief Evaluates the function at every cell centre, at the larger of `context.spacing` and
    /// the cell size.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Unused.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The function at a point, at `context.spacing`.
    /// @param height The height before the layer; NaN stays NaN.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param context How the layer is evaluated.
    /// @return The height after the layer.
    /// @throws std::invalid_argument as `ValidatePointContext`.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

  private:
    HeightFunction function_;
};

}  // namespace terraingen
