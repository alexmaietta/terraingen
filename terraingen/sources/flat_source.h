#pragma once

#include "terraingen/sources/source.h"

#include <cstddef>

namespace terraingen {

/// A level datum at `elevation` over a `width` x `height` north-up grid of square `pixelSize` cells
/// centred on `centre` (the origin by default): the start of a fully synthetic terrain.
class FlatSource final : public Source {
  public:
    /// @throws std::invalid_argument if `width` or `height` is zero, or `pixelSize` or `elevation`
    /// is not finite or `pixelSize` not positive.
    FlatSource(std::size_t width, std::size_t height, double pixelSize, float elevation = 0.0f,
               Vec2d centre = {});

    RasterGrid Grid() const override {
        return grid_;
    }

    void Fill(Raster<float>& heights) const override;

    float HeightAt(double /*x*/, double /*y*/) const override {
        return elevation_;
    }

  private:
    RasterGrid grid_;
    float elevation_;
};

}  // namespace terraingen
