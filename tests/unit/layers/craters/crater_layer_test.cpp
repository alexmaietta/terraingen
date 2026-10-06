#include "terraingen/layers/craters/crater_layer.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

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
            std::make_shared<terraingen::NoDegradation>(),
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
    layer.Apply(heights, 2.0, features);

    const CraterModels models = Models();
    const CraterList craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), {-64.0, -64.0, 64.0, 64.0}, *models.density, *models.ages, 3u,
        {.dMin = 6.0, .dMax = 40.0, .marginRadii = models.profile->ReachRadii()});
    Raster<float> expected = Grid();
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation);
    ASSERT_GT(craters.Size(), 0u);
    EXPECT_EQ(features.craters.Ids(), craters.Ids());
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
    layer.Apply(heights, 2.0, features);
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
    layer.Apply(heights, 2.0, features);
    EXPECT_LT(heights(32, 32), -1.0f);
}

TEST(CraterLayer, LeavesOutCratersTooSmallForTheSpacing) {
    CraterLayerOptions options = Options();
    options.generate = false;
    options.generation.manual.Append({0.0, 0.0, 10.0, 0.0, 0u});
    const CraterLayer layer(Models(), options);
    Raster<float> fine = Grid();
    TerrainFeatures features;
    layer.Apply(fine, 2.0, features);
    EXPECT_LT(fine(32, 32), -0.5f);
    Raster<float> coarse = Grid();
    layer.Apply(coarse, 4.0, features);
    EXPECT_EQ(coarse(32, 32), 0.0f) << "a 10 m crater is under 3 samples of 4 m";
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
