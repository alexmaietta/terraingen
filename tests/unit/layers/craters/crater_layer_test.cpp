#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::CraterLayer;
using terraingen::CraterLayerOptions;
using terraingen::CraterList;
using terraingen::CraterModels;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::TerrainFeatures;

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
    TerrainFeatures features;
    layer.Apply(heights, {2.0}, features);

    const CraterModels models = Models();
    const CraterList craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), {-64.0, -64.0, 64.0, 64.0}, *models.density, *models.ages, 3u,
        {.dMin = 6.0, .dMax = 40.0, .marginRadii = models.profile->ReachRadii()});
    Raster<float> expected = Grid();
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation);
    ASSERT_GT(craters.Size(), 0u);
    EXPECT_EQ(features.craters.Ids(), craters.Ids());
    ASSERT_EQ(features.craterAttributes.size(), craters.Size());
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const auto crater = craters.At(n);
        EXPECT_EQ(features.craterAttributes[n],
                  terraingen::ComputeCraterAttributes(crater, models.degradation->State(crater),
                                                      *models.profile));
        EXPECT_EQ(features.craterAttributes[n], layer.Attributes(crater));
    }
    EXPECT_EQ(layer.Craters({-64.0, -64.0, 64.0, 64.0}).Ids(), craters.Ids());
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], expected.Values()[i]) << i;
    }
}

TEST(CraterLayer, AppendsToTheFeaturesOfEarlierLayers) {
    const CraterLayer layer(Models(), Options());
    TerrainFeatures features;
    features.craters.Append({1000.0, 1000.0, 10.0, 0.0, 99u});
    Raster<float> heights = Grid();
    layer.Apply(heights, {2.0}, features);
    EXPECT_EQ(features.craters.At(0).id, 99u);
    EXPECT_GT(features.craters.Size(), 1u);
}

TEST(CraterLayer, AppliesOnlyManualCratersWithoutGenerating) {
    CraterLayerOptions options = Options();
    options.generate = false;
    options.generation.manual.Append({0.0, 0.0, 30.0, 0.5, 0u});
    options.generation.manual.Append({5000.0, 0.0, 30.0, 0.5, 0u});
    const CraterLayer layer(Models(), options);
    const CraterList craters = layer.Craters({-64.0, -64.0, 64.0, 64.0});
    ASSERT_EQ(craters.Size(), 1u);
    EXPECT_TRUE(craters.At(0).manual);
    EXPECT_EQ(craters.At(0).id, terraingen::ManualCraterId(0.0, 0.0, 30.0));
    Raster<float> heights = Grid();
    TerrainFeatures features;
    layer.Apply(heights, {2.0}, features);
    EXPECT_LT(heights(32, 32), -1.0f);
}

TEST(CraterLayer, FadesCratersOutAsTheSpacingCoarsens) {
    CraterLayerOptions options = Options();
    options.generate = false;
    options.generation.manual.Append({0.0, 0.0, 10.0, 0.0, 0u});
    const CraterLayer layer(Models(), options);
    TerrainFeatures features;
    const auto floor = [&](double spacing) {
        Raster<float> heights = Grid();
        layer.Apply(heights, {spacing}, features);
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
    EXPECT_EQ(uncached.Cache(), nullptr);
    ASSERT_NE(cached.Cache(), nullptr);
    const terraingen::Bounds2d region{-64.0, -64.0, 64.0, 64.0};
    EXPECT_EQ(cached.Craters(region).Ids(), uncached.Craters(region).Ids());
    EXPECT_GT(cached.Cache()->Size(), 0u);
    EXPECT_EQ(cached.Craters(region).Ids(), uncached.Craters(region).Ids());
    EXPECT_EQ(cached.ApplyAt(1.0f, 3.0, -7.0, {2.0}), uncached.ApplyAt(1.0f, 3.0, -7.0, {2.0}));
}

TEST(CraterLayer, ReportsItsLargestCrater) {
    EXPECT_EQ(CraterLayer(Models(), Options()).MaxFeatureDiameter(), 40.0);
    CraterLayerOptions options = Options();
    options.generate = false;
    options.generation.manual.Append({0.0, 0.0, 75.0, 0.0, 0u});
    options.generation.manual.Append({0.0, 0.0, 12.0, 0.0, 0u});
    EXPECT_EQ(CraterLayer(Models(), options).MaxFeatureDiameter(), 75.0);
}
