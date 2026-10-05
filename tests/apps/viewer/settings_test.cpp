#include "settings.h"

#include "terraingen/craters.h"
#include "terraingen/spatial/cells.h"
#include "terraingen/surfaces/synthetic_terrain.h"

#include "ernie/ernie.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using terraingen::FractalType;
using terraingen::GainFromHurst;
using terraingen::GenerateSyntheticTerrain;
using terraingen::NoiseBasis;
using terraingen::viewer::BuildCraterModels;
using terraingen::viewer::CraterDensityModel;
using terraingen::viewer::CraterModels;
using terraingen::viewer::CraterProfileModel;
using terraingen::viewer::CraterSettings;
using terraingen::viewer::DefaultLayer;
using terraingen::viewer::ExpectedCraterCount;
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
    EXPECT_NE(problem([](TerrainSettings& s) { s.width = 1; }), "");
    EXPECT_NE(problem([](TerrainSettings& s) { s.height = 1; }), "");
    EXPECT_NE(problem([](TerrainSettings& s) { s.width = terraingen::viewer::kMaxGridSize + 1; }),
              "");
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
    settings.craters.enabled = false;
    settings.width = 33;
    settings.height = 21;
    settings.pixelSize = 3.0;
    settings.layers[0].useHurst = true;
    const auto generated = GenerateTerrain(settings);
    const auto expected = GenerateSyntheticTerrain(33, 21, 3.0, settings.EffectiveTerrain());
    ASSERT_EQ(generated.Size(), expected.Size());
    for (std::size_t i = 0; i < generated.Size(); ++i) {
        ASSERT_EQ(generated.Values()[i], expected.Values()[i]);
    }
}

TEST(TerrainSettings, GenerateRejectsInvalidSettings) {
    TerrainSettings settings;
    settings.width = 0;
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

TEST(TerrainBounds, SpansCellCentresAndTheExaggeratedHeights) {
    terraingen::Raster<float> raster(3, 2);
    raster.SetTransform(terraingen::GeoTransform::NorthUp(100.0, 50.0, 2.5));
    terraingen::RasterStats stats;
    stats.min = -4.0;
    stats.max = 6.0;
    stats.validCount = 6;
    const auto bounds = terraingen::viewer::TerrainBounds(raster, stats, 2.0f);
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->first, glm::vec3(101.25f, 46.25f, -8.0f));
    EXPECT_EQ(bounds->second, glm::vec3(106.25f, 48.75f, 12.0f));

    stats.validCount = 0;
    EXPECT_FALSE(terraingen::viewer::TerrainBounds(raster, stats, 1.0f).has_value());
}

TEST(AuthorTerrain, WritesHeightsAndGridWithoutCopying) {
    auto raster = std::make_shared<terraingen::Raster<float>>(3, 2);
    raster->SetTransform(terraingen::GeoTransform::NorthUp(100.0, 50.0, 2.5));
    raster->SetNoData(-1.0f);
    (*raster)(2, 1) = 7.0f;
    ernie::Scene scene;
    terraingen::viewer::AuthorTerrain(scene, raster);
    const ernie::Tensor* heights = scene.Read(terraingen::viewer::kTerrainPrim, "heights");
    ASSERT_NE(heights, nullptr);
    EXPECT_EQ(heights->Data(), raster->Values().data());
    EXPECT_EQ(heights->Shape(), (std::vector<std::int64_t>{2, 3}));
    EXPECT_EQ(heights->As<float>()[5], 7.0f);
    EXPECT_EQ(scene.Get<glm::dvec2>(terraingen::viewer::kTerrainPrim, ernie::tokens::kOrigin, {}),
              glm::dvec2(100.0, 50.0));
    EXPECT_EQ(scene.Get<double>(terraingen::viewer::kTerrainPrim, ernie::tokens::kCellSize, 0.0),
              2.5);
    EXPECT_EQ(scene.Get<float>(terraingen::viewer::kTerrainPrim, ernie::tokens::kNoData, 0.0f),
              -1.0f);

    auto noNoData = std::make_shared<terraingen::Raster<float>>(2, 2);
    noNoData->SetTransform(terraingen::GeoTransform::NorthUp(0.0, 0.0, 1.0));
    terraingen::viewer::AuthorTerrain(scene, noNoData);
    EXPECT_EQ(scene.Get(terraingen::viewer::kTerrainPrim, ernie::tokens::kNoData), nullptr);

    auto rotated = std::make_shared<terraingen::Raster<float>>(2, 2);
    rotated->SetTransform(terraingen::GeoTransform::FromGdal({0.0, 1.0, 0.2, 0.0, 0.0, -1.0}));
    EXPECT_THROW(terraingen::viewer::AuthorTerrain(scene, rotated), std::invalid_argument);
}

TEST(TensorOf, SharesTheImagesPixels) {
    auto image = std::make_shared<ernie::Image>(3, 2, glm::vec4(0.5f));
    const ernie::Tensor tensor = terraingen::viewer::TensorOf(image);
    EXPECT_EQ(tensor.Shape(), (std::vector<std::int64_t>{2, 3, 4}));
    EXPECT_EQ(tensor.Data(), static_cast<const void*>(image->pixels.data()));
    EXPECT_EQ(tensor.As<float>()[23], 0.5f);
}

TEST(AuthorView, RoundTripsTheViewThroughErniesDrawParameters) {
    const ernie::BrdfRegistry brdfs = ernie::BrdfRegistry::WithBuiltins();
    terraingen::viewer::ViewSettings view;
    view.sunAzimuthDegrees = 90.0f;
    view.sunElevationDegrees = 30.0f;
    view.ambient = 0.25f;
    view.exaggeration = 2.0f;
    view.colorMode = ernie::TerrainColorMode::Hillshade;
    view.wireframe = true;
    view.shadows.enabled = false;
    view.shadows.cascades = 2;
    view.shadows.resolution = 1024;
    view.lod.detail = 6.0f;
    view.lod.showLevels = true;
    view.material.surface.brdf = "hapke";
    view.material.exposure = 1.5f;
    view.material.surface.albedo = glm::vec3(0.2f, 0.3f, 0.4f);
    view.material.surface.detail.tileSize = 9.0f;
    view.material.brdfParams["hapke"] = {0.4f};
    ernie::OrbitController camera;
    camera.yaw = 0.3f;

    ernie::Scene scene;
    terraingen::viewer::AuthorView(scene, view, camera, brdfs);
    const ernie::RenderPrims prims =
        ernie::ResolveRenderPrims(scene, terraingen::viewer::kSettingsPrim);
    EXPECT_EQ(prims.camera, ernie::Path(terraingen::viewer::kCameraPrim));
    EXPECT_EQ(prims.terrain, ernie::Path(terraingen::viewer::kTerrainPrim));
    EXPECT_EQ(prims.material, ernie::Path(terraingen::viewer::kSurfacePrim));
    EXPECT_EQ(prims.light, ernie::Path(terraingen::viewer::kSunPrim));

    const ernie::FrameParams frame = ernie::FrameParamsOf(scene, prims, 48, 32);
    const glm::vec3 sun = SunDirection(90.0f, 30.0f);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(frame.sunDirection[i], sun[i], 1e-6f);
        EXPECT_NEAR(frame.View().eye[i], camera.Eye()[i], 1e-3f);
    }
    EXPECT_NEAR(frame.View().verticalFov, camera.verticalFov, 1e-6f);
    EXPECT_EQ(frame.ambient, 0.25f);
    EXPECT_TRUE(frame.wireframe);
    EXPECT_FALSE(frame.shadows.enabled);
    EXPECT_EQ(frame.shadows.cascades, 2);
    EXPECT_EQ(frame.shadows.resolution, 1024u);
    EXPECT_EQ(frame.camera.width, 48u);
    ASSERT_EQ(frame.sunIrradiance.size(), 16u);
    EXPECT_GT(frame.sunIrradiance[0], 0.0f) << "the exposure scales the sun's irradiance";

    const ernie::TerrainParams terrain = ernie::TerrainParamsOf(scene, prims);
    EXPECT_EQ(terrain.exaggeration, 2.0f);
    EXPECT_EQ(terrain.colorMode, ernie::TerrainColorMode::Hillshade);
    EXPECT_EQ(terrain.lod.detail, 6.0f);
    EXPECT_TRUE(terrain.lod.showLevels);

    const ernie::MaterialSettings material =
        ernie::MaterialSettingsOf(scene, prims.material, brdfs);
    EXPECT_EQ(material.brdf, "hapke");
    EXPECT_EQ(material.brdfParams, view.material.Effective(brdfs).brdfParams);
    EXPECT_EQ(material.albedo, glm::vec3(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(material.detail.tileSize, 9.0f);

    view.colorMode = ernie::TerrainColorMode::Colormap;
    terraingen::viewer::AuthorView(scene, view, camera, brdfs);
    EXPECT_EQ(ernie::TerrainParamsOf(scene, prims).colorMode, ernie::TerrainColorMode::Colormap);
    view.colorMode = ernie::TerrainColorMode::ShadedColormap;
    terraingen::viewer::AuthorView(scene, view, camera, brdfs);
    EXPECT_EQ(ernie::TerrainParamsOf(scene, prims).colorMode,
              ernie::TerrainColorMode::ShadedColormap);
}

TEST(AuthorView, WritesTheCameraExposureBandsAndPhysicalSun) {
    const ernie::BrdfRegistry brdfs = ernie::BrdfRegistry::WithBuiltins();
    terraingen::viewer::ViewSettings view;
    EXPECT_EQ(ernie::Camera{}.LinearExposureScale(), 1.0) << "defaults are neutral";
    view.camera = {1.0f, 0.01f, 200.0f, 16.0f, 8, true};
    ernie::Scene scene;
    terraingen::viewer::AuthorView(scene, view, ernie::OrbitController{}, brdfs);
    const ernie::RenderPrims prims =
        ernie::ResolveRenderPrims(scene, terraingen::viewer::kSettingsPrim);
    const ernie::FrameParams frame = ernie::FrameParamsOf(scene, prims, 16, 16);
    EXPECT_EQ(frame.camera.bands.count, 8);
    EXPECT_NEAR(frame.camera.LinearExposureScale(), 0.01 * 2.0 * 2.0 / 256.0, 1e-9);
    const ernie::SpectralBands bands = frame.camera.bands;
    EXPECT_EQ(frame.sunIrradiance, ernie::BlackbodyIrradiance(bands, 5772.0, 1361.0));

    view.camera.physicalSun = false;
    terraingen::viewer::AuthorView(scene, view, ernie::OrbitController{}, brdfs);
    EXPECT_NE(ernie::FrameParamsOf(scene, prims, 16, 16).sunIrradiance,
              ernie::BlackbodyIrradiance(bands, 5772.0, 1361.0))
        << "back to relative units";
}

TEST(CraterSettings, DefaultsAreAValidLunarSurface) {
    const TerrainSettings settings;
    EXPECT_EQ(settings.craters.apply, terraingen::CraterApplyOptions{});
    EXPECT_EQ(settings.craters.apply.datum, terraingen::CraterDatum::None);
    EXPECT_EQ(settings.craters.rims.sigmaRadii, 0.0);
    EXPECT_TRUE(settings.craters.enabled);
    EXPECT_EQ(settings.craters.density, CraterDensityModel::LunarSurface);
    EXPECT_EQ(ValidateSettings(settings), "");
    const double area = static_cast<double>(settings.width * settings.height) * settings.pixelSize *
                        settings.pixelSize;
    const CraterModels models = BuildCraterModels(settings.craters);
    EXPECT_NEAR(ExpectedCraterCount(settings), models.density->ExpectedCount(area, 4.0, 250.0),
                1e-9);
    EXPECT_GT(ExpectedCraterCount(settings), 100.0);
    TerrainSettings disabled;
    disabled.craters.enabled = false;
    EXPECT_EQ(ExpectedCraterCount(disabled), 0.0);
}

TEST(CraterSettings, ReportsCraterProblems) {
    const auto problem = [](auto mutate) {
        TerrainSettings settings;
        mutate(settings.craters);
        return ValidateSettings(settings);
    };
    EXPECT_NE(problem([](CraterSettings& c) { c.minDiameter = 0.0; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.apply.minDiameterPixels = -1.0; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.apply.interiorReplacement = 2.0; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.spacing.diameterRatio = 0.5; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.spacing.separation = -0.1; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.rims.strength = 2.0; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.maxDiameter = 3.0; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.minDiameter = 2.0; }), "");
    EXPECT_NE(problem([](CraterSettings& c) { c.surfaceAgeGyr = 5.0; }), "");
    EXPECT_NE(problem([](CraterSettings& c) {
                  c.density = CraterDensityModel::PowerLaw;
                  c.powerLawExponent = 0.0;
              }),
              "");
    EXPECT_NE(problem([](CraterSettings& c) {
                  c.density = CraterDensityModel::Trask;
                  c.lifetimeGyr = 0.0;
              }),
              "");
    const std::string tooMany = problem([](CraterSettings& c) {
        c.density = CraterDensityModel::Trask;
        c.minDiameter = 0.1;
    });
    EXPECT_NE(tooMany.find("raise the minimum diameter"), std::string::npos) << tooMany;
    EXPECT_EQ(problem([](CraterSettings& c) {
                  c.enabled = false;
                  c.minDiameter = -1.0;
              }),
              "");
}

TEST(CraterSettings, BuildsTheSelectedModels) {
    CraterSettings settings;
    CraterModels lunar = BuildCraterModels(settings);
    EXPECT_NE(dynamic_cast<const terraingen::SteadyStateAges*>(lunar.ages.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::FreshnessDegradation*>(lunar.degradation.get()),
              nullptr);
    EXPECT_EQ(lunar.profile->ReachRadii(), 1.5);

    settings.density = CraterDensityModel::Trask;
    settings.degrade = false;
    settings.profile = CraterProfileModel::MartinParkesDunstan;
    const CraterModels trask = BuildCraterModels(settings);
    EXPECT_DOUBLE_EQ(trask.density->Cumulative(10.0),
                     terraingen::TraskEquilibrium().Cumulative(10.0));
    EXPECT_NE(dynamic_cast<const terraingen::UniformAges*>(trask.ages.get()), nullptr);
    EXPECT_NE(dynamic_cast<const terraingen::NoDegradation*>(trask.degradation.get()), nullptr);
    EXPECT_EQ(trask.profile->ReachRadii(), 2.0);

    settings.density = CraterDensityModel::Hartmann;
    settings.profile = CraterProfileModel::FassettThomsonPlateau;
    EXPECT_DOUBLE_EQ(BuildCraterModels(settings).density->Cumulative(10.0),
                     terraingen::HartmannEquilibrium().Cumulative(10.0));

    settings.density = CraterDensityModel::PowerLaw;
    settings.powerLawCoefficient = 0.2;
    settings.powerLawExponent = 2.5;
    EXPECT_DOUBLE_EQ(BuildCraterModels(settings).density->Cumulative(2.0),
                     0.2 * std::pow(2.0, -2.5));
}

TEST(CraterSettings, GenerateAppliesCratersOnTheNoiseTerrain) {
    TerrainSettings settings;
    settings.width = 129;
    settings.height = 97;
    settings.craters.minDiameter = 8.0;
    settings.craters.maxDiameter = 100.0;
    const auto generated = GenerateTerrain(settings);

    auto expected = GenerateSyntheticTerrain(129, 97, 2.0, settings.EffectiveTerrain());
    const CraterModels models = BuildCraterModels(settings.craters);
    const auto craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), {-129.0, -97.0, 129.0, 97.0}, *models.density, *models.ages,
        settings.craters.seed, {.dMin = 8.0, .dMax = 100.0, .marginRadii = 1.5});
    ASSERT_FALSE(craters.Empty());
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation);
    ASSERT_EQ(generated.Size(), expected.Size());
    for (std::size_t i = 0; i < generated.Size(); ++i) {
        ASSERT_EQ(generated.Values()[i], expected.Values()[i]) << i;
    }

    settings.craters.enabled = false;
    const auto plain = GenerateTerrain(settings);
    EXPECT_FALSE(
        std::equal(plain.Values().begin(), plain.Values().end(), generated.Values().begin()));
}

TEST(CraterSettings, SpacingAndMinimumPixelsReachTheLibrary) {
    TerrainSettings settings;
    settings.width = 129;
    settings.height = 97;
    settings.craters.minDiameter = 8.0;
    settings.craters.maxDiameter = 100.0;
    settings.craters.spacing.separation = 1.0;
    settings.craters.apply = {.minDiameterPixels = 6.0, .datum = terraingen::CraterDatum::Plane};
    settings.craters.rims = {.sigmaRadii = 0.1, .strength = 0.5};
    const auto generated = GenerateTerrain(settings);

    auto expected = GenerateSyntheticTerrain(129, 97, 2.0, settings.EffectiveTerrain());
    const CraterModels models = BuildCraterModels(settings.craters);
    const auto craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), {-129.0, -97.0, 129.0, 97.0}, *models.density, *models.ages,
        settings.craters.seed,
        {.dMin = 8.0, .dMax = 100.0, .marginRadii = 1.5, .spacing = {1.5, 1.0}});
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation,
                             {.minDiameterPixels = 6.0, .datum = terraingen::CraterDatum::Plane});
    terraingen::SmoothCraterRims(expected, craters, {.sigmaRadii = 0.1, .strength = 0.5});
    for (std::size_t i = 0; i < generated.Size(); ++i) {
        ASSERT_EQ(generated.Values()[i], expected.Values()[i]) << i;
    }
}

TEST(CraterSettings, ManualCratersAreValidatedAndCounted) {
    TerrainSettings settings;
    settings.craters.manual.push_back({0.0, 0.0, 40.0, 0.1, 0u, true});
    EXPECT_EQ(ValidateSettings(settings), "");
    TerrainSettings manualOnly = settings;
    manualOnly.craters.generate = false;
    manualOnly.craters.minDiameter = -5.0;
    EXPECT_EQ(ValidateSettings(manualOnly), "");
    EXPECT_EQ(ExpectedCraterCount(manualOnly), 1.0);
    TerrainSettings generated = settings;
    generated.craters.manual.clear();
    EXPECT_NEAR(ExpectedCraterCount(settings), ExpectedCraterCount(generated) + 1.0, 1e-9);
    settings.craters.manual.push_back({0.0, 0.0, 0.0, 0.0, 0u, true});
    EXPECT_NE(ValidateSettings(settings).find("manual crater 2"), std::string::npos);
}

TEST(CraterSettings, ManualOnlyAppliesJustTheManualCraters) {
    TerrainSettings settings;
    settings.width = 101;
    settings.height = 101;
    settings.layers[0].layer.fractal.amplitude = 0.0f;
    settings.craters.generate = false;
    settings.craters.manual = {{0.0, 0.0, 60.0, 0.0, 0u, true}, {5000.0, 0.0, 60.0, 0.0, 0u, true}};
    const auto raster = GenerateTerrain(settings);

    auto expected = GenerateSyntheticTerrain(101, 101, 2.0, settings.EffectiveTerrain());
    const CraterModels models = BuildCraterModels(settings.craters);
    terraingen::CraterList craters;
    craters.Append({0.0, 0.0, 60.0, 0.0, terraingen::ManualCraterId(0.0, 0.0, 60.0), true});
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation);
    for (std::size_t i = 0; i < raster.Size(); ++i) {
        ASSERT_EQ(raster.Values()[i], expected.Values()[i]) << i;
    }
    EXPECT_LT(raster(50, 50), -1.0f);
}

TEST(CraterSettings, ManualCratersMergeWithGeneratedCraters) {
    TerrainSettings settings;
    settings.width = 129;
    settings.height = 97;
    settings.craters.minDiameter = 8.0;
    settings.craters.maxDiameter = 100.0;
    settings.craters.manual = {{10.0, -5.0, 70.0, 0.0, 0u, true}};
    const auto generated = GenerateTerrain(settings);

    auto expected = GenerateSyntheticTerrain(129, 97, 2.0, settings.EffectiveTerrain());
    const CraterModels models = BuildCraterModels(settings.craters);
    terraingen::CraterList manual;
    manual.Append({10.0, -5.0, 70.0, 0.0, terraingen::ManualCraterId(10.0, -5.0, 70.0), true});
    const auto craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), {-129.0, -97.0, 129.0, 97.0}, *models.density, *models.ages,
        settings.craters.seed, {.dMin = 8.0, .dMax = 100.0, .marginRadii = 1.5, .manual = manual});
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation);
    for (std::size_t i = 0; i < generated.Size(); ++i) {
        ASSERT_EQ(generated.Values()[i], expected.Values()[i]) << i;
    }
}

TEST(ViewSettings, DefaultsToAGrayShadedTerrainOnBlack) {
    const terraingen::viewer::ViewSettings view;
    EXPECT_EQ(view.colorMode, ernie::TerrainColorMode::Hillshade);
    EXPECT_EQ(view.background, (std::array<float, 3>{0.0f, 0.0f, 0.0f}));
    EXPECT_EQ(view.material.surface.albedo, glm::vec3(0.5f));
}

TEST(CraterSettings, MaxCratersReachesTheLibraryAndFitsTheMinimumDiameter) {
    TerrainSettings settings;
    settings.width = 129;
    settings.height = 97;
    settings.craters.minDiameter = 8.0;
    settings.craters.maxDiameter = 100.0;
    settings.craters.maxCraters = 20;
    const auto generated = GenerateTerrain(settings);

    auto expected = GenerateSyntheticTerrain(129, 97, 2.0, settings.EffectiveTerrain());
    const CraterModels models = BuildCraterModels(settings.craters);
    const auto craters = terraingen::GenerateCraters(
        terraingen::PlanarCells(), {-129.0, -97.0, 129.0, 97.0}, *models.density, *models.ages,
        settings.craters.seed, {.dMin = 8.0, .dMax = 100.0, .marginRadii = 1.5, .maxCraters = 20});
    EXPECT_EQ(craters.Size(), 20u);
    terraingen::ApplyCraters(expected, craters, *models.profile, *models.degradation);
    for (std::size_t i = 0; i < generated.Size(); ++i) {
        ASSERT_EQ(generated.Values()[i], expected.Values()[i]) << i;
    }

    const double fitted = terraingen::viewer::MinDiameterForCount(settings);
    settings.craters.minDiameter = fitted;
    EXPECT_NEAR(ExpectedCraterCount(settings), 20.0, 1e-3);
}
