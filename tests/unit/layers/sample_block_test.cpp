#include "terraingen/layers/sample_block.h"

#include "terraingen/distribution/power_law.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/layers/features.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/raster/geo_transform.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

using terraingen::Raster;
using terraingen::SampleBlock;
using terraingen::Vec2d;

TEST(SampleBlock, ListsAGridsCellCentresRowByRow) {
    Raster<float> heights(3, 2, 1.0f);
    heights.SetTransform(terraingen::GeoTransform::NorthUp(10.0, 20.0, 2.0));
    const SampleBlock samples(heights);
    EXPECT_TRUE(samples.IsGrid());
    EXPECT_EQ(samples.Grid(), &heights);
    ASSERT_EQ(samples.Size(), 6u);
    EXPECT_EQ(samples.Position(0), (Vec2d{11.0, 19.0}));
    EXPECT_EQ(samples.Position(4), (Vec2d{13.0, 17.0}));
    samples.Height(4) = 5.0f;
    EXPECT_EQ(heights(1, 1), 5.0f);
    const auto bounds = samples.Bounds();
    EXPECT_EQ(bounds.minX, 10.0);
    EXPECT_EQ(bounds.maxY, 20.0);
    heights.SetNoData(1.0f);
    EXPECT_TRUE(samples.IsNoData(0));
    EXPECT_FALSE(samples.IsNoData(4));
}

TEST(SampleBlock, HoldsScatteredPointsAndTheirBounds) {
    const std::vector<Vec2d> positions{{1.0, 5.0}, {-2.0, 3.0}, {4.0, -1.0}};
    std::vector<float> heights{1.0f, std::nanf(""), 3.0f};
    const SampleBlock samples(positions, heights);
    EXPECT_FALSE(samples.IsGrid());
    EXPECT_EQ(samples.Grid(), nullptr);
    EXPECT_EQ(samples.Position(1), (Vec2d{-2.0, 3.0}));
    EXPECT_TRUE(samples.IsNoData(1));
    const auto bounds = samples.Bounds();
    EXPECT_EQ(bounds.minX, -2.0);
    EXPECT_EQ(bounds.minY, -1.0);
    EXPECT_GT(bounds.maxX, 4.0);
    EXPECT_GT(bounds.maxY, 5.0);
    EXPECT_TRUE(bounds.Contains(4.0, 5.0)) << "the box holds its largest coordinates";
    EXPECT_FALSE(bounds.Contains(4.0001, 5.0));
    std::vector<float> short_heights(2);
    EXPECT_THROW(SampleBlock(positions, short_heights), std::invalid_argument);
}

TEST(SampleBlock, AppliesALayerToManyPointsAsToEachAlone) {
    terraingen::CraterLayerOptions options;
    options.seed = 9;
    options.generation.dMin = 4.0;
    options.generation.dMax = 120.0;
    const auto layer = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::UniformAges>(1.0),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
    const std::vector<Vec2d> positions{{0.0, 0.0}, {37.5, -12.0}, {-80.0, 64.0}, {150.0, 3.0}};
    std::vector<float> heights(positions.size(), 0.0f);
    SampleBlock samples(positions, heights);
    terraingen::TerrainFeatures features;
    layer->Apply(samples, {2.0}, features);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        EXPECT_EQ(heights[i], layer->ApplyAt(0.0f, positions[i].x, positions[i].y, {2.0}));
    }
    EXPECT_THROW(layer->Apply(samples, {}, features), std::invalid_argument)
        << "points need a sample spacing";
}

TEST(FunctionLayer, PassesTheSpacingInMetresOnASphere) {
    const auto moon = terraingen::Surface::Sphere(1737400.0);
    double seen = 0.0;
    const terraingen::FunctionLayer layer(
        [&seen](double, double, double spacing, double height) {
            seen = spacing;
            return height;
        },
        moon);
    Raster<float> heights(1, 1, 0.0f);
    heights.SetTransform(terraingen::GeoTransform::NorthUp(0.0, 0.001, 0.001));
    terraingen::TerrainFeatures features;
    layer.Apply(heights, {}, features);
    EXPECT_NEAR(seen, 0.001 * moon.UnitLength(), 1e-9);
}
