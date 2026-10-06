#include "terraingen/pipeline/terrain.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

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

TEST(EvaluateAt, EqualsTheGridCellCentredThereToTheBit) {
    const Terrain terrain = Rolling();
    for (const double spacing : {2.0, 5.0}) {
        const auto grid = Evaluate(terrain, terrain.source->Grid(), spacing);
        for (std::size_t row = 0; row < grid.heights.Height(); row += 3) {
            for (std::size_t col = 0; col < grid.heights.Width(); col += 3) {
                const auto centre = grid.heights.CellCenter(col, row);
                ASSERT_EQ(terraingen::EvaluateAt(terrain, centre.x, centre.y, spacing),
                          grid.heights(col, row))
                    << "spacing " << spacing << " cell " << col << "," << row;
            }
        }
    }
}

TEST(EvaluateAt, ChangesContinuouslyWithTheSpacing) {
    Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(64, 64, 1.0);
    terraingen::CraterLayerOptions options;
    options.generate = false;
    options.generation.manual.Append({0.0, 0.0, 20.0, 0.0, 0u});
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
                                 std::make_shared<terraingen::UniformAges>(1.0),
                                 std::make_shared<terraingen::NoDegradation>(),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options));
    const float whole = terraingen::EvaluateAt(terrain, 0.0, 0.0, 1.0);
    ASSERT_LT(whole, -1.0f);
    float previous = whole;
    double largestStep = 0.0;
    for (double spacing = 1.0; spacing <= 12.0; spacing *= 1.01) {
        const float height = terraingen::EvaluateAt(terrain, 0.0, 0.0, spacing);
        largestStep = std::fmax(largestStep, std::fabs(height - previous));
        previous = height;
    }
    EXPECT_EQ(previous, 0.0f) << "under 2 samples across at the coarsest spacing";
    EXPECT_LT(largestStep, 0.03 * std::fabs(static_cast<double>(whole)))
        << "no level pops the crater in or out";
}

TEST(EvaluateAt, GivesTheSameHeightsFromManyThreads) {
    const Terrain terrain = Rolling();
    std::vector<std::array<double, 2>> points;
    for (int n = 0; n < 400; ++n) {
        points.push_back({-90.0 + 0.45 * n, 60.0 - 0.3 * n});
    }
    std::vector<float> serial;
    for (const auto& point : points) {
        serial.push_back(terraingen::EvaluateAt(terrain, point[0], point[1], 2.0));
    }
    std::vector<float> parallel(points.size());
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            for (std::size_t n = t; n < points.size(); n += 8) {
                parallel[n] = terraingen::EvaluateAt(terrain, points[n][0], points[n][1], 2.0);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(parallel, serial);
}

TEST(Evaluate, TilesEqualTheWholeTerrainAtACoarserSpacing) {
    const Terrain terrain = Rolling();
    const RasterGrid grid = terrain.source->Grid();
    const auto whole = Evaluate(terrain, grid, 7.0);
    const auto corner = grid.transform.PixelToWorld(30.0, 20.0);
    const RasterGrid tile{30, 25, GeoTransform::NorthUp(corner.x, corner.y, 2.0)};
    const auto part = Evaluate(terrain, tile, 7.0);
    for (std::size_t row = 0; row < tile.height; ++row) {
        for (std::size_t col = 0; col < tile.width; ++col) {
            ASSERT_EQ(part.heights(col, row), whole.heights(30 + col, 20 + row));
        }
    }
    EXPECT_LT(whole.features.craters.Size(), Evaluate(terrain).features.craters.Size())
        << "bands too small for 7 m are not generated";
}

TEST(EvaluateAt, RejectsFiltersBadSpacingsAndIncompleteTerrains) {
    Terrain terrain = Rolling();
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, -1.0), std::invalid_argument);
    terrain.filters.push_back(
        std::make_shared<terraingen::RimSmoothingFilter>(terraingen::RimSmoothing{}));
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, 2.0), std::invalid_argument);
    terrain.filters.clear();
    terrain.layers.push_back(nullptr);
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, 2.0), std::invalid_argument);
    terrain.source.reset();
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, 2.0), std::invalid_argument);
}

namespace {

/// A layer with only `Apply`, so `ApplyAt` is the default one-cell evaluation.
class Bump final : public terraingen::Layer {
  public:
    void Apply(terraingen::Raster<float>& heights, double spacing,
               terraingen::TerrainFeatures& /*features*/) const override {
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const auto centre = heights.CellCenter(col, row);
                heights(col, row) += static_cast<float>(centre.x * 0.5 + centre.y + spacing);
            }
        }
    }
};

}  // namespace

TEST(Layer, DefaultApplyAtEvaluatesOneCellCentredOnThePoint) {
    const Bump bump;
    EXPECT_EQ(bump.ApplyAt(1.0f, 4.0, -2.0, 3.0), 1.0f + 2.0f - 2.0f + 3.0f);
    EXPECT_THROW(bump.ApplyAt(0.0f, 0.0, 0.0, 0.0), std::invalid_argument);
}

namespace {

/// `Rolling` with rim smoothing after the layers.
Terrain Smoothed() {
    Terrain terrain = Rolling();
    terrain.filters.push_back(
        std::make_shared<terraingen::RimSmoothingFilter>(terraingen::RimSmoothing{}));
    return terrain;
}

void ExpectSameHeights(const terraingen::Raster<float>& a, const terraingen::Raster<float>& b) {
    ASSERT_EQ(a.Width(), b.Width());
    ASSERT_EQ(a.Height(), b.Height());
    for (std::size_t i = 0; i < a.Size(); ++i) {
        ASSERT_EQ(a.Values()[i], b.Values()[i]) << i;
    }
}

}  // namespace

TEST(Evaluate, GivesTheSameBitsOnAnyNumberOfThreads) {
    const Terrain terrain = Smoothed();
    const RasterGrid grid = terrain.source->Grid();
    const auto serial = Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 1});
    for (const unsigned threads : {2u, 8u, 0u}) {
        const auto parallel = Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, threads});
        ExpectSameHeights(parallel.heights, serial.heights);
        EXPECT_EQ(parallel.features.craters.Ids(), serial.features.craters.Ids());
    }
    EXPECT_THROW(Evaluate(terrain, grid, terraingen::EvaluationOptions{-1.0, 1}),
                 std::invalid_argument);
}

TEST(EvaluateTile, EqualsTheSameCellsOfALargerGridWithFilters) {
    const Terrain terrain = Smoothed();
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    const double margin = terraingen::FilterMargin(terrain, 2.0);
    EXPECT_DOUBLE_EQ(margin, (1.2 + 3.0 * 0.05) * 25.0 + 4.0);
    const RasterGrid large{200, 160, GeoTransform::NorthUp(-200.0, 160.0, 2.0)};
    const auto whole = Evaluate(terrain, large);
    for (const terraingen::TileKey key :
         {terraingen::TileKey{2, 1, 1}, terraingen::TileKey{2, 2, 0},
          terraingen::TileKey{2, 0, 2}}) {
        const auto tile = terraingen::EvaluateTile(terrain, scheme, key, 4);
        const RasterGrid grid = scheme.Grid(key);
        const auto offset =
            large.transform.WorldToPixel(grid.transform.originX, grid.transform.originY);
        const auto col0 = static_cast<std::size_t>(offset.x);
        const auto row0 = static_cast<std::size_t>(offset.y);
        for (std::size_t row = 0; row < grid.height; ++row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                ASSERT_EQ(tile.heights(col, row), whole.heights(col0 + col, row0 + row))
                    << "tile " << key.x << "," << key.y << " cell " << col << "," << row;
            }
        }
    }
}

TEST(EvaluateTile, MatchesGridsEvaluatedAtEachLevelsSpacing) {
    const Terrain terrain = Rolling();
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    for (const int level : {0, 1, 2, 3}) {
        const terraingen::TileKey key{level, 0, 0};
        const auto tile = terraingen::EvaluateTile(terrain, scheme, key);
        const auto grid = Evaluate(terrain, scheme.Grid(key));
        ExpectSameHeights(tile.heights, grid.heights);
    }
    EXPECT_EQ(terraingen::FilterMargin(terrain, 2.0), 0.0);
}

TEST(EvaluateTiles, EvaluatesEveryTileInOrder) {
    const Terrain terrain = Smoothed();
    const terraingen::TileScheme scheme{{-96.0, 64.0}, 128.0, 16};
    const auto keys = scheme.TilesCovering({-96.0, 0.0, 0.0, 64.0}, 2);
    const auto tiles = terraingen::EvaluateTiles(terrain, scheme, keys, 4);
    ASSERT_EQ(tiles.size(), keys.size());
    for (std::size_t n = 0; n < keys.size(); ++n) {
        ExpectSameHeights(tiles[n].heights,
                          terraingen::EvaluateTile(terrain, scheme, keys[n]).heights);
    }
    EXPECT_THROW(
        terraingen::EvaluateTile(terrain, terraingen::TileScheme{{0.0, 0.0}, -1.0, 16}, {0, 0, 0}),
        std::invalid_argument);
}

TEST(Layer, DefaultsRunApplyAndPlaceNoFeatures) {
    Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(8, 8, 1.0);
    terrain.layers.push_back(std::make_shared<Bump>());
    const auto parallel =
        Evaluate(terrain, terrain.source->Grid(), terraingen::EvaluationOptions{0.0, 4});
    ExpectSameHeights(parallel.heights, Evaluate(terrain).heights);
    EXPECT_EQ(Bump().MaxFeatureDiameter(), 0.0);
}
