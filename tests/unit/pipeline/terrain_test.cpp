#include "terraingen/pipeline/terrain.h"

#include "support/raster_equality.h"
#include "terraingen/terraingen.h"
#include "terrains.h"

#include <gtest/gtest.h>

#include <algorithm>

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
using terraingen::testing::Datum;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::Rolling;
using terraingen::testing::Smoothed;

TEST(Evaluate, RunsTheSourceThenLayersOverTheSourcesGrid) {
    const Terrain terrain = Rolling();
    const auto whole = Evaluate(terrain);
    EXPECT_EQ(RasterGrid::FromRaster(whole), terrain.source->Grid());
    EXPECT_GT(terraingen::PlacedCraters(terrain, RasterGrid::FromRaster(whole)).craters.Size(), 0u);
    const auto again = Evaluate(terrain, terrain.source->Grid(), 1.0);
    for (std::size_t i = 0; i < whole.Size(); ++i) {
        ASSERT_EQ(again.Values()[i], whole.Values()[i])
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
                ASSERT_EQ(part(col, row), whole(col0 + col, row0 + row))
                    << "tile at " << col0 << "," << row0 << " cell " << col << "," << row;
            }
        }
    }
}

TEST(Evaluate, RoundsCraterRimsAndStillAnswersPointQueries) {
    const Terrain smoothed = terraingen::testing::Smoothed();
    const auto rounded = Evaluate(smoothed);
    const auto sharp = Evaluate(Rolling());
    EXPECT_FALSE(
        std::equal(rounded.Values().begin(), rounded.Values().end(), sharp.Values().begin()));
    for (std::size_t row = 1; row < rounded.Height(); row += 7) {
        for (std::size_t col = 2; col < rounded.Width(); col += 7) {
            const auto centre = rounded.CellCenter(col, row);
            ASSERT_EQ(terraingen::EvaluateAt(smoothed, centre.x, centre.y, 2.0), rounded(col, row))
                << col << ", " << row;
        }
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
    terrain.source.reset();
    EXPECT_THROW(Evaluate(terrain), std::invalid_argument);
    EXPECT_THROW(Evaluate(terrain, grid), std::invalid_argument);
}

TEST(EvaluateAt, EqualsTheGridCellCentredThereToTheBit) {
    const Terrain terrain = Rolling();
    for (const double spacing : {2.0, 5.0}) {
        const auto grid = Evaluate(terrain, terrain.source->Grid(), spacing);
        for (std::size_t row = 0; row < grid.Height(); row += 3) {
            for (std::size_t col = 0; col < grid.Width(); col += 3) {
                const auto centre = grid.CellCenter(col, row);
                ASSERT_EQ(terraingen::EvaluateAt(terrain, centre.x, centre.y, spacing),
                          grid(col, row))
                    << "spacing " << spacing << " cell " << col << "," << row;
            }
        }
    }
}

TEST(EvaluatePoints, EqualsEveryGridCellCentredThereInAnyBatchAndThreadCount) {
    const Terrain terrain = Rolling();
    const auto grid = Evaluate(terrain, terrain.source->Grid(), 2.0);
    std::vector<terraingen::Vec2d> centres;
    for (std::size_t row = 0; row < grid.Height(); ++row) {
        for (std::size_t col = 0; col < grid.Width(); ++col) {
            centres.push_back(grid.CellCenter(col, row));
        }
    }
    for (const unsigned threads : {1u, 3u}) {
        const std::vector<float> heights =
            terraingen::EvaluatePoints(terrain, centres, 2.0, threads);
        ASSERT_EQ(heights.size(), grid.Size());
        for (std::size_t i = 0; i < heights.size(); ++i) {
            ASSERT_EQ(heights[i], grid.Values()[i]) << "point " << i;
        }
    }
    EXPECT_TRUE(terraingen::EvaluatePoints(terrain, {}, 2.0).empty());
    EXPECT_THROW(terraingen::EvaluatePoints(terrain, centres, 0.0), std::invalid_argument);
}

TEST(EvaluateAt, ChangesContinuouslyWithTheSpacing) {
    Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(64, 64, 1.0);
    terraingen::CraterLayerOptions options;
    options.generate = false;
    options.generation.manual.Append({0.0, 0.0, 20.0, 0.0, 0u});
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
        terraingen::testing::PowerLawCraterModels(), options));
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
            ASSERT_EQ(part(col, row), whole(30 + col, 20 + row));
        }
    }
    EXPECT_LT(terraingen::PlacedCraters(terrain, grid, 7.0).craters.Size(),
              terraingen::PlacedCraters(terrain, grid).craters.Size())
        << "bands too small for 7 m are not generated";
}

TEST(EvaluateAt, RejectsBadSpacingsAndIncompleteTerrains) {
    Terrain terrain = Rolling();
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, -1.0), std::invalid_argument);
    terrain.layers.push_back(nullptr);
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, 2.0), std::invalid_argument);
    terrain.source.reset();
    EXPECT_THROW(terraingen::EvaluateAt(terrain, 0.0, 0.0, 2.0), std::invalid_argument);
}

namespace {

/// A layer written against samples only: a function of position and spacing.
class Bump final : public terraingen::Layer {
  protected:
    void ApplySamples(terraingen::SampleBlock& samples,
                      const terraingen::LayerContext& context) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const auto p = samples.Position(i);
            samples.Height(i) += static_cast<float>(p.x * 0.5 + p.y + context.spacing);
        }
    }
};

}  // namespace

TEST(Layer, ApplyAtEvaluatesTheLayerAtThePoint) {
    const Bump bump;
    EXPECT_EQ(bump.ApplyAt(1.0f, 4.0, -2.0, {3.0}), 1.0f + 2.0f - 2.0f + 3.0f);
    EXPECT_THROW(bump.ApplyAt(0.0f, 0.0, 0.0, {0.0}), std::invalid_argument);
}

TEST(Evaluate, GivesTheSameBitsOnAnyNumberOfThreads) {
    const Terrain terrain = Smoothed();
    const RasterGrid grid = terrain.source->Grid();
    const auto serial = Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 1});
    for (const unsigned threads : {2u, 8u, 0u}) {
        const auto parallel = Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, threads});
        ExpectSameRaster(parallel, serial);
    }
    EXPECT_THROW(Evaluate(terrain, grid, terraingen::EvaluationOptions{-1.0, 1}),
                 std::invalid_argument);
}

TEST(Layer, DefaultsRunApplyAndPlaceNoFeatures) {
    Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(8, 8, 1.0);
    terrain.layers.push_back(std::make_shared<Bump>());
    const auto parallel =
        Evaluate(terrain, terrain.source->Grid(), terraingen::EvaluationOptions{0.0, 4});
    ExpectSameRaster(parallel, Evaluate(terrain));
}

TEST(EvaluateAt, EqualsGridsForCratersCutToADatum) {
    for (const auto datum :
         {terraingen::CraterDatum::Plane, terraingen::CraterDatum::CentreHeight}) {
        const Terrain terrain = Datum(datum);
        const auto grid = Evaluate(terrain);
        for (std::size_t row = 1; row < grid.Height(); row += 5) {
            for (std::size_t col = 2; col < grid.Width(); col += 5) {
                const auto centre = grid.CellCenter(col, row);
                ASSERT_EQ(terraingen::EvaluateAt(terrain, centre.x, centre.y, 2.0), grid(col, row))
                    << col << "," << row;
            }
        }
    }
}

TEST(TerrainGround, IsTheSourceAndTheLayersBeneath) {
    const Terrain terrain = Datum();
    const terraingen::TerrainGround source(terrain, 0);
    const terraingen::TerrainGround noise(terrain, 1);
    const terraingen::TerrainGround all(terrain, 2);
    EXPECT_EQ(source.HeightAt(3.0, 4.0, 2.0), 5.0f);
    EXPECT_EQ(noise.HeightAt(3.0, 4.0, 2.0), terrain.layers[0]->ApplyAt(5.0f, 3.0, 4.0, {2.0}));
    EXPECT_EQ(all.HeightAt(3.0, 4.0, 2.0), terraingen::EvaluateAt(terrain, 3.0, 4.0, 2.0));
    EXPECT_EQ(noise.Sampler(2.0)(3.0, 4.0), noise.HeightAt(3.0, 4.0, 2.0));
    const std::vector<terraingen::Vec2d> points{{3.0, 4.0}, {-7.5, 2.25}, {11.0, -6.0}};
    std::vector<float> heights(points.size());
    all.HeightsAt(points, heights, 2.0);
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(heights[i], all.HeightAt(points[i].x, points[i].y, 2.0))
            << "one pass, same heights";
    }
    EXPECT_THROW(terraingen::TerrainGround(terrain, 3), std::invalid_argument);
    EXPECT_EQ(terraingen::TerrainGround(Terrain{}, 0).HeightAt(3.0, 4.0, 2.0), 0.0f)
        << "the datum without a source";
}

TEST(Evaluate, StartsATerrainWithoutASourceAtTheDatum) {
    Terrain terrain;
    terrain.layers = {std::make_shared<terraingen::ScaleLayer>(1.0, 7.5)};
    const RasterGrid grid{6, 4, GeoTransform::NorthUp(100.0, 50.0, 2.0)};
    const auto heights = Evaluate(terrain, grid);
    EXPECT_TRUE(std::all_of(heights.Values().begin(), heights.Values().end(),
                            [](float h) { return h == 7.5f; }));
    EXPECT_EQ(terraingen::EvaluateAt(terrain, 101.0, 49.0, 2.0), 7.5f);
    EXPECT_THROW(Evaluate(terrain), std::invalid_argument) << "no source grid to default to";
}

TEST(Evaluate, RunsOnTheChosenBackendToWithinFloatRounding) {
    const Terrain terrain = Rolling();
    const RasterGrid grid = terrain.source->Grid();
    const auto cpu = Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 2});
    ExpectSameRaster(
        Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 2, terraingen::Backend::Cpu}),
        cpu);
    const auto automatic =
        Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 2, terraingen::Backend::Auto});
    if (terraingen::ResolveBackend(terraingen::Backend::Auto) == terraingen::Backend::Cpu) {
        ExpectSameRaster(automatic, cpu);
        EXPECT_THROW(Evaluate(terrain, grid,
                              terraingen::EvaluationOptions{0.0, 2, terraingen::Backend::Vulkan}),
                     std::runtime_error);
        return;
    }
    for (std::size_t i = 0; i < cpu.Size(); ++i) {
        ASSERT_NEAR(automatic.Values()[i], cpu.Values()[i],
                    1e-4f * std::max(1.0f, std::fabs(cpu.Values()[i])))
            << i;
    }
}

TEST(Evaluate, WithoutABackendFollowsTheProcessDefault) {
    const Terrain terrain = Rolling();
    const RasterGrid grid = terrain.source->Grid();
    const auto cpu =
        Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 2, terraingen::Backend::Cpu});
    terraingen::SetDefaultBackend(terraingen::Backend::Auto);
    const auto byDefault = Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 2});
    const auto chosen =
        Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 2, terraingen::Backend::Auto});
    terraingen::SetDefaultBackend(terraingen::Backend::Cpu);
    ExpectSameRaster(byDefault, chosen);
    ExpectSameRaster(Evaluate(terrain, grid, terraingen::EvaluationOptions{0.0, 2}), cpu);
}
