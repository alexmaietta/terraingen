// SPDX-License-Identifier: BSD-3-Clause

#include "support/source_agreement.h"
#include "terraingen/sources/synthetic_source.h"

#include "terraingen/sources/diamond_square.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::GeoTransform;
using terraingen::NoiseLayer;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::SyntheticSource;
using terraingen::SyntheticTerrainParams;
using terraingen::testing::ExpectPointsTilesAndGridAgree;

namespace {

SyntheticTerrainParams Hills() {
    NoiseLayer layer;
    layer.fractal.seed = 5;
    layer.fractal.wavelength = 80.0f;
    layer.fractal.amplitude = 6.0f;
    layer.fractal.octaves = 4;
    return SyntheticTerrainParams{{layer}};
}

}  // namespace

TEST(SyntheticSource, AnEmptyStackIsFlatAtItsElevation) {
    const SyntheticSource flat;
    EXPECT_EQ(flat.HeightAt(1e6, -3.0, 1.0), 0.0f);
    const SyntheticSource raised(SyntheticTerrainParams{{}}, 12.5f);
    Raster<float> heights = RasterGrid{4, 3, GeoTransform::NorthUp(-1e5, 2e5, 7.0)}.MakeRaster();
    raised.Fill(heights);
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, 12.5f);
    }
    EXPECT_FALSE(heights.NoData().has_value());
    EXPECT_FALSE(terraingen::BoundedGrid(raised).has_value());
    EXPECT_EQ(raised.ResolutionAt(0.0, 0.0), std::numeric_limits<double>::infinity());
    EXPECT_EQ(raised.ReferencePoint().x, 0.0);
    EXPECT_TRUE(raised.Keys().Empty());
    EXPECT_EQ(raised.UpsamplerInUse(), nullptr);
}

TEST(SyntheticSource, EvaluatesTheNoiseAtEveryPointWithoutAPixelSize) {
    const SyntheticSource source(Hills(), 3.0f);
    for (const double spacing : {10.0, 0.1}) {
        EXPECT_EQ(source.HeightAt(17.3, -4.1, spacing),
                  3.0f + terraingen::NoiseLayersAt(Hills().layers, {}, {}, 17.3, -4.1));
    }
    Raster<float> filled = RasterGrid{64, 48, GeoTransform::NorthUp(-50.0, 40.0, 2.0)}.MakeRaster();
    source.Fill(filled);
    const std::vector<NoiseLayer> layers = Hills().layers;
    for (std::size_t row = 0; row < filled.Height(); ++row) {
        for (std::size_t col = 0; col < filled.Width(); ++col) {
            const terraingen::Vec2d c = filled.CellCenter(col, row);
            ASSERT_EQ(filled(col, row), 3.0f + terraingen::EvaluateNoiseLayers(
                                                   static_cast<float>(c.x), static_cast<float>(c.y),
                                                   layers.data(), layers.size()));
        }
    }
    ExpectPointsTilesAndGridAgree(source, {20, 15, GeoTransform::NorthUp(13.1, 7.7, 0.9)});
    const RasterGrid grid = source.Grid();
    EXPECT_EQ(grid.width, 0u);
    EXPECT_EQ(grid.transform, GeoTransform::NorthUp(0.0, 0.0, 1.0));
}

TEST(SyntheticSource, UpsamplesNoiseSampledAtPostsWithAPixelSize) {
    const SyntheticSource source(Hills(), 0.0f, 8.0);
    EXPECT_EQ(source.ResolutionAt(1.0, 2.0), 8.0);
    EXPECT_EQ(source.PixelSize(), 8.0);
    EXPECT_EQ(source.Grid().transform, GeoTransform::NorthUp(-4.0, 4.0, 8.0));
    EXPECT_FALSE(terraingen::BoundedGrid(source).has_value());
    const float post = terraingen::NoiseLayersAt(Hills().layers, {}, {}, 16.0, -24.0);
    EXPECT_EQ(source.HeightAt(16.0, -24.0, 8.0), post);
    EXPECT_EQ(source.HeightAt(16.0, -24.0, 1.0), post) << "cubic passes through the posts";
    const float between = source.HeightAt(19.0, -21.0, 1.0);
    EXPECT_NE(between, terraingen::NoiseLayersAt(Hills().layers, {}, {}, 19.0, -21.0))
        << "noise finer than the posts is not evaluated";
    ExpectPointsTilesAndGridAgree(source, {24, 18, GeoTransform::NorthUp(-31.0, 17.0, 1.3)});
    ExpectPointsTilesAndGridAgree(source, {20, 16, GeoTransform::NorthUp(-4.0, 4.0, 8.0)});
}

TEST(SyntheticSource, RefinesItsPostsWithDiamondSquare) {
    terraingen::DiamondSquareOptions options;
    options.seed = 2;
    const SyntheticSource source(Hills(), 0.0f, 8.0,
                                 std::make_shared<terraingen::DiamondSquareUpsampler>(options));
    const auto fitted = std::dynamic_pointer_cast<const terraingen::DiamondSquareUpsampler>(
        source.UpsamplerInUse());
    ASSERT_NE(fitted, nullptr);
    EXPECT_TRUE(fitted->Options().fractalDimension.has_value()) << "estimated from the posts";
    EXPECT_TRUE(fitted->Options().scale.has_value());
    const float post = terraingen::NoiseLayersAt(Hills().layers, {}, {}, 16.0, -24.0);
    EXPECT_EQ(source.HeightAt(16.0, -24.0, 0.5), post);
    ExpectPointsTilesAndGridAgree(source, {24, 18, GeoTransform::NorthUp(-31.0, 17.0, 0.9)});
}

TEST(SyntheticSource, EvaluatesNoiseIn3DOnABody) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const SyntheticSource source(Hills(), 0.0f, std::nullopt, nullptr, moon);
    EXPECT_EQ(source.HeightAt(10.0, 20.0, 1.0),
              terraingen::NoiseLayersAt(Hills().layers, {}, moon, 10.0, 20.0));
    EXPECT_NEAR(source.HeightAt(-180.0, 5.0, 1.0), source.HeightAt(180.0, 5.0, 1.0), 1e-3)
        << "seamless across the antimeridian";
}

TEST(SyntheticSource, RejectsInvalidSettings) {
    SyntheticTerrainParams many;
    many.layers.assign(terraingen::kMaxNoiseLayers + 1, NoiseLayer{});
    EXPECT_THROW(SyntheticSource{many}, std::invalid_argument);
    SyntheticTerrainParams bad = Hills();
    bad.layers[0].fractal.octaves = 0;
    EXPECT_THROW(SyntheticSource{bad}, std::invalid_argument);
    EXPECT_THROW(SyntheticSource(Hills(), std::numeric_limits<float>::infinity()),
                 std::invalid_argument);
    EXPECT_THROW(SyntheticSource(Hills(), 0.0f, 0.0), std::invalid_argument);
    EXPECT_THROW(SyntheticSource(Hills(), 0.0f, std::nullopt,
                                 std::make_shared<terraingen::KernelUpsampler>()),
                 std::invalid_argument)
        << "an upsampler needs posts";
}

TEST(ValidateSyntheticTerrain, AllowsAnEmptyStackAndNamesTheBadLayer) {
    EXPECT_NO_THROW(terraingen::ValidateSyntheticTerrain(SyntheticTerrainParams{}));
    SyntheticTerrainParams many;
    many.layers.resize(terraingen::kMaxNoiseLayers + 1);
    EXPECT_THROW(terraingen::ValidateSyntheticTerrain(many), std::invalid_argument);
    SyntheticTerrainParams bad = Hills();
    bad.layers.push_back(NoiseLayer{});
    bad.layers[1].fractal.gain = 0.0f;
    try {
        terraingen::ValidateSyntheticTerrain(bad);
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()).rfind("Layer 2:", 0), 0u) << error.what();
    }
}

TEST(SyntheticTerrainParams, ComparesLayers) {
    EXPECT_EQ(SyntheticTerrainParams{}, SyntheticTerrainParams{});
    EXPECT_NE(Hills(), SyntheticTerrainParams{});
}

TEST(SyntheticSource, ADefaultStackFillsZeros) {
    EXPECT_TRUE(SyntheticTerrainParams{}.layers.empty());
    Raster<float> heights = RasterGrid{5, 4, GeoTransform::NorthUp(-30.0, 20.0, 3.0)}.MakeRaster();
    SyntheticSource(SyntheticTerrainParams{}).Fill(heights);
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, 0.0f);
    }
}

TEST(SyntheticSource, TakesItsPixelSizeInMetersOnABody) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const SyntheticSource source(Hills(), 0.0f, 5.0, nullptr, moon);
    ASSERT_TRUE(source.PixelSize().has_value());
    EXPECT_DOUBLE_EQ(*source.PixelSize(), 5.0 / moon.UnitLength()) << "posts 5 m apart, in degrees";
    EXPECT_EQ(*SyntheticSource(Hills(), 0.0f, 5.0).PixelSize(), 5.0) << "meters on a plane";
}
