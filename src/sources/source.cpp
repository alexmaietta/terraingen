// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace detail {

void RequireHeightPerPoint(Span<const Vec2d> points, Span<float> heights) {
    if (points.size() != heights.size()) {
        throw std::invalid_argument("Heights at points need one height per point, not " +
                                    std::to_string(heights.size()) + " for " +
                                    std::to_string(points.size()) + " points");
    }
}

}  // namespace detail

void Source::Fill(const RasterGrid& grid, Span<float> heights, double spacing,
                  unsigned threads) const {
    if (heights.size() != grid.width * grid.height) {
        throw std::invalid_argument("Filling a source needs " +
                                    std::to_string(grid.width * grid.height) + " heights, not " +
                                    std::to_string(heights.size()));
    }
    Raster<float> filled(grid.width, grid.height, heights);
    filled.SetTransform(grid.transform);
    Fill(filled, spacing, threads);
    for (float& value : heights) {
        value = filled.IsMissing(value) ? std::numeric_limits<float>::quiet_NaN() : value;
    }
}

void Source::HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const {
    detail::RequireHeightPerPoint(points, heights);
    for (std::size_t i = 0; i < points.size(); ++i) {
        heights[i] = HeightAt(points[i].x, points[i].y, spacing);
    }
}

}  // namespace terraingen
