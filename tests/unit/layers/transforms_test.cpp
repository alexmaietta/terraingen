#include "terraingen/layers/transforms.h"

#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::ClampLayer;
using terraingen::FunctionLayer;
using terraingen::ScaleLayer;

namespace {

/// A 6 x 4 grid of heights -2.5, -1.5, ... with one no-data cell.
terraingen::Raster<float> Ramp() {
    const terraingen::RasterGrid grid{6, 4, terraingen::GeoTransform::NorthUp(10.0, 20.0, 0.5)};
    auto heights = grid.MakeRaster();
    for (std::size_t i = 0; i < heights.Size(); ++i) {
        heights.Values()[i] = static_cast<float>(i) - 2.5f;
    }
    heights.SetNoData(-9999.0f);
    heights(3, 2) = -9999.0f;
    return heights;
}

/// Checks that `layer` changes every cell of `Ramp` as its point query does and leaves no-data.
void ExpectGridMatchesPoints(const terraingen::Layer& layer) {
    const auto before = Ramp();
    auto after = before;
    terraingen::TerrainFeatures features;
    layer.Apply(after, {0.0, 3}, features);
    for (std::size_t row = 0; row < after.Height(); ++row) {
        for (std::size_t col = 0; col < after.Width(); ++col) {
            if (before.IsNoData(before(col, row))) {
                EXPECT_EQ(after(col, row), -9999.0f);
                continue;
            }
            const auto centre = after.CellCenter(col, row);
            EXPECT_EQ(after(col, row), layer.ApplyAt(before(col, row), centre.x, centre.y, {0.5}));
        }
    }
}

}  // namespace

TEST(ScaleLayer, ScalesAndShiftsHeights) {
    const ScaleLayer layer(2.0, -1.0);
    EXPECT_EQ(layer.ApplyAt(3.0f, 0.0, 0.0, {}), 5.0f);
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {})));
    EXPECT_EQ(layer.Scale(), 2.0);
    EXPECT_EQ(layer.Offset(), -1.0);
    ExpectGridMatchesPoints(layer);
    EXPECT_THROW(ScaleLayer(std::numeric_limits<double>::infinity()), std::invalid_argument);
    EXPECT_THROW(ScaleLayer(1.0, std::nan("")), std::invalid_argument);
}

TEST(ClampLayer, ClampsToEitherOrBothBounds) {
    const ClampLayer both(-1.0, 2.0);
    EXPECT_EQ(both.ApplyAt(-3.0f, 0.0, 0.0, {}), -1.0f);
    EXPECT_EQ(both.ApplyAt(0.5f, 0.0, 0.0, {}), 0.5f);
    EXPECT_EQ(both.ApplyAt(7.0f, 0.0, 0.0, {}), 2.0f);
    EXPECT_EQ(ClampLayer(std::nullopt, 2.0).ApplyAt(-30.0f, 0.0, 0.0, {}), -30.0f);
    EXPECT_EQ(ClampLayer(1.0, std::nullopt).ApplyAt(30.0f, 0.0, 0.0, {}), 30.0f);
    EXPECT_EQ(ClampLayer(1.0, std::nullopt).ApplyAt(-30.0f, 0.0, 0.0, {}), 1.0f);
    EXPECT_TRUE(std::isnan(both.ApplyAt(std::nanf(""), 0.0, 0.0, {})));
    EXPECT_EQ(both.Min(), -1.0);
    EXPECT_EQ(both.Max(), 2.0);
    ExpectGridMatchesPoints(both);
    EXPECT_THROW(ClampLayer(3.0, 2.0), std::invalid_argument);
    EXPECT_THROW(ClampLayer(std::nan(""), std::nullopt), std::invalid_argument);
    EXPECT_THROW(ClampLayer(std::nullopt, std::numeric_limits<double>::infinity()),
                 std::invalid_argument);
}

TEST(FunctionLayer, CallsTheFunctionAtCellCentresWithTheSpacing) {
    const FunctionLayer layer(
        [](double x, double y, double spacing, double height) { return height + x - y * spacing; });
    EXPECT_EQ(layer.ApplyAt(1.0f, 2.0, 4.0, {0.25}), 2.0f);
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {1.0})));
    EXPECT_THROW(layer.ApplyAt(1.0f, 0.0, 0.0, {0.0}), std::invalid_argument);
    ExpectGridMatchesPoints(layer);
    EXPECT_THROW(FunctionLayer(nullptr), std::invalid_argument);
}
