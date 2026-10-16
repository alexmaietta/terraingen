#include "terraingen/layers/region.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::Bounds2d;
using terraingen::GeoTransform;
using terraingen::RegionLayer;
using terraingen::RegionShape;

namespace {

/// A 200 x 120 grid at 0.5 m spacing around the origin, heights rising to the east.
terraingen::Raster<float> Slope() {
    const terraingen::RasterGrid grid{200, 120, GeoTransform::NorthUp(-50.0, 30.0, 0.5)};
    auto heights = grid.MakeRaster();
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            heights(col, row) = static_cast<float>(0.1 * heights.CellCenter(col, row).x);
        }
    }
    return heights;
}

std::shared_ptr<const terraingen::CraterLayer> ManualCraters() {
    terraingen::CraterLayerOptions options;
    options.generate = false;
    for (const auto& [x, y] : {std::pair{0.0, 0.0}, {16.0, 0.0}, {40.0, 20.0}}) {
        options.generation.manual.Append({x, y, 8.0, 0.0});
    }
    return std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::FreshAges>(),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
}

}  // namespace

TEST(RegionShape, MeasuresDistancesOutsideRectanglesAndCircles) {
    const auto box = RegionShape::Rectangle({-2.0, -1.0, 2.0, 1.0});
    EXPECT_EQ(box.DistanceOutside(0.0, 0.0), 0.0);
    EXPECT_EQ(box.DistanceOutside(2.0, 1.0), 0.0);
    EXPECT_EQ(box.DistanceOutside(5.0, 0.0), 3.0);
    EXPECT_EQ(box.DistanceOutside(0.0, -4.0), 3.0);
    EXPECT_DOUBLE_EQ(box.DistanceOutside(5.0, 5.0), 5.0);
    EXPECT_EQ(box.Bounds().maxX, 2.0);
    const auto disc = RegionShape::Circle({1.0, 1.0}, 2.0);
    EXPECT_EQ(disc.DistanceOutside(1.0, 2.0), 0.0);
    EXPECT_DOUBLE_EQ(disc.DistanceOutside(1.0, 6.0), 3.0);
    EXPECT_EQ(disc.Bounds().minX, -1.0);
    EXPECT_EQ(disc.Bounds().maxY, 3.0);
    EXPECT_NO_THROW(terraingen::ValidateRegionShape(box));
    EXPECT_THROW(terraingen::ValidateRegionShape(RegionShape::Rectangle({1.0, 0.0, 0.0, 1.0})),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateRegionShape(RegionShape::Circle({0.0, 0.0}, -1.0)),
                 std::invalid_argument);
    EXPECT_THROW(terraingen::ValidateRegionShape(RegionShape::Circle({std::nan(""), 0.0}, 1.0)),
                 std::invalid_argument);
}

TEST(RegionLayer, WeighsTheInnerLayerFullyInsideAndFeathersOut) {
    const auto inner = std::make_shared<terraingen::ScaleLayer>(1.0, 10.0);
    const RegionLayer layer(inner, RegionShape::Rectangle({-10.0, -5.0, 10.0, 5.0}), 4.0);
    EXPECT_EQ(layer.Weight(0.0, 0.0), 1.0);
    EXPECT_EQ(layer.Weight(10.0, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(layer.Weight(12.0, 0.0), 0.5);
    EXPECT_EQ(layer.Weight(14.0, 0.0), 0.0);
    EXPECT_EQ(layer.ApplyAt(1.0f, 0.0, 0.0, {0.5}), 11.0f);
    EXPECT_EQ(layer.ApplyAt(1.0f, 12.0, 0.0, {0.5}), 6.0f);
    EXPECT_EQ(layer.ApplyAt(1.0f, 30.0, 0.0, {0.5}), 1.0f);
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {0.5})));
    const RegionLayer hard(inner, RegionShape::Circle({0.0, 0.0}, 3.0));
    EXPECT_EQ(hard.Weight(3.0, 0.0), 1.0);
    EXPECT_EQ(hard.Weight(3.001, 0.0), 0.0);
    EXPECT_EQ(&layer.Inner(), inner.get());
}

TEST(RegionLayer, GivesGridsThePointQueriesBitsAndLeavesTheRestAlone) {
    for (const auto& inner : std::initializer_list<std::shared_ptr<const terraingen::Layer>>{
             std::make_shared<terraingen::ClampLayer>(std::nullopt, 0.0),
             std::make_shared<terraingen::RoughnessLayer>(terraingen::RoughnessOptions{}),
             ManualCraters()}) {
        const RegionLayer layer(inner, RegionShape::Circle({3.0, 1.0}, 12.0), 5.0,
                                terraingen::BlendShape::Linear);
        const auto before = Slope();
        auto after = before;
        layer.Apply(after, {0.5, 4});
        std::size_t changed = 0;
        for (std::size_t row = 0; row < after.Height(); ++row) {
            for (std::size_t col = 0; col < after.Width(); ++col) {
                const auto centre = after.CellCenter(col, row);
                ASSERT_EQ(after(col, row),
                          layer.ApplyAt(before(col, row), centre.x, centre.y, {0.5}))
                    << col << ", " << row;
                if (layer.Weight(centre.x, centre.y) == 0.0) {
                    ASSERT_EQ(after(col, row), before(col, row));
                }
                changed += after(col, row) != before(col, row) ? 1u : 0u;
            }
        }
        EXPECT_GT(changed, 100u);
    }
}

TEST(RegionLayer, HoldsTheFeaturesCentredInTheRegionOrItsFeather) {
    const RegionLayer layer(ManualCraters(), RegionShape::Rectangle({-5.0, -5.0, 5.0, 5.0}), 2.0);
    EXPECT_TRUE(layer.Holds(0.0, 0.0));
    EXPECT_TRUE(layer.Holds(6.5, 0.0)) << "inside the feather";
    EXPECT_FALSE(layer.Holds(7.5, 0.0));
    const terraingen::RasterGrid grid = terraingen::RasterGrid::Of(Slope());
    const auto inner = layer.InnerGrid(grid);
    ASSERT_TRUE(inner.has_value());
    const auto bounds = inner->Bounds();
    EXPECT_LE(bounds.minX, -7.0);
    EXPECT_GE(bounds.maxX, 7.0);
    EXPECT_LT(inner->width, grid.width) << "only the cells near the region";
    EXPECT_EQ(inner->transform.pixelWidth, grid.transform.pixelWidth);
    const RegionLayer away(ManualCraters(), RegionShape::Rectangle({500.0, 500.0, 510.0, 510.0}));
    EXPECT_FALSE(away.InnerGrid(grid).has_value());
}

TEST(RegionLayer, SkipsTheInnerLayerAwayFromTheRegion) {
    auto calls = std::make_shared<std::atomic<int>>(0);
    const auto inner =
        std::make_shared<terraingen::FunctionLayer>([calls](double, double, double, double height) {
            ++*calls;
            return height + 1.0;
        });
    const RegionLayer away(inner, RegionShape::Rectangle({500.0, 500.0, 510.0, 510.0}), 5.0);
    auto heights = Slope();
    const auto before = heights;
    away.Apply(heights, {});
    EXPECT_EQ(*calls, 0);
    EXPECT_EQ(away.ApplyAt(2.0f, 0.0, 0.0, {1.0}), 2.0f);
    EXPECT_EQ(*calls, 0);
    for (std::size_t i = 0; i < heights.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], before.Values()[i]);
    }
    const RegionLayer small(inner, RegionShape::Rectangle({0.0, 0.0, 2.0, 2.0}));
    heights.SetNoData(-9999.0f);
    const auto hole = heights.Transform().WorldToPixel(1.0, 1.0);
    auto& missing = heights(static_cast<std::size_t>(hole.x), static_cast<std::size_t>(hole.y));
    missing = -9999.0f;
    small.Apply(heights, {});
    EXPECT_EQ(missing, -9999.0f);
    EXPECT_LE(*calls, 36) << "only the cells around the region";
    EXPECT_GE(*calls, 15) << "16 cells in the region, one without data";
}

TEST(RegionLayer, RejectsInvalidSettings) {
    const auto inner = std::make_shared<terraingen::ScaleLayer>(2.0);
    EXPECT_THROW(RegionLayer(nullptr, RegionShape{}), std::invalid_argument);
    EXPECT_THROW(RegionLayer(inner, RegionShape::Circle({0.0, 0.0}, -2.0)), std::invalid_argument);
    EXPECT_THROW(RegionLayer(inner, RegionShape{}, -1.0), std::invalid_argument);
    EXPECT_THROW(RegionLayer(inner, RegionShape{}, std::nan("")), std::invalid_argument);
}
