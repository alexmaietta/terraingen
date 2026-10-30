// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source.h"

#include "terraingen/raster/geo_transform.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/sources/sample_block.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::SampleBlock;
using terraingen::Span;
using terraingen::Vec2d;

namespace {

/// Heights x + 10 y east of x = 0, no data west of it, counting the grid and point blocks.
class HalfRamp final : public terraingen::Source {
  public:
    RasterGrid Grid() const override {
        return {4, 2, GeoTransform::NorthUp(-2.0, 2.0, 1.0)};
    }

    mutable int grids = 0;
    mutable int points = 0;

  protected:
    void FillSamples(SampleBlock& samples, double /*spacing*/,
                     unsigned /*threads*/) const override {
        ++(samples.IsGrid() ? grids : points);
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const Vec2d p = samples.Position(i);
            samples.Height(i) = p.x < 0.0 ? std::numeric_limits<float>::quiet_NaN()
                                          : static_cast<float>(p.x + 10.0 * p.y);
        }
    }
};

}  // namespace

TEST(SourceSampleBlock, GivesItsPointsHeightsAndPositions) {
    const std::vector<Vec2d> points{{1.0, 2.0}, {3.0, 4.0}};
    std::vector<float> heights(2, 0.0f);
    const SampleBlock samples{Span<const Vec2d>(points), Span<float>(heights)};
    EXPECT_EQ(samples.Points().size(), 2u);
    EXPECT_EQ(samples.Heights().data(), heights.data());
    EXPECT_EQ(samples.Positions()[1].y, 4.0);

    Raster<float> grid(2, 2, 0.0f);
    grid.SetTransform(GeoTransform::NorthUp(0.0, 2.0, 1.0));
    const SampleBlock cells(grid);
    EXPECT_TRUE(cells.Points().empty());
    EXPECT_EQ(cells.Heights().size(), 4u);
    const std::vector<Vec2d> centers = cells.Positions();
    ASSERT_EQ(centers.size(), 4u);
    EXPECT_EQ(centers[1].x, 1.5);
    EXPECT_EQ(centers[2].y, 0.5);
}

TEST(Source, AnswersGridsAndPointsThroughOneMethodToTheSameBits) {
    const HalfRamp ramp;
    Raster<float> heights = ramp.Grid().MakeRaster();
    ramp.Fill(heights);
    EXPECT_EQ(ramp.grids, 1);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const Vec2d center = heights.CellCenter(col, row);
            const float point = ramp.HeightAt(center.x, center.y, 1.0);
            EXPECT_TRUE(std::isnan(point) ? std::isnan(heights(col, row))
                                          : point == heights(col, row));
        }
    }
    EXPECT_EQ(ramp.points, 8);
    ASSERT_TRUE(heights.NoData().has_value()) << "NaN cells mark the raster's no-data value";
    EXPECT_TRUE(std::isnan(*heights.NoData()));
}

TEST(Source, KeepsAGridsNoDataValue) {
    const HalfRamp ramp;
    Raster<float> heights = ramp.Grid().MakeRaster();
    heights.SetNoData(-9999.0f);
    ramp.Fill(heights, 1.0, 1);
    EXPECT_EQ(heights(0, 0), -9999.0f);
    EXPECT_EQ(heights.NoData(), -9999.0f);
}

TEST(Source, HeightsAtNeedOneHeightPerPoint) {
    const terraingen::FlatSource flat(2, 2, 1.0, 3.0f);
    const std::vector<Vec2d> points{{0.0, 0.0}, {5.0, 5.0}};
    std::vector<float> heights(2);
    flat.HeightsAt(Span<const Vec2d>(points), Span<float>(heights), 1.0);
    EXPECT_EQ(heights[1], 3.0f);
    std::vector<float> one(1);
    EXPECT_THROW(flat.HeightsAt(Span<const Vec2d>(points), Span<float>(one), 1.0),
                 std::invalid_argument);
}
