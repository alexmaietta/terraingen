// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Interpolation kernel weights and kernel interpolation over a lattice.
#pragma once

#include "terraingen/raster/resample.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace terraingen {

/// The weight of a cubic, Lanczos or B-spline kernel at an offset of `x` cells, before any
/// normalization.
double KernelWeight(ResampleMethod method, double x);

/// The weights of the 2 `KernelRadius` cells around a point `fraction` of the way from cell 0 to
/// cell 1, for cells -radius + 1 to radius, as the interpolation applies them; nearest is not a
/// kernel and gives none.
/// @param fraction In [0, 1).
std::array<double, 6> KernelWeights(ResampleMethod method, double fraction);

/// `InterpolateLattice` of a kernel of `radius` from its taps' weights, as `KernelWeights` gives
/// them: `wxs` along the row from column `u0` and `wys` down the column from row `v0`, the cell
/// at or before the point along each axis. The result has the type `value` gives.
template <typename Value, typename Missing,
          typename Result = std::decay_t<std::invoke_result_t<const Value&, double, double>>>
std::optional<Result> InterpolateLatticeWeights(double u0, double v0,
                                                const std::array<double, 6>& wxs,
                                                const std::array<double, 6>& wys, int radius,
                                                const Value& value, const Missing& missing) {
    double sum = 0.0;
    for (int j = 1 - radius; j <= radius; ++j) {
        const double wy = wys[static_cast<std::size_t>(j + radius - 1)];
        for (int i = 1 - radius; i <= radius; ++i) {
            const double wx = wxs[static_cast<std::size_t>(i + radius - 1)];
            if (wx * wy == 0.0) {
                continue;
            }
            const Result cell = value(u0 + i, v0 + j);
            if (missing(cell)) {
                return std::nullopt;
            }
            sum += wx * wy * static_cast<double>(cell);
        }
    }
    return static_cast<Result>(sum);
}

/// Values on a lattice interpolated at lattice position (`u`, `v`), measured in cells from the
/// center of cell (0, 0), with `method`: cells are read as `value(col, row)` with whole `col`
/// and `row` as doubles, and taps of zero weight are skipped. Nothing where a tap with weight
/// holds a value that `missing(value)` says has no data.
/// @tparam Value Callable as `float(double col, double row)` or `double(double col, double row)`.
/// @tparam Missing Callable as `bool(float value)`, or of a double for double values.
template <typename Value, typename Missing,
          typename Result = std::decay_t<std::invoke_result_t<const Value&, double, double>>>
std::optional<Result> InterpolateLattice(double u, double v, ResampleMethod method,
                                         const Value& value, const Missing& missing) {
    if (method == ResampleMethod::Nearest) {
        const Result nearest = value(std::round(u), std::round(v));
        if (missing(nearest)) {
            return std::nullopt;
        }
        return nearest;
    }
    const double u0 = std::floor(u);
    const double v0 = std::floor(v);
    return InterpolateLatticeWeights(u0, v0, KernelWeights(method, u - u0),
                                     KernelWeights(method, v - v0), KernelRadius(method), value,
                                     missing);
}

}  // namespace terraingen
