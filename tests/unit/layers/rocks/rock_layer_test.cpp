#include "terraingen/layers/rocks/rock_layer.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>

using terraingen::Bounds2d;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::Rock;
using terraingen::RockLayer;
using terraingen::RockLayerOptions;

namespace {

/// Craters with no generation, only `manual` ones, fresh.
std::shared_ptr<const terraingen::CraterLayer>
Craters(const std::vector<terraingen::Crater>& manual) {
    terraingen::CraterLayerOptions options;
    options.generate = false;
    for (const auto& crater : manual) {
        options.generation.manual.Append(crater);
    }
    return std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
                                 std::make_shared<terraingen::FreshAges>(),
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     std::make_shared<terraingen::UniformAges>(1.0),
                                     terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
}

class Analytic final : public terraingen::Ground {
  public:
    explicit Analytic(std::function<float(double, double)> height) : height_(std::move(height)) {}
    float HeightAt(double x, double y, double /*spacing*/) const override {
        return height_(x, y);
    }

  private:
    std::function<float(double, double)> height_;
};

}  // namespace

TEST(RockLayer, GivesEachRockStableAttributesInTheirRanges) {
    RockLayerOptions options;
    options.coverage = 0.05;
    const RockLayer layer(options);
    const auto rocks = layer.Rocks({-20.0, -20.0, 20.0, 20.0});
    ASSERT_GT(rocks.size(), 50u);
    std::set<std::uint64_t> ids;
    for (const Rock& rock : rocks) {
        EXPECT_GE(rock.diameter, 0.1);
        EXPECT_LT(rock.diameter, 4.0);
        EXPECT_GE(rock.height / rock.diameter, 0.4 - 1e-12);
        EXPECT_LE(rock.height / rock.diameter, 0.6 + 1e-12);
        EXPECT_GE(rock.burial, 0.1);
        EXPECT_LE(rock.burial, 0.4);
        EXPECT_GE(rock.yaw, 0.0);
        EXPECT_LT(rock.yaw, 2.0 * std::numbers::pi);
        EXPECT_LE(rock.lean, 0.26);
        EXPECT_DOUBLE_EQ(rock.ExposedHeight(), rock.height * (1.0 - rock.burial));
        ids.insert(rock.id);
    }
    EXPECT_EQ(ids.size(), rocks.size());
    EXPECT_EQ(layer.Rocks({-20.0, -20.0, 20.0, 20.0}), rocks);
}

TEST(RockLayer, GivesTheSameRocksInEveryRegionAndSkipsOnlyUnresolvedOnes) {
    RockLayerOptions options;
    options.coverage = 0.05;
    options.patches = {2.0, 30.0};
    const RockLayer layer(options);
    const auto whole = layer.Rocks({-30.0, -30.0, 30.0, 30.0});
    const auto part = layer.Rocks({-5.0, 0.0, 10.0, 12.0});
    std::set<std::uint64_t> wholeIds;
    for (const Rock& rock : whole) {
        wholeIds.insert(rock.id);
    }
    for (const Rock& rock : part) {
        EXPECT_TRUE(wholeIds.count(rock.id)) << rock.id;
    }
    const auto coarse = layer.Rocks({-30.0, -30.0, 30.0, 30.0}, 0.5);
    std::set<std::uint64_t> resolved;
    for (const Rock& rock : whole) {
        if (rock.diameter >= 2.0 * 0.5) {
            resolved.insert(rock.id);
        }
    }
    std::set<std::uint64_t> kept;
    for (const Rock& rock : coarse) {
        if (rock.diameter >= 1.0) {
            kept.insert(rock.id);
        }
    }
    EXPECT_EQ(kept, resolved);
    EXPECT_LT(coarse.size(), whole.size());
}

TEST(RockLayer, AddsBlocksAroundFreshCraters) {
    RockLayerOptions options;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.coverage = 0.15;
    const auto craters = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    const RockLayer layer(options, craters);
    EXPECT_EQ(layer.AbundanceAt(50.0, 0.0).ejecta, 1.0) << "at the rim";
    EXPECT_EQ(layer.AbundanceAt(10.0, 5.0).ejecta, 1.0) << "on the floor";
    const double halfway = layer.AbundanceAt(100.0, 0.0).ejecta;
    EXPECT_GT(halfway, 0.0);
    EXPECT_LT(halfway, 1.0);
    EXPECT_EQ(layer.AbundanceAt(160.0, 0.0).ejecta, 0.0) << "beyond 3 radii";
    EXPECT_EQ(layer.AbundanceAt(160.0, 0.0).background, 1.0);
    EXPECT_DOUBLE_EQ(layer.DensityAt(1.0, 50.0, 0.0),
                     terraingen::GolombekRappSfd(0.02).Incremental(1.0) +
                         terraingen::GolombekRappSfd(0.15).Incremental(1.0))
        << "crater blocks add to the rocks between craters";
    const auto nearRim = layer.Rocks({40.0, -10.0, 60.0, 10.0});
    const auto far = layer.Rocks({300.0, -10.0, 320.0, 10.0});
    EXPECT_GT(nearRim.size(), 3 * far.size());
    EXPECT_THROW(RockLayer{options}, std::invalid_argument) << "ejecta need craters";

    options.ejecta.minCraterDiameter = 200.0;
    EXPECT_EQ(RockLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 0.0)
        << "smaller craters did not dig to rock";
    options.ejecta.minCraterDiameter = 0.0;
    options.ejecta.regolithThickness = 8.0;
    EXPECT_EQ(options.ejecta.MinCraterDiameter(), 80.0) << "excavating a tenth of the diameter";
    EXPECT_EQ(RockLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 1.0);
    options.ejecta.regolithThickness = 15.0;
    EXPECT_EQ(RockLayer(options, craters).AbundanceAt(50.0, 0.0).ejecta, 0.0);
}

TEST(RockLayer, ShapesAndAgesCraterBlocksLikeSynthterrainWhenAsked) {
    RockLayerOptions options;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.profile = terraingen::EjectaProfile::Exponential;
    options.ejecta.reach = 2.0;
    options.ejecta.aging = terraingen::EjectaAging::Age;
    const RockLayer fresh(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}}));
    EXPECT_DOUBLE_EQ(fresh.AbundanceAt(55.0, 0.0).ejecta, std::exp(-0.1 / 0.7));
    EXPECT_DOUBLE_EQ(fresh.AbundanceAt(10.0, 0.0).ejecta, 0.05) << "the floor's share";
    EXPECT_EQ(fresh.AbundanceAt(101.0, 0.0).ejecta, 0.0) << "beyond 2 radii";
    const RockLayer old(options, Craters({{0.0, 0.0, 100.0, 2.0, 0u}}));
    EXPECT_DOUBLE_EQ(old.AbundanceAt(55.0, 0.0).ejecta, 0.125 * std::exp(-0.1 / 0.7))
        << "(1 - 2 / 4)^3 at 2 Gyr";
    const RockLayer gone(options, Craters({{0.0, 0.0, 100.0, 5.0, 0u}}));
    EXPECT_EQ(gone.AbundanceAt(55.0, 0.0).ejecta, 0.0);
}

TEST(RockLayer, LimitsCraterBlocksToHowFarTheyAreThrownOverRegolith) {
    EXPECT_NEAR(terraingen::BoulderThrowDistance(2200.0, 4.0, 1.62), 7500.0, 100.0)
        << "Linne: about 7.5 km over 4 m of regolith (Pajola et al. 2019)";
    EXPECT_THROW(terraingen::BoulderThrowDistance(0.0, 4.0, 1.62), std::invalid_argument);
    EXPECT_THROW(terraingen::BoulderThrowDistance(10.0, 4.0, -1.0), std::invalid_argument);
    RockLayerOptions options;
    options.ejecta.regolithThickness = 60.0;
    options.ejecta.excavationDepthRatio = 1.0;
    const auto craters = Craters({{0.0, 0.0, 100.0, 0.0, 0u}});
    EXPECT_GT(RockLayer(options, craters).AbundanceAt(130.0, 0.0).ejecta, 0.0);
    options.ejecta.gravity = 1.62;
    const double thrown = terraingen::BoulderThrowDistance(100.0, 60.0, 1.62);
    ASSERT_LT(thrown, 130.0);
    EXPECT_EQ(RockLayer(options, craters).AbundanceAt(130.0, 0.0).ejecta, 0.0);
    EXPECT_GT(RockLayer(options, craters).AbundanceAt(thrown - 5.0, 0.0).ejecta, 0.0);
}

TEST(RockLayer, DrawsRocksAndCraterBlocksFromAnyDistribution) {
    RockLayerOptions options;
    options.distribution =
        terraingen::SynthterrainRockDistribution(terraingen::SynthterrainRocks::ViperSpecification);
    options.minDiameter = 1.0;
    options.maxDiameter = 8.0;
    options.ejecta.minCraterDiameter = 50.0;
    options.ejecta.distribution =
        terraingen::Sp8023CraterBlocks(terraingen::RockModelCurve::Nominal);
    const RockLayer layer(options, Craters({{0.0, 0.0, 100.0, 0.0, 0u}}));
    EXPECT_DOUBLE_EQ(layer.DensityAt(2.0, 400.0, 0.0), options.distribution->Incremental(2.0));
    EXPECT_DOUBLE_EQ(layer.DensityAt(2.0, 50.0, 0.0),
                     options.distribution->Incremental(2.0) +
                         options.ejecta.distribution->Incremental(2.0));
    const auto rocks = layer.Rocks({-100.0, -100.0, 100.0, 100.0});
    ASSERT_FALSE(rocks.empty());
    for (const Rock& rock : rocks) {
        EXPECT_GE(rock.diameter, 1.0);
        EXPECT_LT(rock.diameter, 8.0);
    }
    options.minDiameter = 0.1;
    EXPECT_THROW(RockLayer(options, Craters({})), std::invalid_argument)
        << "the crater table starts at 0.85 m";
    auto extrapolated = terraingen::Sp8023CraterBlocks(terraingen::RockModelCurve::Nominal);
    extrapolated->SetAllowExtrapolation(true);
    options.ejecta.distribution = extrapolated;
    EXPECT_NO_THROW(RockLayer(options, Craters({})));
}

TEST(RockLayer, RaisesTheAbundanceWhereBedrockShows) {
    RockLayerOptions options;
    options.patches.spread = 3.0;
    options.patches.size = 1e6;
    options.coverage = 0.01;
    options.exposure = terraingen::ExposureParams{};
    const RockLayer layer(options);
    const Analytic steep([](double x, double) { return static_cast<float>(0.8 * x); });
    const Analytic level([](double, double) { return 0.0f; });
    const double soil = layer.AbundanceAt(100.0, 0.0, &level).background;
    const double bedrock = layer.AbundanceAt(100.0, 0.0, &steep).background;
    EXPECT_NEAR(bedrock, 3.0 * soil, 1e-12) << "bedrockAbundance times as many";
    EXPECT_THROW(layer.AbundanceAt(0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(layer.Rocks({0.0, 0.0, 1.0, 1.0}), std::invalid_argument);
}

TEST(RockLayer, AddsDomesThatPointQueriesMatchToTheBit) {
    RockLayerOptions options;
    options.coverage = 0.1;
    options.minDiameter = 0.5;
    const RockLayer layer(options);
    Raster<float> heights(80, 60, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(-20.0, 15.0, 0.5));
    terraingen::TerrainFeatures features;
    layer.Apply(heights, {0.5, 4}, features);
    ASSERT_FALSE(features.rocks.empty());
    bool raised = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, centre.x, centre.y, {0.5}), heights(col, row));
            raised = raised || heights(col, row) > 1.0f;
        }
    }
    EXPECT_TRUE(raised);
    Raster<float> serial(80, 60, 1.0f);
    serial.SetTransform(heights.Transform());
    terraingen::TerrainFeatures again;
    layer.Apply(serial, {0.5, 1}, again);
    EXPECT_TRUE(
        std::equal(serial.Values().begin(), serial.Values().end(), heights.Values().begin()));
    EXPECT_EQ(again.rocks, features.rocks);
}

TEST(AddRock, IsADomeOfTheExposedHeight) {
    const Rock rock{0.0, 0.0, 2.0, 1.0, 0.25, 0.0, 0.0, 1u};
    EXPECT_FLOAT_EQ(terraingen::AddRock(5.0f, 0.0, 0.0, rock, 1.0), 5.75f);
    EXPECT_FLOAT_EQ(terraingen::AddRock(5.0f, 0.6, 0.0, rock, 1.0), 5.0f + 0.75f * 0.8f);
    EXPECT_FLOAT_EQ(terraingen::AddRock(5.0f, 0.0, 0.0, rock, 0.5), 5.375f);
    EXPECT_EQ(terraingen::AddRock(5.0f, 1.0, 0.0, rock, 1.0), 5.0f);
}

TEST(RockLayer, LeavesHeightsAloneWithoutReliefAndRejectsBadOptions) {
    RockLayerOptions options;
    options.coverage = 0.1;
    options.relief = terraingen::RockRelief::None;
    const RockLayer layer(options);
    Raster<float> heights(120, 120, 2.0f);
    heights.SetTransform(GeoTransform::NorthUp(0.0, 60.0, 0.5));
    terraingen::TerrainFeatures features;
    layer.Apply(heights, {0.5}, features);
    EXPECT_FALSE(features.rocks.empty());
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, 2.0f);
    }
    EXPECT_EQ(layer.ApplyAt(2.0f, 1.0, 1.0, {0.5}), 2.0f);
    EXPECT_TRUE(std::isnan(RockLayer({}).ApplyAt(std::nanf(""), 1.0, 1.0, {0.5})));
    EXPECT_THROW(RockLayer({}).ApplyAt(0.0f, 1.0, 1.0, {0.0}), std::invalid_argument);
    EXPECT_EQ(layer.MaxFeatureDiameter(), 4.0);
    const std::vector<std::function<void(RockLayerOptions&)>> changes{
        [](RockLayerOptions& o) { o.coverage = 0.0; },
        [](RockLayerOptions& o) { o.maxDiameter = 0.05; },
        [](RockLayerOptions& o) { o.patches.spread = 0.5; },
        [](RockLayerOptions& o) { o.patches.spread = 60.0; },
        [](RockLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.coverage = 2.0;
        },
        [](RockLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.floorShare = 2.0;
        },
        [](RockLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.lifetimeGyr = 0.0;
        },
        [](RockLayerOptions& o) {
            o.ejecta.minCraterDiameter = 10.0;
            o.ejecta.gravity = 0.0;
        },
        [](RockLayerOptions& o) { o.ejecta.regolithThickness = -1.0; },
        [](RockLayerOptions& o) { o.ejecta.excavationDepthRatio = 0.0; },
        [](RockLayerOptions& o) { o.bedrockAbundance = 0.0; },
        [](RockLayerOptions& o) {
            o.exposure = terraingen::ExposureParams{};
            o.coverage = 0.4;
        },
        [](RockLayerOptions& o) {
            o.distribution = std::make_shared<terraingen::PowerLawSfd>(
                1.0, 2.0, terraingen::SfdKind::ProductionRate);
        },
        [](RockLayerOptions& o) { o.ejecta.minCraterDiameter = -1.0; },
        [](RockLayerOptions& o) { o.exposureSpacing = 0.0; },
        [](RockLayerOptions& o) { o.exposure = terraingen::ExposureParams{.reach = 0.0}; },
        [](RockLayerOptions& o) { o.maxHeightRatio = 0.1; },
        [](RockLayerOptions& o) { o.maxBurial = 1.0; },
        [](RockLayerOptions& o) { o.maxLean = -1.0; },
        [](RockLayerOptions& o) { o.fade = {3.0, 3.0}; }};
    for (const auto& bad : changes) {
        RockLayerOptions invalid;
        bad(invalid);
        EXPECT_THROW(terraingen::ValidateRockLayerOptions(invalid), std::invalid_argument);
    }
}

TEST(RockModels, SplitsRocksByDiameterOrBySamplesAtTheSpacing) {
    const terraingen::RockModels none;
    EXPECT_FALSE(none.IsModel(100.0, 1.0));
    const terraingen::RockModels big{.minDiameter = 2.0};
    EXPECT_TRUE(big.IsModel(2.0, 0.0));
    EXPECT_FALSE(big.IsModel(1.9, 0.0));
    const terraingen::RockModels resolved{.minSamples = 8.0};
    EXPECT_TRUE(resolved.IsModel(1.0, 0.125)) << "eight samples across at 12.5 cm";
    EXPECT_FALSE(resolved.IsModel(1.0, 0.5)) << "relief in a coarser level of detail";
    EXPECT_FALSE(resolved.IsModel(1.0, 0.0));
}

TEST(RockLayer, PlacesLargeRocksAsModelsOnTheGroundAndLeavesThemOutOfTheHeights) {
    RockLayerOptions options;
    options.coverage = 0.15;
    options.minDiameter = 0.5;
    options.models.minDiameter = 1.0;
    const RockLayer layer(options);
    Raster<float> heights(160, 160, 2.0f);
    heights.SetTransform(GeoTransform::NorthUp(-40.0, 40.0, 0.5));
    terraingen::TerrainFeatures features;
    layer.Apply(heights, {0.5, 2}, features);
    std::size_t models = 0;
    for (const Rock& rock : features.rocks) {
        EXPECT_EQ(rock.model, rock.diameter >= 1.0);
        models += rock.model ? 1 : 0;
        if (rock.model && heights.Transform().WorldToPixel(rock.x, rock.y).x > 1.0) {
            ASSERT_TRUE(rock.ground.has_value());
            EXPECT_FLOAT_EQ(static_cast<float>(*rock.ground), 2.0f) << "the heights as given";
        }
    }
    ASSERT_GT(models, 0u);
    EXPECT_LE(features.boulders.size(), models);
    EXPECT_GT(features.boulders.size(), 0u);
    for (const auto& boulder : features.boulders) {
        EXPECT_TRUE(boulder.rock.model);
        terraingen::TriangleMesh placed = boulder.shape->mesh;
        for (auto& p : placed.positions) {
            p = boulder.placement.Apply(p);
        }
        const auto box = terraingen::Bounds(placed);
        EXPECT_NEAR(box.min.z, 2.0 - boulder.rock.burial * (box.max.z - box.min.z), 1e-9);
        EXPECT_NEAR(0.5 * (box.min.x + box.max.x), boulder.rock.x, boulder.rock.diameter);
    }

    options.models.minDiameter = options.minDiameter;
    Raster<float> flat(160, 160, 2.0f);
    flat.SetTransform(heights.Transform());
    terraingen::TerrainFeatures all;
    RockLayer(options).Apply(flat, {0.5}, all);
    for (const float value : flat.Values()) {
        ASSERT_EQ(value, 2.0f) << "models add nothing to the heights";
    }
    EXPECT_EQ(RockLayer(options).ApplyAt(2.0f, 1.0, 1.0, {0.5}), 2.0f);
}

TEST(RockLayer, ReadsTheGroundBeneathEachRock) {
    RockLayerOptions options;
    options.coverage = 0.15;
    options.minDiameter = 1.0;
    options.models.minDiameter = 1.0;
    const RockLayer layer(options);
    const Analytic slope([](double x, double) { return static_cast<float>(0.1 * x); });
    const auto rocks = layer.Rocks({0.0, 0.0, 40.0, 40.0}, 0.5, &slope);
    ASSERT_FALSE(rocks.empty());
    for (const Rock& rock : rocks) {
        ASSERT_TRUE(rock.ground.has_value());
        EXPECT_NEAR(*rock.ground, 0.1 * rock.x, 1e-5);
    }
    EXPECT_FALSE(layer.Rocks({0.0, 0.0, 40.0, 40.0}, 0.5).front().ground.has_value());
}

TEST(RockLayer, DrawsTheTopsOfBoulderShapesThatPointQueriesMatch) {
    RockLayerOptions options;
    options.coverage = 0.1;
    options.minDiameter = 0.5;
    options.relief = terraingen::RockRelief::Shape;
    options.shapes.subdivisions = 2;
    const RockLayer layer(options);
    Raster<float> heights(60, 50, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(-15.0, 12.5, 0.5));
    terraingen::TerrainFeatures features;
    layer.Apply(heights, {0.5, 3}, features);
    ASSERT_FALSE(features.rocks.empty());
    double highest = 0.0;
    double tallest = 0.0;
    for (const Rock& rock : features.rocks) {
        tallest = std::fmax(tallest, rock.ExposedHeight());
    }
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, centre.x, centre.y, {0.5}), heights(col, row));
            highest = std::fmax(highest, static_cast<double>(heights(col, row)) - 1.0);
        }
    }
    EXPECT_GT(highest, 0.0);
    EXPECT_LE(highest, 2.0 * tallest) << "overlapping rocks add up";
    options.relief = terraingen::RockRelief::Dome;
    Raster<float> domes(60, 50, 1.0f);
    domes.SetTransform(heights.Transform());
    terraingen::TerrainFeatures again;
    RockLayer(options).Apply(domes, {0.5}, again);
    EXPECT_FALSE(std::equal(domes.Values().begin(), domes.Values().end(), heights.Values().begin()))
        << "shapes are not domes";
}

TEST(RockLayer, SharesShapesBetweenRocksOfOnePrototype) {
    RockLayerOptions options;
    options.shapePrototypes = 1;
    const RockLayer layer(options);
    EXPECT_EQ(layer.Shape(Rock{.id = 1}).get(), layer.Shape(Rock{.id = 99}).get());
    options.shapePrototypes = 0;
    const RockLayer unique(options);
    EXPECT_NE(unique.Shape(Rock{.id = 1})->mesh.positions,
              unique.Shape(Rock{.id = 99})->mesh.positions);
    options.models.minSamples = -1.0;
    EXPECT_THROW(RockLayer{options}, std::invalid_argument);
}
