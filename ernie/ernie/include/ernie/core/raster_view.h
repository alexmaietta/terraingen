#pragma once

#include <cmath>
#include <cstddef>
#include <optional>

namespace ernie {

/// Non-owning view of a north-up grid of `float` samples with square cells: the form in which
/// Ernie takes heightfields and albedo maps.
///
/// Row 0 is the northern edge. `originX` and `originY` are the world coordinates of the north-west
/// corner of the first cell, so cell (`col`, `row`) is centred at
/// (`originX + (col + 0.5) cellSize`, `originY - (row + 0.5) cellSize`). The world frame is x east,
/// y north, z up, in the same units as the camera; heights are in those units too.
struct RasterView {
    const float* data = nullptr;
    std::size_t width = 0;
    std::size_t height = 0;
    std::size_t rowStride = 0;  ///< Elements from one row to the next; 0 means `width`.
    double originX = 0.0;
    double originY = 0.0;
    double cellSize = 1.0;        ///< Side of a cell in world units; positive.
    std::optional<float> noData;  ///< Value marking cells without data, if any.

    std::size_t Stride() const {
        return rowStride == 0 ? width : rowStride;
    }

    float operator()(std::size_t col, std::size_t row) const {
        return data[row * Stride() + col];
    }

    /// Whether `value` is finite and not the nodata value.
    bool IsValid(float value) const {
        return std::isfinite(value) && !(noData && value == *noData);
    }
};

}  // namespace ernie
