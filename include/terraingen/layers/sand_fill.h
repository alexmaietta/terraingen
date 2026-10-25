// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `SandFillLayer`, sand pooled in the lows of the ground beneath it.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/util/geometry.h"

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

/// Sand pooled in the lows of the ground beneath it: cracks, troughs and hollows between rises.
///
/// A point's surroundings are the mean ground height on two rings, at half the reach and at the
/// reach; where the point lies below them, sand fills `fill` of the depth, thickening smoothly to
/// at most `maxThickness`. Reads the ground beneath it at points, so grids, tiles and points
/// agree; it lays sand over the material map.
class SandFillLayer final : public FormulaLayer, public MaterialSource {
  public:
    /// On a body, the rings lie in each point's local frame.
    /// @throws std::invalid_argument as `ValidateSandFillOptions`.
    explicit SandFillLayer(SandFillOptions options = {}, TerrainBase base = {});

    /// The sand thickness at world (x, y) over `ground`, in meters.
    /// @param spacing Sample spacing in meters the ground is read at.
    double ThicknessAt(double x, double y, double spacing, const Ground& ground) const;

    /// The options the layer was made with.
    const SandFillOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Sand;
    }
    /// @throws std::invalid_argument without `ground`.
    double MaterialCoverAt(double x, double y, double spacing, const Ground* ground) const override;

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;
    void CheckContext(const LayerContext& context) const override;

  private:
    double Thickness(const Ground& ground, double x, double y, double height, double spacing) const;

    /// Points on each ring the surroundings are read at; the inner ring is turned half a step.
    static constexpr int kRingPoints = 8;

    SandFillOptions options_;
    /// The two rings read around a point, inner first, in meters east and north of it.
    std::array<Vec2d, 2 * kRingPoints> rings_{};
};

}  // namespace terraingen
