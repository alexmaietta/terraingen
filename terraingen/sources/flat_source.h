/// @file
/// @brief A source of constant elevation, the start of a fully synthetic terrain.
#pragma once

#include "terraingen/sources/source.h"
#include "terraingen/util/surface.h"

#include <cstddef>

namespace terraingen {

/// A level datum at `elevation` over a `width` x `height` north-up grid of square
/// `pixelSize` cells centred on `centre`: the start of a fully synthetic terrain.
class FlatSource final : public Source {
  public:
    /// The flat source over the given grid.
    /// @param width Columns, at least 1.
    /// @param height Rows, at least 1.
    /// @param pixelSize Cell size in world units (metres), positive and finite.
    /// @param elevation Height of every cell in metres, finite.
    /// @param centre Grid centre in world units, finite; the origin by default.
    /// @throws std::invalid_argument if `width` or `height` is zero, `pixelSize` is not positive
    /// and finite, or `elevation` or `centre` is not finite.
    FlatSource(std::size_t width, std::size_t height, double pixelSize, float elevation = 0.0f,
               Vec2d centre = {});

    /// The flat source over a grid on `surface` whose cell size is given in metres, as for a site
    /// on a body: the grid is in the surface's coordinates.
    /// @param pixelSize Cell size in metres, positive and finite; on a body it is converted to
    /// degrees along a meridian (`Surface::UnitLength`).
    /// @param centre Grid centre in the surface's coordinates (longitude and latitude in degrees on
    /// a body).
    /// @throws std::invalid_argument as the constructor in world units.
    FlatSource(std::size_t width, std::size_t height, double pixelSize, float elevation,
               Vec2d centre, const Surface& surface)
        : FlatSource(width, height, pixelSize / surface.UnitLength(), elevation, centre) {}

    /// The grid given at construction.
    RasterGrid Grid() const override {
        return grid_;
    }

    /// Sets every cell of `heights` to the elevation.
    void Fill(Raster<float>& heights) const override;

    /// The elevation in metres, everywhere.
    float HeightAt(double /*x*/, double /*y*/) const override {
        return elevation_;
    }

  private:
    RasterGrid grid_;
    float elevation_;
};

}  // namespace terraingen
