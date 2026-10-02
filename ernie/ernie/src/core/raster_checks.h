#pragma once

#include "ernie/core/raster_view.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace ernie {

/// Throws `std::invalid_argument`, naming `what`, unless `view` has data, at least `minSide` cells
/// along each side, a row stride of at least its width, and a finite origin and positive finite
/// cell size.
inline void CheckRasterView(const RasterView& view, const std::string& what, std::size_t minSide) {
    if (view.width < minSide || view.height < minSide) {
        throw std::invalid_argument(what + " must be at least " + std::to_string(minSide) + " x " +
                                    std::to_string(minSide) + " cells");
    }
    if (view.data == nullptr) {
        throw std::invalid_argument(what + " has no data");
    }
    if (view.Stride() < view.width) {
        throw std::invalid_argument(what + " row stride is shorter than its width");
    }
    if (!std::isfinite(view.originX) || !std::isfinite(view.originY) ||
        !std::isfinite(view.cellSize) || !(view.cellSize > 0.0)) {
        throw std::invalid_argument(what + " needs a finite origin and a positive cell size");
    }
}

}  // namespace ernie
