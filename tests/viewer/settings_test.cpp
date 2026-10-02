#include "settings.h"

#include "terraingen/models/synthetic_terrain.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>

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
