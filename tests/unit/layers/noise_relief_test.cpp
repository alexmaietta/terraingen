#include "terraingen/layers/noise_relief.h"

#include "terraingen/noise/noise_layers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::GeoTransform;
using terraingen::NoiseRelief;
using terraingen::Raster;
using terraingen::SyntheticTerrainParams;

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
    NoiseRelief(Params()).Apply(heights, {2.0});
    Raster<float> expected(24, 16);
    expected.SetTransform(heights.Transform());
    terraingen::FillSyntheticTerrain(expected, Params());
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], 10.0f + expected.Values()[i]) << i;
    }
}

TEST(NoiseRelief, MeasuresPositionsFromItsOriginAndSkipsNoData) {
    Raster<float> heights(4, 4, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(500000.0, 4000000.0, 1.0));
    heights.SetNoData(std::numeric_limits<float>::quiet_NaN());
    heights(1, 2) = std::numeric_limits<float>::quiet_NaN();
    const SyntheticTerrainParams params = Params();
    NoiseRelief({params}, {500000.0, 4000000.0}).Apply(heights, {1.0});
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
    const NoiseRelief relief(Params(), {1.0, -2.0});
    relief.Apply(heights, {2.0});
    const auto centre = heights.CellCenter(5, 2);
    EXPECT_EQ(relief.ApplyAt(3.0f, centre.x, centre.y, {2.0}), heights(5, 2));
    EXPECT_TRUE(
        std::isnan(relief.ApplyAt(std::numeric_limits<float>::quiet_NaN(), 0.0, 0.0, {2.0})));
}

TEST(NoiseLayersAt, EvaluatesThePlaneFromItsOriginAndTheSphereIn3D) {
    terraingen::NoiseLayer layer;
    layer.fractal.wavelength = 30.0f;
    layer.fractal.amplitude = 2.0f;
    const std::vector<terraingen::NoiseLayer> layers{layer};
    EXPECT_EQ(terraingen::NoiseLayersAt(layers, {100.0, 50.0}, terraingen::Surface(), 112.0, 47.0),
              terraingen::EvaluateNoiseLayers(12.0f, -3.0f, layers.data(), layers.size()));
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const terraingen::Vec3d p = moon.Position({10.0, 20.0});
    EXPECT_EQ(terraingen::NoiseLayersAt(layers, {}, moon, 10.0, 20.0),
              terraingen::EvaluateNoiseLayers3(p.x, p.y, p.z, layers.data(), layers.size()));
}
