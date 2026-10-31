// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/masked.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using terraingen::Bounds2d;
using terraingen::GeoTransform;
using terraingen::MaskedLayer;
using terraingen::RegionShape;

namespace {

/// A 200 x 120 grid at 0.5 m spacing around the origin, heights rising to the east.
terraingen::Raster<float> Slope() {
    const terraingen::RasterGrid grid{200, 120, GeoTransform::NorthUp(-50.0, 30.0, 0.5)};
    terraingen::Raster<float> heights(grid);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            heights(col, row) = static_cast<float>(0.1 * heights.CellCenter(col, row).x);
        }
    }
    return heights;
}

std::shared_ptr<const terraingen::CraterLayer> ManualCraters() {
    terraingen::CraterLayerOptions options;
    options.generateRandom = false;
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

TEST(RegionShape, MeasuresSignedDistancesToRectanglesAndCircles) {
    const auto box = RegionShape::Rectangle({-2.0, -1.0, 2.0, 1.0});
    EXPECT_EQ(box.DistanceOutside(0.0, 0.0), -1.0);
    EXPECT_EQ(box.DistanceOutside(1.5, 0.0), -0.5);
    EXPECT_EQ(box.DistanceOutside(2.0, 1.0), 0.0);
    EXPECT_EQ(box.DistanceOutside(5.0, 0.0), 3.0);
    EXPECT_EQ(box.DistanceOutside(0.0, -4.0), 3.0);
    EXPECT_DOUBLE_EQ(box.DistanceOutside(5.0, 5.0), 5.0);
    EXPECT_EQ(box.Bounds().maxX, 2.0);
    const auto disc = RegionShape::Circle({1.0, 1.0}, 2.0);
    EXPECT_EQ(disc.DistanceOutside(1.0, 2.0), -1.0);
    EXPECT_EQ(disc.DistanceOutside(1.0, 3.0), 0.0);
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

TEST(RegionShape, MeasuresSignedDistancesToAConcavePolygon) {
    // A U open to the north: 6 wide, 4 tall, a 2 wide notch 3 deep in the middle.
    const auto u = RegionShape::Polygon({{0.0, 0.0},
                                         {6.0, 0.0},
                                         {6.0, 4.0},
                                         {4.0, 4.0},
                                         {4.0, 1.0},
                                         {2.0, 1.0},
                                         {2.0, 4.0},
                                         {0.0, 4.0}});
    EXPECT_NO_THROW(terraingen::ValidateRegionShape(u));
    EXPECT_DOUBLE_EQ(u.DistanceOutside(1.0, 2.0), -1.0) << "in the west arm";
    EXPECT_DOUBLE_EQ(u.DistanceOutside(3.0, 0.5), -0.5) << "in the base under the notch";
    EXPECT_DOUBLE_EQ(u.DistanceOutside(3.0, 3.0), 1.0) << "in the notch, outside";
    EXPECT_DOUBLE_EQ(u.DistanceOutside(3.0, 2.0), 1.0);
    EXPECT_DOUBLE_EQ(u.DistanceOutside(3.0, 5.0), std::sqrt(2.0)) << "nearest the arm corners";
    EXPECT_DOUBLE_EQ(u.DistanceOutside(-3.0, 2.0), 3.0);
    EXPECT_DOUBLE_EQ(u.DistanceOutside(9.0, 8.0), 5.0) << "from the corner (6, 4)";
    EXPECT_EQ(u.DistanceOutside(4.0, 2.0), 0.0) << "on an edge";
    EXPECT_EQ(u.DistanceOutside(0.0, 0.0), 0.0) << "on a vertex";
    const Bounds2d box = u.Bounds();
    EXPECT_EQ(box.minX, 0.0);
    EXPECT_EQ(box.minY, 0.0);
    EXPECT_EQ(box.maxX, 6.0);
    EXPECT_EQ(box.maxY, 4.0);

    const auto reversed = RegionShape::Polygon({{0.0, 4.0},
                                                {2.0, 4.0},
                                                {2.0, 1.0},
                                                {4.0, 1.0},
                                                {4.0, 4.0},
                                                {6.0, 4.0},
                                                {6.0, 0.0},
                                                {0.0, 0.0}});
    for (const auto& [x, y] : {std::pair{1.0, 2.0}, {3.0, 3.0}, {3.0, 0.5}, {7.0, 7.0}}) {
        EXPECT_DOUBLE_EQ(reversed.DistanceOutside(x, y), u.DistanceOutside(x, y))
            << "winding does not matter at " << x << ", " << y;
    }
}

TEST(RegionShape, CountsAPolygonsHolesAsOutside) {
    const auto frame = RegionShape::Polygon({{-5.0, -5.0}, {5.0, -5.0}, {5.0, 5.0}, {-5.0, 5.0}},
                                            {{{-2.0, -2.0}, {2.0, -2.0}, {2.0, 2.0}, {-2.0, 2.0}}});
    EXPECT_NO_THROW(terraingen::ValidateRegionShape(frame));
    EXPECT_DOUBLE_EQ(frame.DistanceOutside(0.0, 0.0), 2.0) << "the hole's middle";
    EXPECT_DOUBLE_EQ(frame.DistanceOutside(1.5, 0.0), 0.5);
    EXPECT_DOUBLE_EQ(frame.DistanceOutside(3.0, 0.0), -1.0) << "between hole and outline";
    EXPECT_DOUBLE_EQ(frame.DistanceOutside(4.5, 0.0), -0.5);
    EXPECT_DOUBLE_EQ(frame.DistanceOutside(8.0, 0.0), 3.0);
    EXPECT_EQ(frame.DistanceOutside(2.0, 0.0), 0.0) << "on the hole's edge";
    EXPECT_EQ(frame.Bounds().minX, -5.0);
    EXPECT_EQ(frame.Bounds().maxY, 5.0);
    EXPECT_NE(frame, RegionShape::Polygon(frame.points));
    EXPECT_EQ(frame, RegionShape::Polygon(frame.points, frame.holes));
}

TEST(RegionShape, MeasuresPolygonsInTheLocalFrameOnASphere) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const auto square =
        RegionShape::Polygon({{10.0, 59.0}, {12.0, 59.0}, {12.0, 61.0}, {10.0, 61.0}});
    const auto box = RegionShape::Rectangle({10.0, 59.0, 12.0, 61.0});
    for (const auto& [x, y] : {std::pair{11.0, 60.0}, {13.0, 60.0}, {11.0, 62.0}, {11.5, 60.2}}) {
        EXPECT_NEAR(square.DistanceOutside(x, y, moon), box.DistanceOutside(x, y, moon), 1e-6)
            << x << ", " << y;
    }
    EXPECT_LT(square.DistanceOutside(371.0, 60.0, moon), 0.0) << "a turn east is the same place";
}

TEST(RegionShape, RejectsDegeneratePolygons) {
    const auto rejects = [](const RegionShape& shape, const std::string& part) {
        try {
            terraingen::ValidateRegionShape(shape);
            ADD_FAILURE() << "expected std::invalid_argument for " << part;
        } catch (const std::invalid_argument& error) {
            EXPECT_NE(std::string(error.what()).find(part), std::string::npos) << error.what();
        }
    };
    rejects(RegionShape::Polygon({}), "at least 3 points, not 0");
    rejects(RegionShape::Polygon({{0.0, 0.0}, {1.0, 0.0}}), "at least 3 points, not 2");
    rejects(RegionShape::Polygon({{0.0, 0.0}, {1.0, std::nan("")}, {0.0, 1.0}}),
            "point 1 not finite");
    rejects(RegionShape::Polygon({{0.0, 0.0}, {1.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}),
            "zero-length edge: point 2 repeats point 1");
    rejects(RegionShape::Polygon({{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {0.0, 0.0}}),
            "do not repeat the first point");
    rejects(RegionShape::Polygon({{0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}}), "no area");
    rejects(RegionShape::Polygon({{0.0, 0.0}, {4.0, 0.0}, {0.0, 4.0}},
                                 {{{1.0, 1.0}, {2.0, 1.0}, {1.0, 2.0}}, {{1.0, 1.0}, {2.0, 1.0}}}),
            "hole 1 needs at least 3 points");
    EXPECT_THROW(MaskedLayer(std::make_shared<terraingen::ScaleLayer>(1.0, 1.0),
                             RegionShape::Polygon({{0.0, 0.0}, {1.0, 0.0}})),
                 std::invalid_argument);
}

TEST(MaskedLayer, AppliesInsideAConcavePolygonAndFeathersFromItsEdges) {
    const auto inner = std::make_shared<terraingen::ScaleLayer>(1.0, 10.0);
    const MaskedLayer layer(inner,
                            RegionShape::Polygon({{-20.0, -10.0},
                                                  {20.0, -10.0},
                                                  {20.0, 10.0},
                                                  {10.0, 10.0},
                                                  {10.0, 0.0},
                                                  {-10.0, 0.0},
                                                  {-10.0, 10.0},
                                                  {-20.0, 10.0}}),
                            4.0, terraingen::BlendShape::Linear);
    EXPECT_EQ(layer.Weight(-15.0, 5.0), 1.0);
    EXPECT_EQ(layer.Weight(0.0, 8.0), 0.0) << "deep in the notch";
    EXPECT_DOUBLE_EQ(layer.Weight(0.0, 1.0), 0.75);
    EXPECT_EQ(layer.ApplyAt(1.0f, -15.0, 5.0, {0.5}), 11.0f);
    EXPECT_EQ(layer.ApplyAt(1.0f, 0.0, 8.0, {0.5}), 1.0f);
    const auto before = Slope();
    auto after = before;
    layer.Apply(after, {0.5, 4});
    for (std::size_t row = 0; row < after.Height(); ++row) {
        for (std::size_t col = 0; col < after.Width(); ++col) {
            const auto center = after.CellCenter(col, row);
            ASSERT_EQ(after(col, row), layer.ApplyAt(before(col, row), center.x, center.y, {0.5}))
                << col << ", " << row;
        }
    }
}

TEST(MaskedLayer, WeighsTheInnerLayerFullyInsideAndFeathersOut) {
    const auto inner = std::make_shared<terraingen::ScaleLayer>(1.0, 10.0);
    const MaskedLayer layer(inner, RegionShape::Rectangle({-10.0, -5.0, 10.0, 5.0}), 4.0);
    EXPECT_EQ(layer.Weight(0.0, 0.0), 1.0);
    EXPECT_EQ(layer.Weight(10.0, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(layer.Weight(12.0, 0.0), 0.5);
    EXPECT_EQ(layer.Weight(14.0, 0.0), 0.0);
    EXPECT_EQ(layer.ApplyAt(1.0f, 0.0, 0.0, {0.5}), 11.0f);
    EXPECT_EQ(layer.ApplyAt(1.0f, 12.0, 0.0, {0.5}), 6.0f);
    EXPECT_EQ(layer.ApplyAt(1.0f, 30.0, 0.0, {0.5}), 1.0f);
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {0.5})));
    const MaskedLayer hard(inner, RegionShape::Circle({0.0, 0.0}, 3.0));
    EXPECT_EQ(hard.Weight(3.0, 0.0), 1.0);
    EXPECT_EQ(hard.Weight(3.001, 0.0), 0.0);
    EXPECT_EQ(&layer.Inner(), inner.get());
}

TEST(MaskedLayer, GivesGridsThePointQueriesBitsAndLeavesTheRestAlone) {
    for (const auto& inner : std::initializer_list<std::shared_ptr<const terraingen::Layer>>{
             std::make_shared<terraingen::ClampLayer>(std::nullopt, 0.0),
             std::make_shared<terraingen::RoughnessLayer>(terraingen::RoughnessOptions{}),
             ManualCraters()}) {
        const MaskedLayer layer(inner, RegionShape::Circle({3.0, 1.0}, 12.0), 5.0,
                                terraingen::BlendShape::Linear);
        const auto before = Slope();
        auto after = before;
        layer.Apply(after, {0.5, 4});
        std::size_t changed = 0;
        for (std::size_t row = 0; row < after.Height(); ++row) {
            for (std::size_t col = 0; col < after.Width(); ++col) {
                const auto center = after.CellCenter(col, row);
                ASSERT_EQ(after(col, row),
                          layer.ApplyAt(before(col, row), center.x, center.y, {0.5}))
                    << col << ", " << row;
                if (layer.Weight(center.x, center.y) == 0.0) {
                    ASSERT_EQ(after(col, row), before(col, row));
                }
                changed += after(col, row) != before(col, row) ? 1u : 0u;
            }
        }
        EXPECT_GT(changed, 100u);
    }
}

TEST(MaskedLayer, HoldsTheFeaturesCenteredInTheRegionOrItsFeather) {
    const MaskedLayer layer(ManualCraters(), RegionShape::Rectangle({-5.0, -5.0, 5.0, 5.0}), 2.0);
    EXPECT_TRUE(layer.Holds(0.0, 0.0));
    EXPECT_TRUE(layer.Holds(6.5, 0.0)) << "inside the feather";
    EXPECT_FALSE(layer.Holds(7.5, 0.0));
}

TEST(MaskedLayer, SkipsTheInnerLayerAwayFromTheRegion) {
    auto calls = std::make_shared<std::atomic<int>>(0);
    const auto inner =
        std::make_shared<terraingen::FunctionLayer>([calls](double, double, double, double height) {
            ++*calls;
            return height + 1.0;
        });
    const MaskedLayer away(inner, RegionShape::Rectangle({500.0, 500.0, 510.0, 510.0}), 5.0);
    auto heights = Slope();
    const auto before = heights;
    away.Apply(heights, {});
    EXPECT_EQ(*calls, 0);
    EXPECT_EQ(away.ApplyAt(2.0f, 0.0, 0.0, {1.0}), 2.0f);
    EXPECT_EQ(*calls, 0);
    for (std::size_t i = 0; i < heights.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], before.Values()[i]);
    }
    const MaskedLayer small(inner, RegionShape::Rectangle({0.0, 0.0, 2.0, 2.0}));
    heights.SetNoData(-9999.0f);
    const auto hole = heights.Transform().WorldToPixel(1.0, 1.0);
    auto& missing = heights(static_cast<std::size_t>(hole.x), static_cast<std::size_t>(hole.y));
    missing = -9999.0f;
    small.Apply(heights, {});
    EXPECT_EQ(missing, -9999.0f);
    EXPECT_LE(*calls, 36) << "only the cells around the region";
    EXPECT_GE(*calls, 15) << "16 cells in the region, one without data";
}

TEST(MaskedLayer, ForwardsBaseSeedsAndDependenciesToItsInnerLayer) {
    const RegionShape box = RegionShape::Rectangle({-5.0, -5.0, 5.0, 5.0});
    EXPECT_FALSE(MaskedLayer(std::make_shared<terraingen::ScaleLayer>(2.0), box).LayerBase());
    EXPECT_EQ(MaskedLayer(std::make_shared<terraingen::ScaleLayer>(2.0), box).Reseeded(1), nullptr)
        << "an inner layer without seeds";
    const auto craters = ManualCraters();
    const MaskedLayer masked(craters, box, 2.0);
    EXPECT_EQ(masked.LayerBase()->surface, craters->LayerBase()->surface);
    const auto source = std::make_shared<terraingen::FlatSource>(8, 8, 1.0);
    const terraingen::LayerPtr rebased = masked.Rebased({terraingen::Surface{}, source});
    ASSERT_NE(rebased, nullptr);
    EXPECT_EQ(rebased->LayerBase()->source, source) << "the inner layer rebuilt on the base";
    const terraingen::LayerPtr reseeded = masked.Reseeded(3);
    ASSERT_NE(reseeded, nullptr);
    const auto& confining = dynamic_cast<const MaskedLayer&>(*reseeded);
    EXPECT_EQ(dynamic_cast<const terraingen::CraterLayer&>(confining.Inner()).Options().seed,
              terraingen::ReseedWord(3, 0));
    EXPECT_TRUE(confining.Holds(6.5, 0.0)) << "the same region";

    terraingen::BoulderLayerOptions options;
    options.ejecta.minCraterDiameter = 5.0;
    const auto boulders = std::make_shared<terraingen::BoulderLayer>(options, craters);
    const MaskedLayer rocks(boulders, box);
    EXPECT_EQ(rocks.Dependencies(), std::vector<const terraingen::Layer*>{craters.get()});
    EXPECT_EQ(rocks.Rebound({}), nullptr);
    const auto moved = ManualCraters();
    const terraingen::LayerPtr rebound = rocks.Rebound({{craters.get(), moved}});
    ASSERT_NE(rebound, nullptr);
    EXPECT_EQ(rebound->Dependencies(), std::vector<const terraingen::Layer*>{moved.get()});
}

TEST(MaskedLayer, ConfinesALayerToARegionOnTheContextsSphere) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const MaskedLayer layer(std::make_shared<terraingen::ScaleLayer>(1.0, 5.0),
                            RegionShape::Circle({-40.0, 45.0}, 1000.0), 200.0,
                            terraingen::BlendShape::Linear);
    const terraingen::Vec2d near = moon.Locate({-40.0, 45.0}, {900.0, 0.0});
    terraingen::LayerContext onMoon{1.0};
    onMoon.surface = &moon;
    EXPECT_FLOAT_EQ(layer.ApplyAt(0.0f, near.x, near.y, onMoon), 5.0f);
    EXPECT_EQ(layer.Weight(near.x, near.y, moon), 1.0);
    const terraingen::Vec2d far = moon.Locate({-40.0, 45.0}, {1300.0, 0.0});
    EXPECT_EQ(layer.ApplyAt(0.0f, far.x, far.y, onMoon), 0.0f);
    EXPECT_EQ(layer.ApplyAt(0.0f, far.x, far.y, {1.0}), 5.0f)
        << "within 1000 of the center in degrees read as meters on a plane";
}

TEST(MaskedLayer, RejectsInvalidSettings) {
    const auto inner = std::make_shared<terraingen::ScaleLayer>(2.0);
    EXPECT_THROW(MaskedLayer(nullptr, RegionShape{}), std::invalid_argument);
    EXPECT_THROW(MaskedLayer(inner, RegionShape::Circle({0.0, 0.0}, -2.0)), std::invalid_argument);
    EXPECT_THROW(MaskedLayer(inner, RegionShape{}, -1.0), std::invalid_argument);
    EXPECT_THROW(MaskedLayer(inner, RegionShape{}, std::nan("")), std::invalid_argument);
}
