// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `SandFillLayer`, sand pooled in the lows of the ground beneath it.
#pragma once

#include "terraingen/geometry/geometry.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"

#include <array>

namespace terraingen {

/// How much sand a `SandFillLayer` pools, and where.
struct SandFillOptions {
    double reach = 10.0;  ///< Radius in meters of the surroundings a low is measured against.
    /// Share, in [0, 1], of a low's depth below its surroundings that sand fills.
    double fill = 0.6;
    double maxThickness = 0.5;  ///< The thickest the sand gets, approached smoothly (m).
    /// Sand thickness in meters at which it fully covers the ground; thinner sand covers part.
    double coverThickness = 0.02;

    bool operator==(const SandFillOptions& other) const {
        return reach == other.reach && fill == other.fill && maxThickness == other.maxThickness &&
               coverThickness == other.coverThickness;
    }
};

/// Checks sand fill options.
/// @throws std::invalid_argument for a reach, maximum or cover thickness that is not positive and
/// finite, or a fill outside [0, 1].
void ValidateSandFillOptions(const SandFillOptions& options);

/// Sand pooled in the lows of the ground beneath it, filling `fill` of the depth below the mean
/// height on two rings, up to `maxThickness`; it lays sand over the material map.
class SandFillLayer final : public FormulaLayer, public MaterialSource {
  public:
    /// On a body, the rings lie in each point's local frame.
    /// @throws std::invalid_argument as `ValidateSandFillOptions`.
    explicit SandFillLayer(SandFillOptions options = {});

    /// The sand thickness at world (x, y) over `context.ground`, read at `context.spacing` on
    /// its surface, in meters.
    /// @throws std::invalid_argument without `context.ground`.
    double ThicknessAt(double x, double y, const LayerContext& context) const;

    /// The options the layer was made with.
    const SandFillOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Sand;
    }
    /// @throws std::invalid_argument without `context.ground`.
    double MaterialCoverAt(double x, double y, const LayerContext& context) const override;

  protected:
    float HeightAt(float height, double x, double y, const LayerContext& context) const override;
    void CheckContext(const LayerContext& context) const override;

  private:
    /// The thickness at (x, y) where the ground is `height` meters high, read on the context's.
    double Thickness(double x, double y, double height, const LayerContext& context) const;

    /// Points on each ring the surroundings are read at; the inner ring is turned half a step.
    static constexpr int kRingPoints = 8;

    SandFillOptions options_;
    /// The two rings read around a point, inner first, in meters east and north of it.
    std::array<Vec2d, 2 * kRingPoints> rings_{};
};

}  // namespace terraingen
