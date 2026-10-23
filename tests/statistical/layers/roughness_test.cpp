// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/roughness.h"

#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

namespace {

/// The RMS slope over `lag` cells of `heights`, along rows and columns together.
double MeasuredRmsSlope(const terraingen::Raster<float>& heights, std::size_t lag, double spacing) {
    double sum = 0.0;
    double count = 0.0;
    for (std::size_t row = 0; row + lag < heights.Height(); ++row) {
        for (std::size_t col = 0; col + lag < heights.Width(); ++col) {
            const auto h = static_cast<double>(heights(col, row));
            const double dx = static_cast<double>(heights(col + lag, row)) - h;
            const double dy = static_cast<double>(heights(col, row + lag)) - h;
            sum += dx * dx + dy * dy;
            count += 2.0;
        }
    }
    return std::sqrt(sum / count) / (static_cast<double>(lag) * spacing);
}

}  // namespace

TEST(RoughnessLayer, MeasuresTheRmsSlopeItPredictsOverEveryBaseline) {
    constexpr double kSpacing = 0.02;
    const terraingen::RasterGrid grid{1500, 1500,
                                      terraingen::GeoTransform::NorthUp(0.0, 30.0, kSpacing)};
    for (const double hurst : {0.6, 0.9}) {
        terraingen::RoughnessOptions options;
        options.rmsSlope = 0.15;
        options.baseline = 0.25;
        options.hurst = hurst;
        options.maxWavelength = 2.0;
        options.minWavelength = 0.08;
        options.seed = 3;
        const terraingen::RoughnessLayer layer(options);
        auto heights = grid.MakeRaster();
        layer.Apply(heights, {kSpacing, 0});
        for (const std::size_t lag : {2u, 5u, 12u, 25u, 50u}) {
            const double baseline = static_cast<double>(lag) * kSpacing;
            EXPECT_NEAR(MeasuredRmsSlope(heights, lag, kSpacing), layer.RmsSlope(baseline),
                        0.05 * layer.RmsSlope(baseline))
                << "H " << hurst << ", baseline " << baseline << " m";
        }
    }
}
