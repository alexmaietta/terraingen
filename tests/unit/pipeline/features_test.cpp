#include "terraingen/pipeline/features.h"

#include "terraingen/terraingen.h"
#include "tests/unit/pipeline/terrains.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using terraingen::RasterGrid;
using terraingen::RegionShape;
using terraingen::Terrain;
using terraingen::testing::PowerLawCraterModels;

namespace {

/// A crater layer of only manual 8 m craters at `centres`.
std::shared_ptr<const terraingen::CraterLayer>
ManualCraters(const std::vector<terraingen::Vec2d>& centres) {
    terraingen::CraterLayerOptions options;
    options.generate = false;
    for (const auto& centre : centres) {
        options.generation.manual.Append({centre.x, centre.y, 8.0, 0.0});
    }
    return std::make_shared<terraingen::CraterLayer>(PowerLawCraterModels(), options);
}

/// A 100 x 80 flat source of 1 m cells at 2 m.
Terrain Flat() {
    Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(100, 80, 1.0, 2.0f);
    return terrain;
}

/// Boulders of 1 m and more, every one a model.
terraingen::BoulderLayerOptions Models() {
    terraingen::BoulderLayerOptions options;
    options.sizes.minDiameter = 1.0;
    options.sizes.coverage = 0.3;
    options.rendering.models.minDiameter = 1.0;
    return options;
}

}  // namespace

TEST(PlacedCraters, ListsEachCraterLayerInOrderWithItsAttributes) {
    Terrain terrain = Flat();
    const auto first = ManualCraters({{0.0, 0.0}, {10.0, 5.0}});
    const auto second = ManualCraters({{-20.0, -10.0}});
    terrain.layers = {first, std::make_shared<terraingen::ScaleLayer>(2.0), second};
    const RasterGrid grid = terrain.source->Grid();
    const auto placed = terraingen::PlacedCraters(terrain, grid);
    ASSERT_EQ(placed.craters.Size(), 3u);
    ASSERT_EQ(placed.attributes.size(), 3u);
    EXPECT_EQ(placed.craters.At(1).x, 10.0);
    EXPECT_EQ(placed.craters.At(2).x, -20.0);
    EXPECT_EQ(placed.attributes[2], second->Attributes(placed.craters.At(2)));
    EXPECT_EQ(placed.craters.Ids(),
              terraingen::PlacedCraters(terrain.layers, grid, 1.0).craters.Ids());
}

TEST(PlacedCraters, KeepsOnlyTheCratersARegionHolds) {
    Terrain terrain = Flat();
    terrain.layers = {std::make_shared<terraingen::RegionLayer>(
        ManualCraters({{0.0, 0.0}, {16.0, 0.0}, {30.0, 20.0}}),
        RegionShape::Rectangle({-5.0, -5.0, 5.0, 5.0}), 12.0)};
    const auto placed = terraingen::PlacedCraters(terrain, terrain.source->Grid());
    ASSERT_EQ(placed.craters.Size(), 2u) << "the crater 11 m off is inside a 12 m feather";
    EXPECT_EQ(placed.craters.At(1).x, 16.0);
    terrain.layers = {std::make_shared<terraingen::RegionLayer>(
        ManualCraters({{0.0, 0.0}}), RegionShape::Rectangle({500.0, 500.0, 510.0, 510.0}))};
    EXPECT_TRUE(terraingen::PlacedCraters(terrain, terrain.source->Grid()).craters.Empty());
}

TEST(PlacedCraters, ListsAtASpacingFinerThanTheGrid) {
    Terrain terrain = Flat();
    terraingen::CraterLayerOptions options;
    options.generation.dMin = 0.5;
    options.generation.dMax = 20.0;
    terrain.layers = {std::make_shared<terraingen::CraterLayer>(PowerLawCraterModels(), options)};
    const RasterGrid grid = terrain.source->Grid();
    const auto atGrid = terraingen::PlacedCraters(terrain, grid);
    const auto finer = terraingen::PlacedCraters(terrain, grid, 0.25);
    EXPECT_GT(finer.craters.Size(), atGrid.craters.Size())
        << "craters too small for the grid but not for the spacing";
    double smallest = 1e9;
    for (const double diameter : finer.craters.Diameters()) {
        smallest = std::fmin(smallest, diameter);
    }
    EXPECT_LT(smallest, 2.0) << "below two of the grid's 1 m cells";
}

TEST(PlacedBoulders, ListsEveryBoulderOfTheSizesHoweverSmall) {
    Terrain terrain = Flat();
    terraingen::BoulderLayerOptions options;
    options.sizes.minDiameter = 0.2;
    options.sizes.coverage = 0.1;
    const auto layer = std::make_shared<terraingen::BoulderLayer>(options);
    terrain.layers = {layer};
    const RasterGrid grid = terrain.source->Grid();
    const auto placed = terraingen::PlacedBoulders(terrain, grid);
    EXPECT_EQ(placed.boulders.size(), layer->Boulders(grid.Bounds()).size());
    std::size_t small = 0;
    for (const auto& boulder : placed.boulders) {
        small += boulder.diameter < 2.0 ? 1 : 0;
    }
    EXPECT_GT(small, 0u) << "boulders the 1 m grid cannot draw stay in the list";
    EXPECT_EQ(placed.boulders.size(),
              terraingen::PlacedBoulders(terrain, grid, 4.0).boulders.size())
        << "the spacing does not decide which boulders exist";
}

TEST(PlacedBoulders, PlacesModelsOnTheGroundBeneathTheirLayer) {
    Terrain terrain = Flat();
    terrain.layers = {std::make_shared<terraingen::ScaleLayer>(1.0, 3.0),
                      std::make_shared<terraingen::BoulderLayer>(Models())};
    const auto placed = terraingen::PlacedBoulders(terrain, terrain.source->Grid());
    ASSERT_FALSE(placed.boulders.empty());
    ASSERT_EQ(placed.models.size(), placed.boulders.size());
    for (const auto& model : placed.models) {
        ASSERT_TRUE(model.boulder.ground.has_value());
        EXPECT_FLOAT_EQ(static_cast<float>(*model.boulder.ground), 5.0f)
            << "the source and the layers before the boulders";
    }
}

TEST(PlacedBoulders, KeepsOnlyTheBouldersARegionHolds) {
    Terrain terrain = Flat();
    terrain.layers = {std::make_shared<terraingen::RegionLayer>(
        std::make_shared<terraingen::BoulderLayer>(Models()),
        RegionShape::Circle({0.0, 0.0}, 35.0))};
    const auto placed = terraingen::PlacedBoulders(terrain, terrain.source->Grid());
    ASSERT_FALSE(placed.boulders.empty());
    ASSERT_FALSE(placed.models.empty());
    for (const auto& boulder : placed.boulders) {
        EXPECT_LE(std::hypot(boulder.x, boulder.y), 35.0);
    }
    for (const auto& model : placed.models) {
        EXPECT_LE(std::hypot(model.boulder.x, model.boulder.y), 35.0);
    }
}
