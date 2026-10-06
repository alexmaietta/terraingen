#include "terraingen/pipeline/terrain.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::Evaluate;
using terraingen::GeoTransform;
using terraingen::RasterGrid;
using terraingen::Terrain;

namespace {

/// A flat source, rolling noise, then additive craters.
Terrain Rolling() {
    Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(96, 64, 2.0, 5.0f);
    terraingen::SyntheticTerrainParams noise;
    noise.layers[0].fractal.wavelength = 80.0f;
    noise.layers[0].fractal.amplitude = 6.0f;
    terrain.layers.push_back(std::make_shared<terraingen::NoiseRelief>(noise));
    terraingen::CraterLayerOptions options;
    options.generation.dMin = 6.0;
    options.generation.dMax = 50.0;
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
                                 std::make_shared<terraingen::UniformAges>(1.0),
                                 std::make_shared<terraingen::NoDegradation>(),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options));
    return terrain;
}

}  // namespace

TEST(Evaluate, RunsTheSourceThenLayersOverTheSourcesGrid) {
    const Terrain terrain = Rolling();
    const auto whole = Evaluate(terrain);
    EXPECT_EQ(RasterGrid::Of(whole.heights), terrain.source->Grid());
    EXPECT_GT(whole.features.craters.Size(), 0u);
    const auto again = Evaluate(terrain, terrain.source->Grid(), 1.0);
    for (std::size_t i = 0; i < whole.heights.Size(); ++i) {
        ASSERT_EQ(again.heights.Values()[i], whole.heights.Values()[i])
            << "a spacing finer than the cells is raised to them";
    }
}

TEST(Evaluate, TilesEqualTheWholeTerrain) {
    const Terrain terrain = Rolling();
    const auto whole = Evaluate(terrain);
    const RasterGrid grid = terrain.source->Grid();
    for (const auto& [col0, row0, width, height] :
         {std::array<std::size_t, 4>{0, 0, 40, 30}, std::array<std::size_t, 4>{40, 0, 56, 30},
          std::array<std::size_t, 4>{0, 30, 96, 34}, std::array<std::size_t, 4>{17, 11, 23, 29}}) {
        const auto corner =
            grid.transform.PixelToWorld(static_cast<double>(col0), static_cast<double>(row0));
        const RasterGrid tile{width, height, GeoTransform::NorthUp(corner.x, corner.y, 2.0)};
        const auto part = Evaluate(terrain, tile);
        for (std::size_t row = 0; row < height; ++row) {
            for (std::size_t col = 0; col < width; ++col) {
                ASSERT_EQ(part.heights(col, row), whole.heights(col0 + col, row0 + row))
                    << "tile at " << col0 << "," << row0 << " cell " << col << "," << row;
            }
        }
    }
}

TEST(Evaluate, RunsFiltersAfterEveryLayerWithTheirFeatures) {
    Terrain terrain = Rolling();
    terrain.filters.push_back(
        std::make_shared<terraingen::RimSmoothingFilter>(terraingen::RimSmoothing{}));
    const auto smoothed = Evaluate(terrain);
    terrain.filters.clear();
    auto expected = Evaluate(terrain);
    terraingen::SmoothCraterRims(expected.heights, expected.features.craters, {});
    for (std::size_t i = 0; i < expected.heights.Size(); ++i) {
        ASSERT_EQ(smoothed.heights.Values()[i], expected.heights.Values()[i]) << i;
    }
}

TEST(Evaluate, RejectsIncompleteTerrainsAndBadSpacings) {
    Terrain terrain = Rolling();
    const RasterGrid grid = terrain.source->Grid();
    EXPECT_THROW(Evaluate(terrain, grid, -1.0), std::invalid_argument);
    EXPECT_THROW(Evaluate(terrain, grid, std::numeric_limits<double>::quiet_NaN()),
                 std::invalid_argument);
    terrain.layers.push_back(nullptr);
    EXPECT_THROW(Evaluate(terrain), std::invalid_argument);
    terrain = Rolling();
    terrain.filters.push_back(nullptr);
    EXPECT_THROW(Evaluate(terrain), std::invalid_argument);
    terrain.source.reset();
    EXPECT_THROW(Evaluate(terrain), std::invalid_argument);
    EXPECT_THROW(Evaluate(terrain, grid), std::invalid_argument);
}
