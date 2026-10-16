/// @file
/// Height transforms as layers: scale and offset, clamp, and a function of your own.
#pragma once

#include "terraingen/layers/layer.h"

#include <functional>
#include <optional>

namespace terraingen {

/// Scales and shifts the heights so far: height * `scale` + `offset`.
class ScaleLayer final : public FormulaLayer {
  public:
    /// @param scale Factor on the heights (vertical exaggeration).
    /// @param offset Added after scaling, in metres.
    /// @throws std::invalid_argument unless `scale` and `offset` are finite.
    ScaleLayer(double scale, double offset = 0.0);

    double Scale() const {
        return scale_;
    }
    /// The shift after scaling, in metres.
    double Offset() const {
        return offset_;
    }

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

  private:
    double scale_;
    double offset_;
};

/// Clamps the heights so far to [`min`, `max`], either bound optional.
class ClampLayer final : public FormulaLayer {
  public:
    /// @param min The lowest height kept, in metres; none for no floor.
    /// @param max The highest height kept, in metres; none for no cap.
    /// @throws std::invalid_argument for a bound that is not finite or `min` above `max`.
    ClampLayer(std::optional<double> min, std::optional<double> max);

    std::optional<double> Min() const {
        return min_;
    }
    std::optional<double> Max() const {
        return max_;
    }

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

  private:
    std::optional<double> min_;
    std::optional<double> max_;
};

/// The new height at (`x`, `y`) sampled every `spacing` metres given the `height` so far; must be
/// deterministic and thread-safe.
using HeightFunction = std::function<double(double x, double y, double spacing, double height)>;

/// A layer from a function of position, sample spacing and the height so far.
class FunctionLayer final : public FormulaLayer {
  public:
    /// @param surface What the terrain lies on, which turns a grid's cell size into metres.
    /// @throws std::invalid_argument for an empty `function`.
    explicit FunctionLayer(HeightFunction function, Surface surface = {});

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

  private:
    HeightFunction function_;
};

}  // namespace terraingen
