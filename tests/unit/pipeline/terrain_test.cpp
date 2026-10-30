// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/terrain.h"

#include "support/raster_equality.h"
#include "terraingen/terraingen.h"
#include "terrains.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using terraingen::EvaluationOptions;
using terraingen::GeoTransform;
using terraingen::LayerPtr;
using terraingen::RasterGrid;
using terraingen::StageCache;
using terraingen::Surface;
using terraingen::Terrain;
using terraingen::Vec2d;
using terraingen::testing::Datum;
using terraingen::testing::ExpectSameRaster;
using terraingen::testing::Rolling;
using terraingen::testing::Smoothed;

namespace {

/// Evaluation at `spacing` meters.
EvaluationOptions At(double spacing) {
    return EvaluationOptions{}.Spacing(spacing);
}

/// `terrain.HeightsAt` at `points` on `threads` threads.
std::vector<float> Heights(const Terrain& terrain, const std::vector<Vec2d>& points, double spacing,
                           unsigned threads) {
    std::vector<float> heights(points.size());
    terrain.HeightsAt(points, heights, spacing, threads);
    return heights;
}

/// The source's grid of `terrain`, which has one.
RasterGrid SourceGrid(const Terrain& terrain) {
    return terrain.Grid().value();
}

/// Whether `a` and `b` differ in any cell.
bool Differ(const terraingen::Raster<float>& a, const terraingen::Raster<float>& b) {
    return !std::equal(a.Values().begin(), a.Values().end(), b.Values().begin());
}

/// A crater layer of one manual crater, built for `surface`.
std::shared_ptr<terraingen::CraterLayer> ManualCraters(const Surface& surface = {}) {
    return terraingen::CraterLayer::Manual(
        {terraingen::ManualCrater(0.0, 0.0, 4.0)}, terraingen::DefaultCraterModels(),
        terraingen::DefaultCraterApply(), terraingen::TerrainBase{surface});
}

/// A plane terrain of random 2 to 12 m craters, then boulders thrown from them.
Terrain CratersAndBoulders() {
    terraingen::CraterLayerOptions options;
    options.generation.dMin = 2.0;
    options.generation.dMax = 12.0;
    const auto craters = std::make_shared<terraingen::CraterLayer>(
        terraingen::testing::PowerLawCraterModels(), options);
    const auto boulders =
        std::make_shared<terraingen::BoulderLayer>(terraingen::BoulderLayerOptions{}, craters);
    return Terrain(Surface{}, std::make_shared<terraingen::FlatSource>(32, 32, 1.0),
                   {craters, boulders});
}

/// A plane terrain of synthetic noise heights and a noise layer.
Terrain Noisy() {
    const auto source = std::make_shared<terraingen::SyntheticSource>(
        std::vector<terraingen::Fractal>{terraingen::Fractal::Fbm(5, 120.0, 8.0)});
    return Terrain(
        Surface{}, source,
        {std::make_shared<terraingen::NoiseLayer>(terraingen::Fractal::Fbm(9, 20.0, 1.0))});
}

/// The grid `Noisy` is evaluated over.
const RasterGrid kNoisyGrid{24, 16, GeoTransform::NorthUp(-40.0, 30.0, 2.5)};

/// Evaluation options that record each layer applied into `applied`.
EvaluationOptions Recording(std::vector<std::size_t>& applied) {
    EvaluationOptions options;
    options.onLayer = [&applied](std::size_t layer, double) { applied.push_back(layer); };
    return options;
}

}  // namespace

TEST(ForEachLayer, VisitsTheLayersOfOneTypeInOrderWithTheirIndices) {
    const Terrain rolling = Rolling();
    std::vector<LayerPtr> layers = rolling.Layers();
    layers.push_back(layers.front());
    const Terrain terrain(rolling.Surface(), rolling.Source(), layers);
    std::vector<std::size_t> noise;
    terraingen::ForEachLayer<terraingen::NoiseLayer>(
        terrain, [&](const terraingen::NoiseLayer& layer, std::size_t n) {
            EXPECT_EQ(&layer, terrain.Layers()[n].get());
            noise.push_back(n);
        });
    EXPECT_EQ(noise, (std::vector<std::size_t>{0, 2}));
    std::size_t craters = 0;
    terraingen::ForEachLayer<terraingen::CraterPlacer>(
        terrain, [&](const terraingen::CraterPlacer&, std::size_t n) {
            EXPECT_EQ(n, 1u);
            ++craters;
        });
    EXPECT_EQ(craters, 1u);
}

TEST(Terrain, RejectsNullLayersLayersOfAnotherSurfaceAndDependenciesOutOfOrder) {
    const auto flat = std::make_shared<terraingen::FlatSource>(9, 9, 1.0);
    EXPECT_THROW(Terrain(Surface{}, flat, {nullptr}), std::invalid_argument);
    const Surface moon = Surface::Sphere(1737400.0);
    EXPECT_THROW(Terrain(Surface{}, flat, {ManualCraters(moon)}), std::invalid_argument)
        << "a layer for the Moon on a plane";
    EXPECT_NO_THROW(Terrain(moon, nullptr, {ManualCraters(moon)}));

    const auto craters = ManualCraters();
    const auto boulders =
        std::make_shared<terraingen::BoulderLayer>(terraingen::BoulderLayerOptions{}, craters);
    EXPECT_EQ(boulders->Dependencies(), std::vector<const terraingen::Layer*>{craters.get()});
    EXPECT_THROW(Terrain(Surface{}, flat, {boulders}), std::invalid_argument)
        << "the crater layer is missing";
    EXPECT_THROW(Terrain(Surface{}, flat, {boulders, craters}), std::invalid_argument)
        << "the crater layer comes after";
    EXPECT_NO_THROW(Terrain(Surface{}, flat, {craters, boulders}));
}

TEST(Terrain, RejectsSourcesWhoseCoordinatesDoNotSuitTheSurface) {
    terraingen::Raster<float> heights(4, 4, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(10.0, 20.0, 0.25));
    const Surface moon = Surface::Sphere(1737400.0);
    const auto degrees = std::make_shared<terraingen::RasterSource>(
        heights, terraingen::RasterSourceOptions{}, terraingen::GeographicSphereKeys(1737400.0));
    EXPECT_THROW(Terrain(Surface{}, degrees, {}), std::invalid_argument) << "degrees on a plane";
    EXPECT_EQ(Terrain(moon, degrees, {}).Evaluate().Values()[0], 1.0f);

    terraingen::GeoKeys projected;
    projected.directory = {1, 1, 0, 1, 1024, 0, 1, 1};
    const auto meters = std::make_shared<terraingen::RasterSource>(
        heights, terraingen::RasterSourceOptions{}, projected);
    EXPECT_THROW(Terrain(moon, meters, {}), std::invalid_argument)
        << "a projected source on a body";
    EXPECT_NO_THROW(Terrain(Surface{}, meters, {}).Evaluate());
}

TEST(Terrain, GivesItsPartsGridAndKeys) {
    const Terrain terrain = Rolling();
    EXPECT_EQ(terrain.Layers().size(), 2u);
    EXPECT_EQ(terrain.Grid(), terrain.Source()->Grid());
    EXPECT_FALSE(Terrain().Grid().has_value());
    EXPECT_FALSE(Noisy().Grid().has_value()) << "a source without bounds";
    EXPECT_TRUE(Terrain().Keys().Empty());
    EXPECT_TRUE(Terrain(Surface::Sphere(1000.0), nullptr, {}).Keys().IsGeographic());
}

TEST(Evaluate, RunsTheSourceThenLayersOverTheSourcesGrid) {
    const Terrain terrain = Rolling();
    const auto whole = terrain.Evaluate();
    EXPECT_EQ(RasterGrid::FromRaster(whole), terrain.Source()->Grid());
    EXPECT_GT(terrain.Craters(RasterGrid::FromRaster(whole)).craters.Size(), 0u);
    const auto again = terrain.Evaluate(SourceGrid(terrain), At(1.0));
    for (std::size_t i = 0; i < whole.Size(); ++i) {
        ASSERT_EQ(again.Values()[i], whole.Values()[i])
            << "a spacing finer than the cells is raised to them";
    }
}

TEST(Evaluate, NeedsAGridWithoutABoundedSource) {
    EXPECT_THROW(Terrain().Evaluate(), std::invalid_argument) << "no source";
    EXPECT_THROW(Noisy().Evaluate(), std::invalid_argument) << "a source without bounds";
    EXPECT_NO_THROW(Noisy().Evaluate(kNoisyGrid));
}

TEST(Fill, WritesWhatEvaluateGivesToTheBitAndRejectsWrongSizes) {
    const Terrain terrain = Rolling();
    const RasterGrid grid = SourceGrid(terrain);
    const auto options = EvaluationOptions{}.Spacing(3.0).Threads(2);
    const auto expected = terrain.Evaluate(grid, options);
    std::vector<float> heights(grid.width * grid.height, -1.0f);
    terrain.Fill(grid, heights, options);
    ASSERT_EQ(heights.size(), expected.Size());
    EXPECT_EQ(std::memcmp(heights.data(), expected.Values().data(), heights.size() * sizeof(float)),
              0);

    std::vector<float> defaults(grid.width * grid.height);
    terrain.Fill(grid, defaults);
    const auto plain = terrain.Evaluate(grid);
    EXPECT_EQ(std::memcmp(defaults.data(), plain.Values().data(), defaults.size() * sizeof(float)),
              0);

    Terrain datum = terrain;
    datum.SetSource(nullptr);
    std::vector<float> stale(grid.width * grid.height, 123.0f);
    datum.Fill(grid, stale);
    const auto onDatum = datum.Evaluate(grid);
    EXPECT_EQ(std::memcmp(stale.data(), onDatum.Values().data(), stale.size() * sizeof(float)), 0)
        << "without a source the buffer starts from the datum, whatever it held";

    std::vector<float> tooShort(grid.width * grid.height - 1);
    EXPECT_THROW(terrain.Fill(grid, tooShort), std::invalid_argument);
    std::vector<float> tooLong(grid.width * grid.height + 1);
    EXPECT_THROW(terrain.Fill(grid, tooLong), std::invalid_argument);
}

TEST(Evaluate, TilesEqualTheWholeTerrain) {
    const Terrain terrain = Rolling();
    const auto whole = terrain.Evaluate();
    const RasterGrid grid = SourceGrid(terrain);
    for (const auto& [col0, row0, width, height] :
         {std::array<std::size_t, 4>{0, 0, 40, 30}, std::array<std::size_t, 4>{40, 0, 56, 30},
          std::array<std::size_t, 4>{0, 30, 96, 34}, std::array<std::size_t, 4>{17, 11, 23, 29}}) {
        const auto corner =
            grid.transform.PixelToWorld(static_cast<double>(col0), static_cast<double>(row0));
        const RasterGrid tile{width, height, GeoTransform::NorthUp(corner.x, corner.y, 2.0)};
        const auto part = terrain.Evaluate(tile);
        for (std::size_t row = 0; row < height; ++row) {
            for (std::size_t col = 0; col < width; ++col) {
                ASSERT_EQ(part(col, row), whole(col0 + col, row0 + row))
                    << "tile at " << col0 << "," << row0 << " cell " << col << "," << row;
            }
        }
    }
}

TEST(Evaluate, RoundsCraterRimsAndStillAnswersPointQueries) {
    const Terrain smoothed = Smoothed();
    const auto rounded = smoothed.Evaluate();
    EXPECT_TRUE(Differ(rounded, Rolling().Evaluate()));
    for (std::size_t row = 1; row < rounded.Height(); row += 7) {
        for (std::size_t col = 2; col < rounded.Width(); col += 7) {
            const auto center = rounded.CellCenter(col, row);
            ASSERT_EQ(smoothed.HeightAt(center.x, center.y, 2.0), rounded(col, row))
                << col << ", " << row;
        }
    }
}

TEST(Evaluate, RejectsBadSpacings) {
    const Terrain terrain = Rolling();
    const RasterGrid grid = SourceGrid(terrain);
    EXPECT_THROW(terrain.Evaluate(grid, At(-1.0)), std::invalid_argument);
    EXPECT_THROW(terrain.Evaluate(grid, At(std::numeric_limits<double>::quiet_NaN())),
                 std::invalid_argument);
}

TEST(HeightAt, EqualsTheGridCellCenteredThereToTheBit) {
    const Terrain terrain = Rolling();
    for (const double spacing : {2.0, 5.0}) {
        const auto grid = terrain.Evaluate(SourceGrid(terrain), At(spacing));
        for (std::size_t row = 0; row < grid.Height(); row += 3) {
            for (std::size_t col = 0; col < grid.Width(); col += 3) {
                const auto center = grid.CellCenter(col, row);
                ASSERT_EQ(terrain.HeightAt(center.x, center.y, spacing), grid(col, row))
                    << "spacing " << spacing << " cell " << col << "," << row;
            }
        }
    }
}

TEST(HeightsAt, EqualsEveryGridCellCenteredThereThroughEitherOverload) {
    const Terrain terrain = Rolling();
    const auto grid = terrain.Evaluate(SourceGrid(terrain), At(2.0));
    std::vector<Vec2d> centers;
    for (std::size_t row = 0; row < grid.Height(); ++row) {
        for (std::size_t col = 0; col < grid.Width(); ++col) {
            centers.push_back(grid.CellCenter(col, row));
        }
    }
    for (const unsigned threads : {1u, 3u, 0u}) {
        const std::vector<float> heights = Heights(terrain, centers, 2.0, threads);
        ASSERT_EQ(heights.size(), grid.Size());
        for (std::size_t i = 0; i < heights.size(); ++i) {
            ASSERT_TRUE(terraingen::testing::SameBits(heights[i], grid.Values()[i]))
                << "point " << i << " on " << threads << " threads";
        }
    }
    std::vector<float> ground(centers.size());
    terrain.HeightsAt(centers, ground, 2.0);
    for (std::size_t i = 0; i < ground.size(); ++i) {
        ASSERT_TRUE(terraingen::testing::SameBits(ground[i], grid.Values()[i])) << "point " << i;
    }
    EXPECT_TRUE(Heights(terrain, {}, 2.0, 1).empty());
    EXPECT_THROW(Heights(terrain, centers, 0.0, 1), std::invalid_argument);
    std::vector<float> fewer(centers.size() - 1);
    EXPECT_THROW(terrain.HeightsAt(centers, fewer, 2.0, 1), std::invalid_argument);
    EXPECT_THROW(terrain.HeightsAt(centers, fewer, 2.0), std::invalid_argument);
}

TEST(HeightAt, ChangesContinuouslyWithTheSpacing) {
    terraingen::CraterLayerOptions options;
    options.generateRandom = false;
    options.generation.manual.Append({0.0, 0.0, 20.0, 0.0, 0u});
    const Terrain terrain(Surface{}, std::make_shared<terraingen::FlatSource>(64, 64, 1.0),
                          {std::make_shared<terraingen::CraterLayer>(
                              terraingen::testing::PowerLawCraterModels(), options)});
    const float whole = terrain.HeightAt(0.0, 0.0, 1.0);
    ASSERT_LT(whole, -1.0f);
    float previous = whole;
    double largestStep = 0.0;
    for (double spacing = 1.0; spacing <= 12.0; spacing *= 1.01) {
        const float height = terrain.HeightAt(0.0, 0.0, spacing);
        largestStep = std::fmax(largestStep, std::fabs(height - previous));
        previous = height;
    }
    EXPECT_EQ(previous, 0.0f) << "under 2 samples across at the coarsest spacing";
    EXPECT_LT(largestStep, 0.03 * std::fabs(static_cast<double>(whole)))
        << "no level pops the crater in or out";
}

TEST(HeightAt, GivesTheSameHeightsFromManyThreads) {
    const Terrain terrain = Rolling();
    std::vector<std::array<double, 2>> points;
    for (int n = 0; n < 400; ++n) {
        points.push_back({-90.0 + 0.45 * n, 60.0 - 0.3 * n});
    }
    std::vector<float> serial;
    for (const auto& point : points) {
        serial.push_back(terrain.HeightAt(point[0], point[1], 2.0));
    }
    std::vector<float> parallel(points.size());
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < 8; ++t) {
        threads.emplace_back([&, t] {
            for (std::size_t n = t; n < points.size(); n += 8) {
                parallel[n] = terrain.HeightAt(points[n][0], points[n][1], 2.0);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(parallel, serial);
}

TEST(HeightAt, RejectsBadSpacings) {
    const Terrain terrain = Rolling();
    EXPECT_THROW(terrain.HeightAt(0.0, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(terrain.HeightAt(0.0, 0.0, -1.0), std::invalid_argument);
}

TEST(Evaluate, TilesEqualTheWholeTerrainAtACoarserSpacing) {
    const Terrain terrain = Rolling();
    const RasterGrid grid = SourceGrid(terrain);
    const auto whole = terrain.Evaluate(grid, At(7.0));
    const auto corner = grid.transform.PixelToWorld(30.0, 20.0);
    const RasterGrid tile{30, 25, GeoTransform::NorthUp(corner.x, corner.y, 2.0)};
    const auto part = terrain.Evaluate(tile, At(7.0));
    for (std::size_t row = 0; row < tile.height; ++row) {
        for (std::size_t col = 0; col < tile.width; ++col) {
            ASSERT_EQ(part(col, row), whole(30 + col, 20 + row));
        }
    }
    EXPECT_LT(terrain.Craters(grid, 7.0).craters.Size(), terrain.Craters(grid).craters.Size())
        << "bands too small for 7 m are not generated";
}

namespace {

/// A flat source that records the last sample spacing and thread count it was given.
class SpacingProbe final : public terraingen::Source {
  public:
    RasterGrid Grid() const override {
        return {};
    }
    mutable double last = 0.0;
    mutable unsigned lastThreads = 99;

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double spacing,
                     unsigned threads) const override {
        last = spacing;
        if (samples.IsGrid()) {
            lastThreads = threads;
        }
        std::fill(samples.Heights().begin(), samples.Heights().end(), 0.0f);
    }
};

}  // namespace

TEST(Evaluate, GivesTheSourceTheSampleSpacingInItsUnits) {
    const auto probe = std::make_shared<SpacingProbe>();
    const Terrain terrain(Surface{}, probe, {});
    const RasterGrid grid{4, 4, GeoTransform::NorthUp(0.0, 0.0, 2.0)};
    terrain.Evaluate(grid);
    EXPECT_EQ(probe->last, 2.0);
    EXPECT_EQ(probe->lastThreads, 0u);
    terrain.Evaluate(grid, At(5.0));
    EXPECT_EQ(probe->last, 5.0);
    terrain.Evaluate(grid, EvaluationOptions{}.Threads(3));
    EXPECT_EQ(probe->lastThreads, 3u) << "the evaluation's threads";
    terrain.HeightAt(1.0, 1.0, 0.25);
    EXPECT_EQ(probe->last, 0.25);
    const Terrain body(Surface::Sphere(1000.0), probe, {});
    body.HeightAt(1.0, 1.0, 0.25);
    EXPECT_DOUBLE_EQ(probe->last, 0.25 / body.Surface().UnitLength()) << "degrees on a body";
    EXPECT_THROW(body.Evaluate(), std::invalid_argument) << "a source without bounds";
}

namespace {

/// A bowl over a 64 x 64 grid of 1 m cells that notes each thread reading it.
class ThreadNotingSource final : public terraingen::Source {
  public:
    RasterGrid Grid() const override {
        return {64, 64, GeoTransform::NorthUp(-32.0, 32.0, 1.0)};
    }
    mutable std::mutex mutex;
    mutable std::set<std::thread::id> threads;

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double /*spacing*/,
                     unsigned count) const override {
        if (terraingen::Raster<float>* grid = samples.Grid()) {
            terraingen::ParallelFor(grid->Height(), count, [&](std::size_t row) {
                for (std::size_t col = 0; col < grid->Width(); ++col) {
                    (*grid)(col, row) = Bowl(grid->CellCenter(col, row));
                }
            });
            return;
        }
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = Bowl(samples.Position(i));
        }
    }

  private:
    /// The bowl's height at `p`, noting the thread reading it.
    float Bowl(const Vec2d& p) const {
        {
            const std::lock_guard lock(mutex);
            threads.insert(std::this_thread::get_id());
        }
        return static_cast<float>(0.01 * (p.x * p.x + p.y * p.y));
    }
};

}  // namespace

TEST(Evaluate, ReadsTheGroundBeneathALayerOnTheEvaluationsThreads) {
    const auto source = std::make_shared<ThreadNotingSource>();
    const Terrain terrain(Surface{}, source, {std::make_shared<terraingen::SandFillLayer>()});
    const RasterGrid grid = source->Grid();
    const terraingen::Raster<float> one = terrain.Evaluate(grid, EvaluationOptions{}.Threads(1));
    EXPECT_EQ(source->threads, std::set<std::thread::id>{std::this_thread::get_id()})
        << "the sand fill's ground reads stay on the one thread asked for";
    ExpectSameRaster(one, terrain.Evaluate(grid));
}

TEST(HeightsAt, EqualsGridsOverAnUpsampledDemAtTheirSpacing) {
    terraingen::Raster<float> dem(16, 16);
    dem.SetTransform(GeoTransform::NorthUp(-32.0, 32.0, 4.0));
    for (std::size_t i = 0; i < dem.Size(); ++i) {
        dem.Values()[i] = static_cast<float>(std::sin(0.37 * static_cast<double>(i)));
    }
    terraingen::DiamondSquareOptions fractal;
    fractal.fractalDimension = 2.4;
    fractal.scale = 0.2;
    terraingen::RasterSourceOptions options;
    options.upsampler = std::make_shared<terraingen::DiamondSquareUpsampler>(fractal);
    const Terrain terrain(Surface{}, std::make_shared<terraingen::RasterSource>(dem, options), {});
    const RasterGrid grid{40, 30, GeoTransform::NorthUp(-20.3, 19.1, 0.6)};
    const auto heights = terrain.Evaluate(grid);
    for (std::size_t row = 0; row < grid.height; row += 3) {
        for (std::size_t col = 0; col < grid.width; col += 3) {
            const auto center = heights.CellCenter(col, row);
            ASSERT_EQ(terrain.HeightAt(center.x, center.y, 0.6), heights(col, row));
        }
    }
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
    const RasterGrid grid = SourceGrid(terrain);
    const auto serial = terrain.Evaluate(grid, EvaluationOptions{}.Threads(1));
    for (const unsigned threads : {2u, 8u, 0u}) {
        ExpectSameRaster(terrain.Evaluate(grid, EvaluationOptions{}.Threads(threads)), serial);
    }
}

TEST(Layer, DefaultsRunApplyAndPlaceNoFeatures) {
    const Terrain terrain(Surface{}, std::make_shared<terraingen::FlatSource>(8, 8, 1.0),
                          {std::make_shared<Bump>()});
    ExpectSameRaster(terrain.Evaluate(SourceGrid(terrain), EvaluationOptions{}.Threads(4)),
                     terrain.Evaluate());
    EXPECT_EQ(terrain.Craters(SourceGrid(terrain)).craters.Size(), 0u);
}

TEST(HeightAt, EqualsGridsForCratersCutToADatum) {
    for (const auto datum :
         {terraingen::CraterDatum::Plane, terraingen::CraterDatum::CenterHeight}) {
        const Terrain terrain = Datum(datum);
        const auto grid = terrain.Evaluate();
        for (std::size_t pass = 0; pass < 2; ++pass) {
            for (std::size_t row = 1 + pass; row < grid.Height(); row += 4) {
                for (std::size_t col = 2; col < grid.Width(); col += 3) {
                    const auto center = grid.CellCenter(col, row);
                    ASSERT_EQ(terrain.HeightAt(center.x, center.y, 2.0), grid(col, row))
                        << "pass " << pass << " at " << col << "," << row;
                }
            }
        }
    }
}

TEST(HeightAt, KeepsDatumsPerTerrainSoALayerSharedOverOtherGroundRefitsThem) {
    const Terrain terrain = Datum(terraingen::CraterDatum::Plane);
    Terrain steeper = terrain;
    std::vector<terraingen::Fractal> noise{terraingen::Fractal{}};
    noise[0].wavelength = 30.0f;
    noise[0].amplitude = 9.0f;
    steeper.SetLayer(0, std::make_shared<terraingen::NoiseLayer>(noise));
    EXPECT_EQ(steeper.Layers()[1], terrain.Layers()[1]) << "the crater layer is shared";
    const auto grid = terrain.Evaluate();
    const auto steeperGrid = steeper.Evaluate();
    std::size_t differing = 0;
    for (std::size_t pass = 0; pass < 2; ++pass) {
        for (std::size_t row = 1; row < grid.Height(); row += 4) {
            for (std::size_t col = 2; col < grid.Width(); col += 3) {
                const auto center = grid.CellCenter(col, row);
                ASSERT_EQ(terrain.HeightAt(center.x, center.y, 2.0), grid(col, row))
                    << "pass " << pass << " at " << col << "," << row;
                ASSERT_EQ(steeper.HeightAt(center.x, center.y, 2.0), steeperGrid(col, row))
                    << "pass " << pass << " at " << col << "," << row;
                differing += grid(col, row) != steeperGrid(col, row) ? std::size_t{1} : 0;
            }
        }
    }
    EXPECT_GT(differing, 0u) << "the grounds differ, so a shared cache would show";
}

TEST(Ground, IsTheSourceAndTheLayersBeneath) {
    const Terrain terrain = Datum();
    const Terrain source = terrain.Ground(0);
    const Terrain noise = terrain.Ground(1);
    const Terrain all = terrain.Ground(2);
    EXPECT_EQ(source.HeightAt(3.0, 4.0, 2.0), 5.0f);
    terraingen::LayerContext context;
    context.spacing = 2.0;
    context.surface = &terrain.Surface();
    context.source = terrain.Source().get();
    EXPECT_EQ(noise.HeightAt(3.0, 4.0, 2.0), terrain.Layers()[0]->ApplyAt(5.0f, 3.0, 4.0, context));
    EXPECT_EQ(all.HeightAt(3.0, 4.0, 2.0), terrain.HeightAt(3.0, 4.0, 2.0));
    EXPECT_EQ(noise.LayerVersion(0), terrain.LayerVersion(0)) << "prefixes share versions";
    EXPECT_EQ(noise.SourceVersion(), terrain.SourceVersion());
    const std::vector<Vec2d> points{{3.0, 4.0}, {-7.5, 2.25}, {11.0, -6.0}};
    std::vector<float> heights(points.size());
    all.HeightsAt(points, heights, 2.0);
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(heights[i], all.HeightAt(points[i].x, points[i].y, 2.0))
            << "one pass, same heights";
    }
    EXPECT_THROW(terrain.Ground(3), std::invalid_argument);
    EXPECT_EQ(Terrain().HeightAt(3.0, 4.0, 2.0), 0.0f) << "the datum without a source";
}

TEST(Ground, EqualsATerrainOfTheFirstLayers) {
    const Terrain terrain = Datum();
    const RasterGrid grid = SourceGrid(terrain);
    for (std::size_t k = 0; k <= terrain.Layers().size(); ++k) {
        const auto end = terrain.Layers().begin() + static_cast<std::ptrdiff_t>(k);
        const std::vector<LayerPtr> first(terrain.Layers().begin(), end);
        const Terrain built(terrain.Surface(), terrain.Source(), first);
        ExpectSameRaster(terrain.Ground(k).Evaluate(grid), built.Evaluate(grid));
        EXPECT_EQ(terrain.Ground(k).Layers().size(), k);
    }
}

TEST(Ground, PrefixesShareTheTerrainsPointQueryValues) {
    const Terrain terrain = Datum(terraingen::CraterDatum::Plane);
    const Terrain cached = terrain.Ground(2);
    const Terrain fresh = Datum(terraingen::CraterDatum::Plane);
    std::vector<Vec2d> points;
    for (int i = 0; i < 600; ++i) {
        points.push_back({-20.0 + 0.07 * i, 15.0 - 0.05 * i});
    }
    std::vector<float> first(points.size());
    std::vector<float> again(points.size());
    std::vector<float> expected(points.size());
    cached.HeightsAt(points, first, 2.0);
    terrain.HeightsAt(points, again, 2.0);
    fresh.HeightsAt(points, expected, 2.0);
    EXPECT_EQ(first, expected);
    EXPECT_EQ(again, expected) << "values kept from the prefix's read";
    EXPECT_EQ(cached.HeightAt(points[7].x, points[7].y, 2.0), expected[7]);
}

TEST(Ground, OutlivesTheTerrainItWasMadeFrom) {
    std::shared_ptr<const terraingen::Ground> ground;
    float expected = 0.0f;
    {
        const Terrain terrain = Rolling();
        expected = terrain.Ground(1).HeightAt(3.0, 4.0, 1.0);
        ground = std::make_shared<const Terrain>(terrain.Ground(1));
    }
    EXPECT_EQ(ground->HeightAt(3.0, 4.0, 1.0), expected);
    std::vector<float> heights(1);
    const Vec2d point{3.0, 4.0};
    ground->HeightsAt({&point, 1}, heights, 1.0);
    EXPECT_EQ(heights[0], expected);
}

TEST(Evaluate, StartsATerrainWithoutASourceAtTheDatum) {
    const Terrain terrain(Surface{}, nullptr, {std::make_shared<terraingen::ScaleLayer>(1.0, 7.5)});
    const RasterGrid grid{6, 4, GeoTransform::NorthUp(100.0, 50.0, 2.0)};
    const auto heights = terrain.Evaluate(grid);
    EXPECT_TRUE(std::all_of(heights.Values().begin(), heights.Values().end(),
                            [](float h) { return h == 7.5f; }));
    EXPECT_EQ(terrain.HeightAt(101.0, 49.0, 2.0), 7.5f);
    EXPECT_THROW(terrain.Evaluate(), std::invalid_argument) << "no source grid to default to";
}

TEST(Evaluate, RunsOnTheChosenBackendToWithinFloatRounding) {
    const Terrain terrain = Rolling();
    const RasterGrid grid = SourceGrid(terrain);
    const auto cpu = terrain.Evaluate(grid, EvaluationOptions{}.Threads(2));
    ExpectSameRaster(
        terrain.Evaluate(grid, EvaluationOptions{}.Threads(2).On(terraingen::Backend::Cpu)), cpu);
    const auto automatic =
        terrain.Evaluate(grid, EvaluationOptions{}.Threads(2).On(terraingen::Backend::Auto));
    if (terraingen::ResolveBackend(terraingen::Backend::Auto) == terraingen::Backend::Cpu) {
        ExpectSameRaster(automatic, cpu);
        EXPECT_THROW(
            terrain.Evaluate(grid, EvaluationOptions{}.Threads(2).On(terraingen::Backend::Vulkan)),
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
    const RasterGrid grid = SourceGrid(terrain);
    const auto cpu =
        terrain.Evaluate(grid, EvaluationOptions{}.Threads(2).On(terraingen::Backend::Cpu));
    terraingen::SetDefaultBackend(terraingen::Backend::Auto);
    const auto byDefault = terrain.Evaluate(grid, EvaluationOptions{}.Threads(2));
    const auto chosen =
        terrain.Evaluate(grid, EvaluationOptions{}.Threads(2).On(terraingen::Backend::Auto));
    terraingen::SetDefaultBackend(terraingen::Backend::Cpu);
    ExpectSameRaster(byDefault, chosen);
    ExpectSameRaster(terrain.Evaluate(grid, EvaluationOptions{}.Threads(2)), cpu);
}

TEST(Evaluate, ReportsEachLayerAsItIsApplied) {
    const Terrain terrain = Rolling();
    std::vector<std::size_t> layers;
    EvaluationOptions options;
    options.onLayer = [&](std::size_t layer, double milliseconds) {
        layers.push_back(layer);
        EXPECT_GE(milliseconds, 0.0);
    };
    const auto reported = terrain.Evaluate(SourceGrid(terrain), options);
    EXPECT_EQ(layers, (std::vector<std::size_t>{0, 1}));
    ExpectSameRaster(reported, terrain.Evaluate());
}

TEST(HeightsAt, GivesEachPointItsOwnHeightInAnyOrder) {
    const Terrain terrain = Rolling();
    std::vector<Vec2d> points;
    for (int n = 0; n < 700; ++n) {
        points.push_back({std::fmod(n * 37.77, 90.0) - 45.0, std::fmod(n * 91.13, 90.0) - 45.0});
    }
    const std::vector<float> heights = Heights(terrain, points, 1.0, 3);
    for (std::size_t n = 0; n < points.size(); n += 23) {
        ASSERT_EQ(heights[n], terrain.HeightAt(points[n].x, points[n].y, 1.0)) << n;
    }
}

TEST(EvaluationOptions, SetsItsFieldsByName) {
    StageCache cache(1);
    const auto options =
        EvaluationOptions{}.Spacing(0.5).Threads(3).On(terraingen::Backend::Cpu).Keep(cache);
    EXPECT_EQ(options.spacing, 0.5);
    EXPECT_EQ(options.threads, 3u);
    EXPECT_EQ(options.backend, terraingen::Backend::Cpu);
    EXPECT_EQ(options.stageCache, &cache);
}

TEST(SetLayer, ChangesTheVersionOfThatLayerOnly) {
    Terrain terrain = Rolling();
    const std::uint64_t noise = terrain.LayerVersion(0);
    const std::uint64_t craters = terrain.LayerVersion(1);
    const std::uint64_t source = terrain.SourceVersion();
    const Terrain before = terrain;
    const LayerPtr smoothed = Smoothed().Layers()[1];
    terrain.SetLayer(1, smoothed);
    EXPECT_EQ(terrain.Layers()[1], smoothed);
    EXPECT_EQ(terrain.Layers()[0], before.Layers()[0]);
    EXPECT_EQ(terrain.LayerVersion(0), noise);
    EXPECT_NE(terrain.LayerVersion(1), craters);
    EXPECT_EQ(terrain.SourceVersion(), source);
    EXPECT_EQ(before.LayerVersion(1), craters) << "a copy keeps its own parts";
    ExpectSameRaster(terrain.Evaluate(), Smoothed().Evaluate());
    EXPECT_THROW(terrain.LayerVersion(2), std::out_of_range);
}

TEST(SetLayer, RejectsABadIndexOrLayerLeavingTheTerrainUnchanged) {
    Terrain terrain = Rolling();
    const LayerPtr noise = terrain.Layers()[0];
    const LayerPtr craters = terrain.Layers()[1];
    const std::uint64_t version = terrain.LayerVersion(1);
    EXPECT_THROW(terrain.SetLayer(2, craters), std::invalid_argument);
    EXPECT_THROW(terrain.SetLayer(1, nullptr), std::invalid_argument);
    EXPECT_THROW(terrain.SetLayer(0, ManualCraters(Surface::Sphere(1000.0))),
                 std::invalid_argument);
    ASSERT_EQ(terrain.Layers().size(), 2u);
    EXPECT_EQ(terrain.Layers()[0], noise);
    EXPECT_EQ(terrain.Layers()[1], craters);
    EXPECT_EQ(terrain.LayerVersion(1), version);
}

TEST(SetLayer, RebindsALaterLayerThatReadsTheReplacedOne) {
    Terrain terrain = CratersAndBoulders();
    const std::uint64_t boulders = terrain.LayerVersion(1);
    const auto craters = ManualCraters();
    terrain.SetLayer(0, craters);
    EXPECT_NE(terrain.LayerVersion(1), boulders) << "the boulders were rebound";
    EXPECT_EQ(terrain.Layers()[1]->Dependencies(),
              std::vector<const terraingen::Layer*>{craters.get()});

    Terrain rolling = Rolling();
    const std::uint64_t kept = rolling.LayerVersion(1);
    rolling.SetLayer(0, rolling.Layers()[0]);
    EXPECT_EQ(rolling.LayerVersion(1), kept) << "a layer that reads no other keeps its version";
}

TEST(SetSource, ChangesTheSourceVersionOnly) {
    Terrain terrain = Rolling();
    const std::uint64_t source = terrain.SourceVersion();
    const std::uint64_t noise = terrain.LayerVersion(0);
    const auto higher = std::make_shared<terraingen::FlatSource>(96, 64, 2.0, 9.0f);
    terrain.SetSource(higher);
    EXPECT_EQ(terrain.Source(), higher);
    EXPECT_NE(terrain.SourceVersion(), source);
    EXPECT_EQ(terrain.LayerVersion(0), noise);
    terraingen::Raster<float> degrees(4, 4, 1.0f);
    degrees.SetTransform(GeoTransform::NorthUp(10.0, 20.0, 0.25));
    EXPECT_THROW(
        terrain.SetSource(std::make_shared<terraingen::RasterSource>(
            degrees, terraingen::RasterSourceOptions{}, terraingen::GeographicSphereKeys(1000.0))),
        std::invalid_argument);
    EXPECT_EQ(terrain.Source(), higher) << "unchanged after a rejected source";
}

TEST(StageCache, KeepsTheHeightsBeforeItsStageAndRestartsFromThem) {
    Terrain terrain = Rolling();
    const RasterGrid grid = SourceGrid(terrain);
    StageCache cache(1);
    EXPECT_EQ(cache.Stage(), 1u);
    EXPECT_TRUE(cache.Empty());
    std::vector<std::size_t> applied;
    ExpectSameRaster(terrain.Evaluate(grid, Recording(applied).Keep(cache)),
                     terrain.Evaluate(grid));
    EXPECT_FALSE(cache.Empty()) << "the first evaluation fills it";
    EXPECT_EQ(applied, (std::vector<std::size_t>{0, 1}));

    terrain.SetLayer(1, Smoothed().Layers()[1]);
    applied.clear();
    const auto restarted = terrain.Evaluate(grid, Recording(applied).Keep(cache));
    EXPECT_EQ(applied, (std::vector<std::size_t>{1})) << "the noise is not applied again";
    ExpectSameRaster(restarted, terrain.Evaluate(grid));

    terrain.SetLayer(0, terrain.Layers()[0]);
    applied.clear();
    terrain.Evaluate(grid, Recording(applied).Keep(cache));
    EXPECT_EQ(applied, (std::vector<std::size_t>{0, 1})) << "a layer before the stage changed";

    cache.Clear();
    EXPECT_TRUE(cache.Empty());
    EXPECT_EQ(cache.Stage(), 1u);
}

TEST(StageCache, IsRefreshedForAnotherGridSpacingOrSource) {
    Terrain terrain = Rolling();
    const RasterGrid grid = SourceGrid(terrain);
    StageCache cache(1);
    std::vector<std::size_t> applied;
    terrain.Evaluate(grid, Recording(applied).Keep(cache));

    const RasterGrid tile{20, 10, grid.transform};
    applied.clear();
    ExpectSameRaster(terrain.Evaluate(tile, Recording(applied).Keep(cache)),
                     terrain.Evaluate(tile));
    EXPECT_EQ(applied, (std::vector<std::size_t>{0, 1})) << "another grid";

    applied.clear();
    ExpectSameRaster(terrain.Evaluate(tile, Recording(applied).Keep(cache).Spacing(5.0)),
                     terrain.Evaluate(tile, At(5.0)));
    EXPECT_EQ(applied, (std::vector<std::size_t>{0, 1})) << "another spacing";

    terrain.SetSource(std::make_shared<terraingen::FlatSource>(96, 64, 2.0, 9.0f));
    applied.clear();
    ExpectSameRaster(terrain.Evaluate(tile, Recording(applied).Keep(cache).Spacing(5.0)),
                     terrain.Evaluate(tile, At(5.0)));
    EXPECT_EQ(applied, (std::vector<std::size_t>{0, 1})) << "another source";

    applied.clear();
    terrain.Evaluate(tile, Recording(applied).Keep(cache).Spacing(5.0));
    EXPECT_EQ(applied, (std::vector<std::size_t>{1})) << "current again";
}

TEST(StageCache, RejectsAStagePastTheLastLayer) {
    const Terrain terrain = Rolling();
    StageCache last(2);
    EXPECT_NO_THROW(terrain.Evaluate(SourceGrid(terrain), EvaluationOptions{}.Keep(last)));
    StageCache past(3);
    EXPECT_THROW(terrain.Evaluate(SourceGrid(terrain), EvaluationOptions{}.Keep(past)),
                 std::invalid_argument);
}

TEST(Reseed, GivesTheSameHeightsForTheSameSeedAndOthersForAnother) {
    const Terrain terrain = Noisy();
    const Terrain one = terrain.Reseed(1);
    const auto first = one.Evaluate(kNoisyGrid);
    ExpectSameRaster(terrain.Reseed(1).Evaluate(kNoisyGrid), first);
    EXPECT_TRUE(Differ(terrain.Reseed(2).Evaluate(kNoisyGrid), first));
    EXPECT_TRUE(Differ(terrain.Evaluate(kNoisyGrid), first));
    EXPECT_NE(one.SourceVersion(), terrain.SourceVersion());
    EXPECT_NE(one.LayerVersion(0), terrain.LayerVersion(0));
    EXPECT_EQ(one.Grid(), terrain.Grid());
}

TEST(Reseed, KeepsMeasuredDataAndUnseededLayers) {
    const Terrain terrain(Surface{}, std::make_shared<terraingen::FlatSource>(16, 16, 1.0, 2.0f),
                          {std::make_shared<terraingen::ScaleLayer>(2.0, 1.0)});
    const Terrain reseeded = terrain.Reseed(42);
    EXPECT_EQ(reseeded.Source(), terrain.Source());
    EXPECT_EQ(reseeded.Layers()[0], terrain.Layers()[0]);
    EXPECT_EQ(reseeded.SourceVersion(), terrain.SourceVersion());
    EXPECT_EQ(reseeded.LayerVersion(0), terrain.LayerVersion(0));
    ExpectSameRaster(reseeded.Evaluate(), terrain.Evaluate());
}

TEST(Reseed, RebindsALayerReadingAReseededLayer) {
    const Terrain terrain = CratersAndBoulders();
    const Terrain reseeded = terrain.Reseed(7);
    ASSERT_NE(reseeded.Layers()[0], terrain.Layers()[0]) << "random craters have a seed";
    EXPECT_EQ(reseeded.Layers()[1]->Dependencies(),
              std::vector<const terraingen::Layer*>{reseeded.Layers()[0].get()});
    EXPECT_EQ(terrain.Layers()[1]->Dependencies(),
              std::vector<const terraingen::Layer*>{terrain.Layers()[0].get()})
        << "the original keeps its layers";
    ExpectSameRaster(terrain.Reseed(7).Evaluate(), reseeded.Evaluate());
}
