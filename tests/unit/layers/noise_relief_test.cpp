#include "terraingen/layers/noise_relief.h"

#include "terraingen/noise/kernels/layers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::NoiseRelief;
using terraingen::Raster;
using terraingen::SyntheticTerrainParams;
using terraingen::TerrainFeatures;

namespace {

SyntheticTerrainParams Params() {
    SyntheticTerrainParams params;
    params.layers[0].fractal.wavelength = 50.0f;
    params.layers[0].fractal.amplitude = 4.0f;
    return params;
}

}  // namespace

TEST(NoiseRelief, AddsTheSyntheticTerrainAtTheOrigin) {
    Raster<float> heights(24, 16, 10.0f);
    heights.SetTransform(GeoTransform::NorthUp(-24.0, 16.0, 2.0));
    TerrainFeatures features;
    NoiseRelief(Params()).Apply(heights, 2.0, features);
    Raster<float> expected(24, 16);
    expected.SetTransform(heights.Transform());
    terraingen::FillSyntheticTerrain(expected, Params());
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], 10.0f + expected.Values()[i]) << i;
    }
    EXPECT_TRUE(features.craters.Empty());
}

TEST(NoiseRelief, MeasuresPositionsFromItsOriginAndSkipsNoData) {
    Raster<float> heights(4, 4, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(500000.0, 4000000.0, 1.0));
    heights.SetNoData(std::numeric_limits<float>::quiet_NaN());
    heights(1, 2) = std::numeric_limits<float>::quiet_NaN();
    TerrainFeatures features;
    const SyntheticTerrainParams params = Params();
    NoiseRelief({params}, {500000.0, 4000000.0}).Apply(heights, 1.0, features);
    EXPECT_TRUE(std::isnan(heights(1, 2)));
    const auto centre = heights.CellCenter(3, 1);
    EXPECT_EQ(heights(3, 1),
              terraingen::EvaluateNoiseLayers(static_cast<float>(centre.x - 500000.0),
                                              static_cast<float>(centre.y - 4000000.0),
                                              params.layers.data(), 1));
}

TEST(NoiseRelief, RejectsInvalidParameters) {
    SyntheticTerrainParams params;
    params.layers.clear();
    EXPECT_THROW(NoiseRelief{params}, std::invalid_argument);
    EXPECT_EQ(NoiseRelief(Params()).Params(), Params());
}

TEST(NoiseRelief, PointQueriesMatchCellsAndKeepNoData) {
    Raster<float> heights(8, 8, 3.0f);
    heights.SetTransform(GeoTransform::NorthUp(-8.0, 8.0, 2.0));
    TerrainFeatures features;
    const NoiseRelief relief(Params(), {1.0, -2.0});
    relief.Apply(heights, 2.0, features);
    const auto centre = heights.CellCenter(5, 2);
    EXPECT_EQ(relief.ApplyAt(3.0f, centre.x, centre.y, 2.0), heights(5, 2));
    EXPECT_TRUE(std::isnan(relief.ApplyAt(std::numeric_limits<float>::quiet_NaN(), 0.0, 0.0, 2.0)));
}
