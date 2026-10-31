// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source.h"

#include "sources/lattice.h"

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace terraingen {

void MarkMissing(Raster<float>& heights, std::optional<float> noData) {
    const float fill = noData.value_or(std::numeric_limits<float>::quiet_NaN());
    bool missing = false;
    for (float& value : heights.Values()) {
        if (std::isnan(value)) {
            value = fill;
            missing = true;
        }
    }
    heights.SetNoData(noData || !missing ? noData : std::optional<float>(fill));
}

std::optional<RasterGrid> detail::ExactSubgrid(const RasterGrid& grid, const PixelWindow& cells) {
    const GeoTransform& whole = grid.transform;
    if (whole.IsRotated() || cells.Empty()) {
        return std::nullopt;
    }
    const RasterGrid part = Subgrid(grid, cells);
    const auto center = [](const GeoTransform& transform, std::size_t col, std::size_t row) {
        return transform.PixelToWorld(static_cast<double>(col) + 0.5,
                                      static_cast<double>(row) + 0.5);
    };
    for (std::size_t c = 0; c < part.width; ++c) {
        if (center(whole, cells.colBegin + c, 0).x != center(part.transform, c, 0).x) {
            return std::nullopt;
        }
    }
    for (std::size_t r = 0; r < part.height; ++r) {
        if (center(whole, 0, cells.rowBegin + r).y != center(part.transform, 0, r).y) {
            return std::nullopt;
        }
    }
    return part;
}

void Source::Fill(SampleBlock& samples, double spacing, unsigned threads) const {
    FillSamples(samples, spacing, threads);
    if (Raster<float>* grid = samples.Grid()) {
        MarkMissing(*grid, grid->NoData());
    }
}

void Source::Fill(Raster<float>& heights, double spacing, unsigned threads) const {
    SampleBlock samples(heights);
    Fill(samples, spacing, threads);
}

void Source::Fill(const RasterGrid& grid, Span<float> heights, double spacing,
                  unsigned threads) const {
    if (heights.size() != grid.width * grid.height) {
        throw std::invalid_argument("Filling a source needs " +
                                    std::to_string(grid.width * grid.height) + " heights, not " +
                                    std::to_string(heights.size()));
    }
    Raster<float> filled(grid, heights);
    Fill(filled, spacing, threads);
    for (float& value : heights) {
        value = filled.IsMissing(value) ? std::numeric_limits<float>::quiet_NaN() : value;
    }
}

float Source::HeightAt(double x, double y, double spacing) const {
    return detail::PointHeight(x, y, [&](SampleBlock& samples) { Fill(samples, spacing, 1); });
}

void Source::HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const {
    SampleBlock samples(points, heights);
    Fill(samples, spacing, 1);
}

}  // namespace terraingen
