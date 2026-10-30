// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A source of constant elevation, the start of a fully synthetic terrain.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/sources/source.h"

#include <cstddef>

namespace terraingen {

/// A level datum at `elevation` over a `width` x `height` north-up grid of square
/// `pixelSize` cells centered on `center`: the start of a fully synthetic terrain.
class FlatSource final : public Source {
  public:
    /// The flat source over the given grid.
    /// @param width Columns, at least 1.
    /// @param height Rows, at least 1.
    /// @param pixelSize Cell size in world units (meters), positive and finite.
    /// @param elevation Height of every cell in meters, finite.
    /// @param center Grid center in world units, finite; the origin by default.
    /// @throws std::invalid_argument if `width` or `height` is zero, `pixelSize` is not positive
    /// and finite, or `elevation` or `center` is not finite.
    FlatSource(std::size_t width, std::size_t height, double pixelSize, float elevation = 0.0f,
               Vec2d center = {});

    /// The flat source over a grid on `surface` whose cell size is given in meters, as for a site
    /// on a body: the grid is in the surface's coordinates.
    /// @param pixelSize Cell size in meters, positive and finite; on a body it is converted to
    /// degrees along a meridian (`Surface::UnitLength`).
    /// @param center Grid center in the surface's coordinates (longitude and latitude in degrees on
    /// a body).
    /// @throws std::invalid_argument as the constructor in world units.
    FlatSource(std::size_t width, std::size_t height, double pixelSize, float elevation,
               Vec2d center, const Surface& surface)
        : FlatSource(width, height, pixelSize / surface.UnitLength(), elevation, center) {}

    /// The grid given at construction.
    RasterGrid Grid() const override {
        return grid_;
    }

  protected:
    /// Sets every sample to the elevation.
    void FillSamples(SampleBlock& samples, double spacing, unsigned threads) const override;

  private:
    RasterGrid grid_;
    float elevation_;
};

}  // namespace terraingen
