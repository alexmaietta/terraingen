// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/analysis/raster_stats.h"

#include "terraingen/geometry/geometry.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace terraingen {

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

/// The interpolated `q` quantile of `values`, reordering them; NaN when empty.
/// @param q The quantile, from 0 to 1.
double Quantile(std::vector<double>& values, double q) {
    if (values.empty()) {
        return kNan;
    }
    const double position = q * static_cast<double>(values.size() - 1);
    const auto rank = static_cast<std::size_t>(std::floor(position));
    const auto at = values.begin() + static_cast<std::ptrdiff_t>(rank);
    std::nth_element(values.begin(), at, values.end());
    const double low = *at;
    if (rank + 1 >= values.size()) {
        return low;
    }
    const double high = *std::min_element(at + 1, values.end());
    return low + (position - static_cast<double>(rank)) * (high - low);
}

/// The neighbors along one grid axis a difference at `index` spans: both sides when they have
/// data, else the side that has and `index` itself; nothing when neither has.
/// @param valid Whether the cell `step` cells from `index` has data, for a step of -1 or 1.
template <typename Valid>
bool DifferenceSpan(std::size_t index, std::size_t size, const Valid& valid, std::size_t& first,
                    std::size_t& second) {
    const bool before = index > 0 && valid(index - 1);
    const bool after = index + 1 < size && valid(index + 1);
    first = before ? index - 1 : index;
    second = after ? index + 1 : index;
    return before || after;
}

/// The slope in degrees of the heights at cell (`col`, `row`), or NaN where it cannot be taken.
double CellSlope(const Raster<float>& heights, const Surface& surface, std::size_t col,
                 std::size_t row) {
    const auto valid = [&heights](std::size_t c, std::size_t r) {
        return !heights.IsMissing(heights(c, r));
    };
    if (!valid(col, row)) {
        return kNan;
    }
    std::size_t west = 0;
    std::size_t east = 0;
    std::size_t north = 0;
    std::size_t south = 0;
    const bool across = DifferenceSpan(
        col, heights.Width(), [&](std::size_t c) { return valid(c, row); }, west, east);
    const bool down = DifferenceSpan(
        row, heights.Height(), [&](std::size_t r) { return valid(col, r); }, north, south);
    if (!across || !down) {
        return kNan;
    }
    const Vec2d center = heights.CellCenter(col, row);
    const Vec2d a = surface.Offset(center, heights.CellCenter(east, row)) -
                    surface.Offset(center, heights.CellCenter(west, row));
    const Vec2d b = surface.Offset(center, heights.CellCenter(col, south)) -
                    surface.Offset(center, heights.CellCenter(col, north));
    const double riseA =
        static_cast<double>(heights(east, row)) - static_cast<double>(heights(west, row));
    const double riseB =
        static_cast<double>(heights(col, south)) - static_cast<double>(heights(col, north));
    const double det = a.x * b.y - a.y * b.x;
    if (det == 0.0 || !std::isfinite(det)) {
        return kNan;
    }
    const double dx = (riseA * b.y - riseB * a.y) / det;
    const double dy = (a.x * riseB - b.x * riseA) / det;
    return GradientSlopeDegrees(dx, dy);
}

}  // namespace

RasterStats ComputeStats(const Raster<float>& raster) {
    RasterStats stats;
    const auto forEachValid = [&](const auto& visit) {
        for (const float value : raster.Values()) {
            if (!raster.IsMissing(value)) {
                visit(static_cast<double>(value));
            }
        }
    };
    double sum = 0.0;
    forEachValid([&](double v) {
        stats.min = stats.validCount == 0 ? v : std::fmin(stats.min, v);
        stats.max = stats.validCount == 0 ? v : std::fmax(stats.max, v);
        sum += v;
        ++stats.validCount;
    });
    if (stats.validCount == 0) {
        return stats;
    }
    const auto count = static_cast<double>(stats.validCount);
    stats.mean = sum / count;
    double squares = 0.0;
    forEachValid([&](double v) { squares += (v - stats.mean) * (v - stats.mean); });
    stats.stddev = std::sqrt(squares / count);
    return stats;
}

SlopeStats ComputeSlopeStats(const Raster<float>& heights, const Surface& surface,
                             unsigned threads) {
    const std::size_t width = heights.Width();
    std::vector<double> cells(heights.Size(), kNan);
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < width; ++col) {
            cells[row * width + col] = CellSlope(heights, surface, col, row);
        }
    });
    std::vector<double> slopes;
    double sum = 0.0;
    for (const double slope : cells) {
        if (!std::isnan(slope)) {
            slopes.push_back(slope);
            sum += slope;
        }
    }
    SlopeStats stats;
    stats.validCount = slopes.size();
    if (slopes.empty()) {
        return stats;
    }
    stats.mean = sum / static_cast<double>(slopes.size());
    stats.median = Quantile(slopes, 0.5);
    stats.p95 = Quantile(slopes, 0.95);
    return stats;
}

}  // namespace terraingen
