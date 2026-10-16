#include "terraingen/filters/gaussian_blur_filter.h"

#include "terraingen/raster/gaussian_blur.h"
#include "terraingen/raster/geo_transform.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

using terraingen::GaussianBlurFilter;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::TerrainFeatures;

namespace {

/// A ridge along x = 0 on a grid of `cell` metre cells over [-32, 32] m.
Raster<float> Ridge(double cell) {
    const auto size = static_cast<std::size_t>(64.0 / cell);
    Raster<float> heights(size, size, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(-32.0, 32.0, cell));
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t col = 0; col < size; ++col) {
            const double x = heights.CellCenter(col, row).x;
            heights(col, row) = static_cast<float>(std::exp(-x * x / 4.0));
        }
    }
    return heights;
}

}  // namespace

TEST(GaussianBlurFilter, BlursBySigmaInMetresAtAnyCellSize) {
    const GaussianBlurFilter filter(2.0);
    Raster<float> heights = Ridge(1.0);
    const Raster<float> expected = terraingen::GaussianBlur(heights, 2.0);
    filter.Apply(heights, {1.0, 2}, TerrainFeatures{});
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], expected.Values()[i]) << i;
    }
    Raster<float> fine = Ridge(0.5);
    filter.Apply(fine, {0.5}, TerrainFeatures{});
    EXPECT_NEAR(fine(63, 32), heights(31, 16), 0.02) << "the same ridge after a 2 m blur";
    EXPECT_EQ(filter.Sigma(), 2.0);
}

TEST(GaussianBlurFilter, ReadsThreeSigmasAroundEachCellAndRejectsBadSettings) {
    EXPECT_EQ(GaussianBlurFilter(2.0).Margin(500.0, 0.5), 7.0);
    EXPECT_EQ(GaussianBlurFilter(0.0).Margin(500.0, 0.5), 0.0);
    Raster<float> heights = Ridge(1.0);
    const Raster<float> original = heights;
    GaussianBlurFilter(0.0).Apply(heights, {1.0}, TerrainFeatures{});
    EXPECT_TRUE(
        std::equal(heights.Values().begin(), heights.Values().end(), original.Values().begin()));
    EXPECT_THROW(GaussianBlurFilter(-1.0), std::invalid_argument);
    EXPECT_THROW(GaussianBlurFilter(std::numeric_limits<double>::infinity()),
                 std::invalid_argument);
    EXPECT_THROW(GaussianBlurFilter(1.0, terraingen::Surface::Sphere(1737400.0)),
                 std::invalid_argument);
}
