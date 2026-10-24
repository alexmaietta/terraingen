// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::CraterLayer;
using terraingen::CraterLayerOptions;
using terraingen::CraterList;
using terraingen::CraterModels;
using terraingen::GeoTransform;
using terraingen::Raster;

namespace {

CraterModels Models() {
    return {std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::UniformAges>(1.0),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
}

CraterLayerOptions Options() {
    CraterLayerOptions options;
    options.seed = 3;
    options.generation.dMin = 6.0;
    options.generation.dMax = 40.0;
    options.apply = terraingen::CraterApplyOptions{};
    return options;
}

Raster<float> Grid(double originX = -64.0, double originY = 64.0) {
    Raster<float> raster(64, 64, 0.0f);
    raster.SetTransform(GeoTransform::NorthUp(originX, originY, 2.0));
    return raster;
}

}  // namespace

TEST(CraterLayer, GeneratesAndAppliesCratersOverTheGrid) {
    const CraterLayer layer(Models(), Options());
    Raster<float> heights = Grid();
    layer.Apply(heights, {2.0});

    const CraterModels models = Models();
    const CraterList craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), {-64.0, -64.0, 64.0, 64.0}, *models.density, *models.ages, 3u,
        {6.0, 40.0, models.profile->ReachRadii()});
    Raster<float> expected = Grid();
    terraingen::ApplyCraters(expected, craters, models.Shape());
    ASSERT_GT(craters.Size(), 0u);
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const auto crater = craters.At(n);
        EXPECT_EQ(layer.Models().Shape().Attributes(crater),
                  terraingen::ComputeCraterAttributes(crater, models.degradation->State(crater),
                                                      *models.profile));
    }
    EXPECT_EQ(layer.Craters({-64.0, -64.0, 64.0, 64.0}).Ids(), craters.Ids());
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], expected.Values()[i]) << i;
    }
}

TEST(CraterLayer, AppliesOnlyManualCratersWithoutGenerating) {
    CraterLayerOptions options = Options();
    options.generateRandom = false;
    options.generation.manual.Append({0.0, 0.0, 30.0, 0.5, 0u});
    options.generation.manual.Append({5000.0, 0.0, 30.0, 0.5, 0u});
    const CraterLayer layer(Models(), options);
    const CraterList craters = layer.Craters({-64.0, -64.0, 64.0, 64.0});
    ASSERT_EQ(craters.Size(), 1u);
    EXPECT_TRUE(craters.At(0).manual);
    EXPECT_EQ(craters.At(0).id, terraingen::ManualCraterId(0.0, 0.0, 30.0));
    Raster<float> heights = Grid();
    layer.Apply(heights, {2.0});
    EXPECT_LT(heights(32, 32), -1.0f);
}

TEST(CraterLayer, ManualMakesALayerOfOnlyTheGivenCraters) {
    EXPECT_TRUE(CraterLayerOptions{}.generateRandom);
    CraterList craters;
    craters.Append({0.0, 0.0, 30.0, 0.5, 0u});
    craters.Append({5000.0, 0.0, 30.0, 0.5, 0u});
    terraingen::CraterApplyOptions apply;
    apply.interiorReplacement = 0.5;
    const auto layer = CraterLayer::Manual(craters, Models(), apply);
    EXPECT_FALSE(layer->Options().generateRandom);
    EXPECT_EQ(layer->Options().apply.interiorReplacement, 0.5);
    EXPECT_EQ(layer->Options().generation.manual.Ids(), craters.Ids());
    EXPECT_EQ(layer->MaxFeatureDiameter(), 30.0);

    CraterLayerOptions options;
    options.generateRandom = false;
    options.generation.manual = craters;
    options.apply = apply;
    const CraterLayer same(Models(), options);
    const terraingen::Bounds2d region{-64.0, -64.0, 64.0, 64.0};
    ASSERT_EQ(layer->Craters(region).Size(), 1u);
    EXPECT_EQ(layer->Craters(region).Ids(), same.Craters(region).Ids());
    Raster<float> heights = Grid();
    layer->Apply(heights, {2.0});
    Raster<float> expected = Grid();
    same.Apply(expected, {2.0});
    EXPECT_TRUE(
        std::equal(heights.Values().begin(), heights.Values().end(), expected.Values().begin()));

    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    EXPECT_EQ(CraterLayer::Manual(craters, Models(), {}, moon)->Options().surface, moon);
    EXPECT_THROW(CraterLayer::Manual(craters, CraterModels{}), std::invalid_argument);
}

TEST(CraterLayer, DrawsTheAgesItsDegradationWearsByWhenGivenNone) {
    const auto ages = std::make_shared<terraingen::UniformAges>(2.0);
    CraterModels models = Models();
    models.ages.reset();
    models.degradation = std::make_shared<terraingen::FreshnessDegradation>(
        ages, terraingen::LunarFreshCraterMorphometry);
    EXPECT_EQ(models.degradation->AgeModel(), ages);
    const CraterLayer layer(models, Options());
    EXPECT_EQ(layer.Models().ages, ages);
    CraterModels other = models;
    other.ages = std::make_shared<terraingen::FreshAges>();
    EXPECT_EQ(CraterLayer(other, Options()).Models().ages, other.ages)
        << "ages given apart from the degradation's are kept";
    EXPECT_EQ(terraingen::NoDegradation(terraingen::LunarFreshCraterMorphometry).AgeModel(),
              nullptr);
}

TEST(CraterLayer, DrawsUnsetShapingSeedsFromItsOwn) {
    CraterLayerOptions options = Options();
    options.seed = 7;
    options.apply.noise.seed = 3;
    const CraterLayer layer(Models(), options);
    const terraingen::CraterApplyOptions& apply = layer.Options().apply;
    EXPECT_EQ(apply.noise.seed, 3u) << "a given seed is kept";
    ASSERT_TRUE(apply.irregularity.seed && apply.texture.seed && apply.slope.seed);
    options.seed = 8;
    const CraterLayer other(Models(), options);
    EXPECT_NE(other.Options().apply.irregularity.seed, apply.irregularity.seed);
    EXPECT_NE(*apply.irregularity.seed, *apply.texture.seed) << "each on its own substream";
    EXPECT_EQ(terraingen::SeededCraterApply(apply, 99), apply) << "set seeds stay";
}

TEST(CraterLayer, ManualNeedsOnlyItsCraters) {
    CraterList craters;
    craters.Append(terraingen::ManualCrater(0.0, 0.0, 30.0).Depth(4.0));
    const auto layer = CraterLayer::Manual(craters);
    EXPECT_EQ(layer->Options().apply, terraingen::SeededCraterApply(
                                          terraingen::DefaultCraterApply(), layer->Options().seed));
    Raster<float> heights = Grid();
    layer->Apply(heights, {2.0});
    EXPECT_LT(*std::min_element(heights.Values().begin(), heights.Values().end()), -2.0f)
        << "a 4 m deep crater, measured from its rim";

    CraterModels shapesOnly = Models();
    shapesOnly.density.reset();
    shapesOnly.ages.reset();
    EXPECT_NO_THROW(CraterLayer::Manual(craters, shapesOnly));
    EXPECT_THROW(CraterLayer(shapesOnly, Options()), std::invalid_argument)
        << "generating craters needs the density and ages";
}

TEST(CraterLayer, RejectsInvalidManualCratersWithoutGenerating) {
    const double nan = std::nan("");
    for (const terraingen::Crater& bad :
         {terraingen::Crater{nan, 0.0, 30.0, 0.5, 0u}, terraingen::Crater{0.0, 0.0, -30.0, 0.5, 0u},
          terraingen::Crater{5000.0, 0.0, 30.0, -1.0, 0u}}) {
        CraterLayerOptions options = Options();
        options.generateRandom = false;
        options.generation.manual.Append(bad);
        const CraterLayer layer(Models(), options);
        EXPECT_THROW(layer.Craters({-64.0, -64.0, 64.0, 64.0}), std::invalid_argument);
    }
}

TEST(CraterLayer, FadesCratersOutAsTheSpacingCoarsens) {
    CraterLayerOptions options = Options();
    options.generateRandom = false;
    options.generation.manual.Append({0.0, 0.0, 10.0, 0.0, 0u});
    const CraterLayer layer(Models(), options);
    const auto floor = [&](double spacing) {
        Raster<float> heights = Grid();
        layer.Apply(heights, {spacing});
        return heights(32, 32);
    };
    const float whole = floor(2.0);
    EXPECT_LT(whole, -0.5f) << "5 samples across: whole";
    const float faded = floor(4.0);
    EXPECT_LT(faded, 0.0f) << "2.5 samples across: partly faded";
    EXPECT_GT(faded, whole);
    EXPECT_EQ(floor(5.0), 0.0f) << "2 samples across: gone";
    const double weight = terraingen::ResolutionWeight(10.0, 4.0, terraingen::ResolutionFade{});
    EXPECT_NEAR(faded, weight * static_cast<double>(whole), 1e-5);
}

TEST(CraterLayer, RejectsMissingModelsAndInvalidOptions) {
    CraterModels missing = Models();
    missing.profile.reset();
    EXPECT_THROW(CraterLayer(missing, Options()), std::invalid_argument);
    CraterLayerOptions options = Options();
    options.apply.interiorReplacement = 2.0;
    EXPECT_THROW(CraterLayer(Models(), options), std::invalid_argument);
    options = Options();
    options.generation.spacing.diameterRatio = 0.5;
    EXPECT_THROW(CraterLayer(Models(), options), std::invalid_argument);
    const CraterLayer layer(Models(), Options());
    EXPECT_EQ(layer.Options().generation.marginRadii, layer.Models().profile->ReachRadii());
}

TEST(CraterLayer, PointQueriesNeedTheGroundForDatumsAndNoCap) {
    CraterLayerOptions options = Options();
    options.apply.datum = terraingen::CraterDatum::Plane;
    EXPECT_THROW(CraterLayer(Models(), options).ApplyAt(0.0f, 0.0, 0.0, {2.0}),
                 std::invalid_argument)
        << "a datum needs the ground beneath the layer";
    options = Options();
    options.generation.maxCraters = 5;
    EXPECT_THROW(CraterLayer(Models(), options).ApplyAt(0.0f, 0.0, 0.0, {2.0}),
                 std::invalid_argument);
    const CraterLayer layer(Models(), Options());
    EXPECT_THROW(layer.ApplyAt(0.0f, 0.0, 0.0, {0.0}), std::invalid_argument);
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {2.0})));
}

TEST(CraterLayer, CachesCellsAndWorksWithoutACache) {
    const CraterLayer cached(Models(), Options());
    CraterLayerOptions options = Options();
    options.cacheCraters = 0;
    const CraterLayer uncached(Models(), options);
    const terraingen::Bounds2d region{-64.0, -64.0, 64.0, 64.0};
    EXPECT_EQ(cached.Craters(region).Ids(), uncached.Craters(region).Ids());
    EXPECT_EQ(cached.Craters(region).Ids(), uncached.Craters(region).Ids());
    EXPECT_EQ(cached.ApplyAt(1.0f, 3.0, -7.0, {2.0}), uncached.ApplyAt(1.0f, 3.0, -7.0, {2.0}));
}

TEST(CraterLayer, ReportsItsLargestCrater) {
    EXPECT_EQ(CraterLayer(Models(), Options()).MaxFeatureDiameter(), 40.0);
    CraterLayerOptions options = Options();
    options.generateRandom = false;
    options.generation.manual.Append({0.0, 0.0, 75.0, 0.0, 0u});
    options.generation.manual.Append({0.0, 0.0, 12.0, 0.0, 0u});
    EXPECT_EQ(CraterLayer(Models(), options).MaxFeatureDiameter(), 75.0);
}

TEST(CraterLayer, ChangesNothingUnderAMapOfOne) {
    CraterLayerOptions options = Options();
    options.densityMap = terraingen::DensityMap{std::make_shared<terraingen::ConstantField>(1.0)};
    const terraingen::Bounds2d region{-300.0, -300.0, 300.0, 300.0};
    EXPECT_EQ(CraterLayer(Models(), options).Craters(region).Ids(),
              CraterLayer(Models(), Options()).Craters(region).Ids());
}

TEST(CraterLayer, SetsTheDensityAMapCountsAndLetsACatalogStandIn) {
    CraterLayerOptions options = Options();
    const double perKm2 = 2.0 * 0.05 / 36.0 * 1e6;
    terraingen::DensityMap map{std::make_shared<terraingen::ConstantField>(perKm2),
                               terraingen::DensityMeasure::Count, 6.0};
    map.areaUnit = 1e6;
    options.densityMap = map;
    options.generation.manual.Append({0.0, 0.0, 30.0, 1.0, 0u, true});
    options.generation.completeness = std::make_shared<terraingen::ConstantField>(20.0);
    const terraingen::Bounds2d region{-1000.0, -1000.0, 1000.0, 1000.0};
    const CraterList craters = CraterLayer(Models(), options).Craters(region);
    std::size_t inside = 0;
    std::size_t manual = 0;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const terraingen::Crater crater = craters.At(n);
        manual += crater.manual ? 1u : 0u;
        if (!crater.manual) {
            ASSERT_LT(crater.diameter, 20.0) << "the catalog stands in from 20 m";
            inside += region.Contains(crater.x, crater.y) ? 1u : 0u;
        }
    }
    EXPECT_EQ(manual, 1u);
    const double generatedShare = (1.0 / 36.0 - 1.0 / 400.0) / (1.0 / 36.0);
    const double expected = perKm2 * 4.0 * generatedShare;
    EXPECT_NEAR(static_cast<double>(inside), expected, 0.04 * expected);
}

namespace {

/// The models of a layer that shapes craters with the Martin-Parkes-Dunstan profile, worn with age.
CraterModels ShapedModels() {
    CraterModels models = Models();
    models.degradation = std::make_shared<terraingen::FreshnessDegradation>(
        std::make_shared<terraingen::UniformAges>(2.0), terraingen::LunarFreshCraterMorphometry);
    models.profile = std::make_shared<terraingen::MartinParkesDunstanProfile>();
    return models;
}

/// A layer with one manual 40 m crater at the origin, `depth` deep with rim `rim`.
CraterLayer ExplicitLayer(bool generateRandom, double depth, double rim) {
    CraterLayerOptions options = Options();
    options.generateRandom = generateRandom;
    options.generation.manual.Append({0.0, 0.0, 40.0, 1.5, 0u, false, depth, rim});
    return CraterLayer(ShapedModels(), options);
}

/// A `size` x `size` flat grid of `cell` m cells centered on the origin.
Raster<float> Centered(std::size_t size, double cell) {
    Raster<float> raster(size, size, 0.0f);
    const double half = 0.5 * static_cast<double>(size) * cell;
    raster.SetTransform(GeoTransform::NorthUp(-half, half, cell));
    return raster;
}

}  // namespace

TEST(CraterLayer, ShapesAManualCraterToItsExplicitDepthAndRim) {
    const CraterLayer layer = ExplicitLayer(false, 6.0, 1.5);
    Raster<float> heights = Centered(200, 0.5);
    layer.Apply(heights, {0.5});
    const auto [low, high] = std::minmax_element(heights.Values().begin(), heights.Values().end());
    EXPECT_NEAR(static_cast<double>(*high), 1.5, 0.05);
    EXPECT_NEAR(static_cast<double>(*high) - static_cast<double>(*low), 6.0, 0.1);
    const terraingen::CraterAttributes attributes =
        layer.Models().Shape().Attributes(layer.Craters({-50.0, -50.0, 50.0, 50.0}).At(0));
    EXPECT_NEAR(attributes.rimHeight, 1.5, 0.05);
    EXPECT_NEAR(attributes.floorDepth + attributes.rimHeight, 6.0, 0.05);
}

TEST(CraterLayer, LeavesOtherCratersAsTheyWereBesideAnExplicitOne) {
    const double nan = std::nan("");
    const CraterLayer plain = ExplicitLayer(true, nan, nan);
    const CraterLayer shaped = ExplicitLayer(true, 6.0, 1.5);
    const terraingen::Bounds2d region{-64.0, -64.0, 64.0, 64.0};
    const CraterList before = plain.Craters(region);
    const CraterList after = shaped.Craters(region);
    ASSERT_EQ(before.Ids(), after.Ids());
    ASSERT_GT(before.Size(), 1u);
    for (std::size_t n = 0; n < before.Size(); ++n) {
        if (!before.At(n).manual) {
            EXPECT_EQ(plain.Models().Shape().Attributes(before.At(n)),
                      shaped.Models().Shape().Attributes(after.At(n)));
        }
    }
    Raster<float> a = Grid();
    Raster<float> b = Grid();
    plain.Apply(a, {2.0});
    shaped.Apply(b, {2.0});
    const double reach = 2.0 * 20.0 + 2.0;
    std::size_t compared = 0;
    for (std::size_t row = 0; row < a.Height(); ++row) {
        for (std::size_t col = 0; col < a.Width(); ++col) {
            const auto center = a.CellCenter(col, row);
            if (std::hypot(center.x, center.y) > reach) {
                ASSERT_EQ(a(col, row), b(col, row)) << col << " " << row;
                ++compared;
            }
        }
    }
    EXPECT_GT(compared, 1000u);
}

TEST(CraterLayer, RejectsAManualRimHeightWithoutADepth) {
    const CraterLayer layer = ExplicitLayer(false, std::nan(""), 1.0);
    EXPECT_THROW(layer.Craters({-64.0, -64.0, 64.0, 64.0}), std::invalid_argument);
}
