// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source.h"

#include "terraingen/sources/upsampling.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace terraingen {

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
    Raster<float> filled(grid.width, grid.height, heights);
    filled.SetTransform(grid.transform);
    Fill(filled, spacing, threads);
    for (float& value : heights) {
        value = filled.IsMissing(value) ? std::numeric_limits<float>::quiet_NaN() : value;
    }
}

float Source::HeightAt(double x, double y, double spacing) const {
    float height = 0.0f;
    const Vec2d point{x, y};
    HeightsAt(Span<const Vec2d>(&point, 1), Span<float>(&height, 1), spacing);
    return height;
}

void Source::HeightsAt(Span<const Vec2d> points, Span<float> heights, double spacing) const {
    SampleBlock samples(points, heights);
    Fill(samples, spacing, 1);
}

}  // namespace terraingen
