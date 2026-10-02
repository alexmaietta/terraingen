#include "settings.h"

#include "terraingen/surfaces/synthetic_terrain.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using terraingen::FractalType;
using terraingen::GainFromHurst;
using terraingen::GenerateSyntheticTerrain;
using terraingen::NoiseBasis;
using terraingen::viewer::DefaultLayer;
using terraingen::viewer::GenerateTerrain;
using terraingen::viewer::LayerSettings;
using terraingen::viewer::SunDirection;
using terraingen::viewer::TerrainSettings;
using terraingen::viewer::ValidateSettings;

TEST(TerrainSettings, DefaultsAreASingleValidPerlinFbmLayer) {
    const TerrainSettings settings;
    EXPECT_EQ(ValidateSettings(settings), "");
    ASSERT_EQ(settings.layers.size(), 1u);
    EXPECT_EQ(settings.layers[0], DefaultLayer());
    EXPECT_EQ(DefaultLayer().layer.fractal.basis, NoiseBasis::Perlin);
    EXPECT_EQ(DefaultLayer().layer.fractal.type, FractalType::Fbm);
}

TEST(LayerSettings, AppliesHurstOnlyWhenEnabled) {
    LayerSettings settings;
    settings.hurst = 0.5f;
    settings.layer.fractal.lacunarity = 4.0f;
    EXPECT_EQ(settings.EffectiveLayer().fractal.gain, settings.layer.fractal.gain);
    settings.useHurst = true;
    EXPECT_FLOAT_EQ(settings.EffectiveLayer().fractal.gain, GainFromHurst(0.5f, 4.0f));
}

TEST(TerrainSettings, EffectiveTerrainKeepsLayerOrder) {
    TerrainSettings settings;
    LayerSettings second = DefaultLayer();
    second.layer.fractal.basis = NoiseBasis::WorleyF1;
    second.useHurst = true;
    settings.layers.push_back(second);
    const auto terrain = settings.EffectiveTerrain();
    ASSERT_EQ(terrain.layers.size(), 2u);
    EXPECT_EQ(terrain.layers[0], settings.layers[0].layer);
    EXPECT_EQ(terrain.layers[1], second.EffectiveLayer());
}

TEST(TerrainSettings, ReportsTheFirstProblem) {
    const auto problem = [](auto mutate) {
        TerrainSettings settings;
        mutate(settings);
        return ValidateSettings(settings);
    };
    EXPECT_NE(problem([](TerrainSettings& s) { s.size = 1; }), "");
    EXPECT_NE(problem([](TerrainSettings& s) { s.pixelSize = 0.0; }), "");
    EXPECT_NE(
        problem([](TerrainSettings& s) { s.pixelSize = std::numeric_limits<double>::infinity(); }),
        "");
    EXPECT_NE(problem([](TerrainSettings& s) {
                  s.layers[0].useHurst = true;
                  s.layers[0].hurst = std::numeric_limits<float>::quiet_NaN();
              }),
              "");
    EXPECT_NE(problem([](TerrainSettings& s) { s.layers[0].layer.fractal.octaves = 0; }), "");
    EXPECT_NE(problem([](TerrainSettings& s) { s.layers.clear(); }), "");
    EXPECT_EQ(problem([](TerrainSettings& s) {
                  s.layers[0].hurst = std::numeric_limits<float>::quiet_NaN();
              }),
              "");
}

TEST(TerrainSettings, GenerateMatchesTheCoreFunction) {
    TerrainSettings settings;
    settings.size = 33;
    settings.pixelSize = 3.0;
    settings.layers[0].useHurst = true;
    const auto generated = GenerateTerrain(settings);
    const auto expected = GenerateSyntheticTerrain(33, 33, 3.0, settings.EffectiveTerrain());
    ASSERT_EQ(generated.Size(), expected.Size());
    for (std::size_t i = 0; i < generated.Size(); ++i) {
        ASSERT_EQ(generated.Values()[i], expected.Values()[i]);
    }
}

TEST(TerrainSettings, GenerateRejectsInvalidSettings) {
    TerrainSettings settings;
    settings.size = 0;
    EXPECT_THROW(GenerateTerrain(settings), std::invalid_argument);
}

TEST(TerrainSettings, ComparesByValue) {
    TerrainSettings a;
    TerrainSettings b;
    EXPECT_EQ(a, b);
    b.layers[0].layer.fractal.seed = 99u;
    EXPECT_FALSE(a == b);
}

TEST(SunDirection, UsesCompassAzimuthAndElevation) {
    const glm::vec3 north = SunDirection(0.0f, 0.0f);
    EXPECT_NEAR(north.x, 0.0f, 1e-6f);
    EXPECT_NEAR(north.y, 1.0f, 1e-6f);
    EXPECT_NEAR(north.z, 0.0f, 1e-6f);

    const glm::vec3 east = SunDirection(90.0f, 0.0f);
    EXPECT_NEAR(east.x, 1.0f, 1e-6f);
    EXPECT_NEAR(east.y, 0.0f, 1e-6f);

    const glm::vec3 overhead = SunDirection(123.0f, 90.0f);
    EXPECT_NEAR(overhead.z, 1.0f, 1e-6f);

    EXPECT_NEAR(glm::length(SunDirection(315.0f, 35.0f)), 1.0f, 1e-6f);
}

TEST(SurfaceViewSettings, ResolvesTheSelectedModelsParameters) {
    const ernie::BrdfRegistry registry = ernie::BrdfRegistry::WithBuiltins();
    terraingen::viewer::SurfaceViewSettings material;
    EXPECT_EQ(material.surface.brdf, "lambert");
    EXPECT_TRUE(material.Effective(registry).brdfParams.empty());

    material.surface.brdf = "hapke";
    EXPECT_EQ(material.Effective(registry).brdfParams, registry.Find("hapke")->Defaults());

    material.brdfParams["hapke"] = {0.4f, 9.0f};
    material.brdfParams["oren_nayar"] = {30.0f};
    const auto hapke = material.Effective(registry).brdfParams;
    ASSERT_EQ(hapke.size(), 6u);
    EXPECT_FLOAT_EQ(hapke[0], 0.4f);
    EXPECT_FLOAT_EQ(hapke[1], 1.0f) << "clamped to the parameter range";
    EXPECT_FLOAT_EQ(hapke[4], registry.Find("hapke")->params[4].defaultValue);

    material.surface.brdf = "oren_nayar";
    EXPECT_EQ(material.Effective(registry).brdfParams, std::vector<float>{30.0f})
        << "each model keeps its own values";

    material.surface.brdf = "unregistered";
    material.surface.brdfParams = {1.0f};
    EXPECT_EQ(material.Effective(registry).brdfParams, std::vector<float>{1.0f});
}

TEST(LoadDetailTexture, LoadsColourAsSrgbAndNormalsAsLinear) {
    const std::string path = std::string(TERRAINGEN_TEST_FIXTURES) + "/images/grey16.png";
    const auto albedo = terraingen::viewer::LoadDetailTexture(path, false);
    const auto normal = terraingen::viewer::LoadDetailTexture(path, true);
    ASSERT_TRUE(std::holds_alternative<ernie::Image>(albedo));
    ASSERT_TRUE(std::holds_alternative<ernie::Image>(normal));
    const float stored = 32768.0f / 65535.0f;
    EXPECT_NEAR(std::get<ernie::Image>(normal).At(0, 1).r, stored, 1e-6f);
    EXPECT_NEAR(std::get<ernie::Image>(albedo).At(0, 1).r, ernie::SrgbToLinear(stored), 1e-6f);

    const auto missing = terraingen::viewer::LoadDetailTexture("/no/such/file.png", false);
    ASSERT_TRUE(std::holds_alternative<std::string>(missing));
    EXPECT_NE(std::get<std::string>(missing).find("/no/such/file.png"), std::string::npos);
    EXPECT_EQ(std::get<std::string>(terraingen::viewer::LoadDetailTexture("", true)),
              "No file given");
}

TEST(ViewOf, SharesTheRastersCellsAndGeometry) {
    terraingen::Raster<float> raster(3, 2);
    raster.SetTransform(terraingen::GeoTransform::NorthUp(100.0, 50.0, 2.5));
    raster.SetNoData(-1.0f);
    raster(2, 1) = 7.0f;
    const ernie::RasterView view = terraingen::viewer::ViewOf(raster);
    EXPECT_EQ(view.data, raster.Values().data());
    EXPECT_EQ(view.width, 3u);
    EXPECT_EQ(view.height, 2u);
    EXPECT_EQ(view.Stride(), 3u);
    EXPECT_DOUBLE_EQ(view.originX, 100.0);
    EXPECT_DOUBLE_EQ(view.originY, 50.0);
    EXPECT_DOUBLE_EQ(view.cellSize, 2.5);
    EXPECT_EQ(view.noData, -1.0f);
    EXPECT_FLOAT_EQ(view(2, 1), 7.0f);
    EXPECT_FALSE(view.IsValid(-1.0f));
    EXPECT_TRUE(view.IsValid(7.0f));
}

TEST(ViewOf, RejectsRotatedOrRectangularPixels) {
    terraingen::Raster<float> raster(4, 4);
    raster.SetTransform(terraingen::GeoTransform::FromGdal({0.0, 1.0, 0.2, 0.0, 0.0, -1.0}));
    EXPECT_THROW(terraingen::viewer::ViewOf(raster), std::invalid_argument);
    raster.SetTransform(terraingen::GeoTransform::FromGdal({0.0, 1.0, 0.0, 0.0, 0.0, -2.0}));
    EXPECT_THROW(terraingen::viewer::ViewOf(raster), std::invalid_argument);
    raster.SetTransform(terraingen::GeoTransform::FromGdal({0.0, 1.0, 0.0, 0.0, 0.0, 1.0}));
    EXPECT_THROW(terraingen::viewer::ViewOf(raster), std::invalid_argument) << "south-up";
}
