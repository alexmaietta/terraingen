/// @file
/// @brief Tests of validating raster sources, chains, raster albedo, spheres and body meshes, and
/// of `DescribeSource` and `BuildSurface` (pipeline/validate.cpp).
#include "configs.h"
#include "support/temp_path.h"
#include "terraingen/config/generate.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

using terraingen::ConfigError;
using terraingen::CraterLayerSpec;
using terraingen::TerrainConfig;
using terraingen::ValidateConfig;
using terraingen::testing::Flat;
using terraingen::testing::Noise;
using terraingen::testing::TempPath;
using terraingen::testing::ValidationErrorPath;

TEST(ValidateConfig, ReportsRasterSourceProblems) {
    using terraingen::testing::RasterConfig;
    using terraingen::testing::RasterSpec;
    using terraingen::testing::WriteDem;
    const auto folder = TempPath("terraingen_raster_errors");
    WriteDem(folder, "dem.tif");
    WriteDem(folder, "degrees.tif", terraingen::testing::Keys(true));
    const auto error = [&](auto mutate) {
        TerrainConfig config = RasterConfig("dem.tif");
        mutate(config);
        try {
            ValidateConfig(config, folder);
        } catch (const ConfigError& e) {
            return std::string(e.Path()) + ": " + e.what();
        }
        return std::string("valid");
    };
    EXPECT_EQ(error([](TerrainConfig&) {}), "valid");
    EXPECT_EQ(error([](TerrainConfig& c) { RasterSpec(c).path.clear(); }).rfind("source.path", 0),
              0u);
    EXPECT_NE(
        error([](TerrainConfig& c) { RasterSpec(c).path = "missing.tif"; }).find("cannot open"),
        std::string::npos);
    EXPECT_NE(error([](TerrainConfig& c) { RasterSpec(c).band = 1; }).find("source.band"),
              std::string::npos);
    EXPECT_NE(
        error([](TerrainConfig& c) { RasterSpec(c).path = "degrees.tif"; }).find("geographic"),
        std::string::npos);
    EXPECT_NE(error([](TerrainConfig& c) {
                  c.surface = terraingen::SphereSurfaceSpec{1737400.0};
              }).find("source.path: on a sphere the raster must be in longitude and latitude"),
              std::string::npos);
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).pixelSize = -1.0;
              }).rfind("source.pixel_size", 0),
              0u);
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).pixelSize = 1e-6;
              }).rfind("source.pixel_size", 0),
              0u);
    EXPECT_EQ(error([](TerrainConfig& c) { RasterSpec(c).pixelSize = 1.0; }), "valid");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::NoiseLayerSpec noise;
                  noise.stack[0].layer.fractal.octaves = 0;
                  c.layers = {noise};
              }).rfind("layers[0].stack[0]", 0),
              0u);
    EXPECT_EQ(error([](TerrainConfig& c) {
                  CraterLayerSpec craters;
                  craters.minDiameter = 0.0;
                  c.layers = {craters};
              }).rfind("layers[0].min_diameter", 0),
              0u);
    terraingen::Raster<float> rotated(8, 8, 1.0f);
    rotated.SetTransform(terraingen::GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0});
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, terraingen::testing::Keys());
    EXPECT_EQ(error([](TerrainConfig& c) {
                  RasterSpec(c).path = "rotated.tif";
                  RasterSpec(c).pixelSize = 0.5;
              }).rfind("source.pixel_size", 0),
              0u);
    terraingen::WriteGeoTiff(folder / "tiny.tif", terraingen::Raster<float>(1, 1, 0.0f),
                             terraingen::testing::Keys());
    EXPECT_EQ(
        error([](TerrainConfig& c) { RasterSpec(c).path = "tiny.tif"; }).rfind("source.path", 0),
        0u);
}

TEST(ValidateConfig, ReportsChainProblemsByPath) {
    const auto folder = TempPath("terraingen_chain_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    const auto raster = [](const std::string& path) {
        return terraingen::RasterSourceSpec{path, 0, std::nullopt,
                                            terraingen::ResampleMethod::Cubic};
    };
    const auto error = [&](terraingen::ChainSourceSpec chain) {
        TerrainConfig config;
        config.source = std::move(chain);
        return ValidationErrorPath(config, folder);
    };
    EXPECT_EQ(error({{raster("dem.tif"), terraingen::FlatSourceSpec{2, 2, 1.0}}}), "valid");
    EXPECT_EQ(error({}), "source.sources");
    EXPECT_EQ(error({{raster("dem.tif"), raster("missing.tif")}}), "source.sources[1].path");
    EXPECT_EQ(error({{terraingen::ChainSourceSpec{{terraingen::FlatSourceSpec{1, 1, 1.0}}}}}),
              "source.sources[0].sources[0].width");
}

TEST(ValidateConfig, ReportsRasterAlbedoProblemsByPath) {
    const auto folder = TempPath("terraingen_albedo_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    terraingen::Raster<float> map(10, 8, 1000.0f);
    map.SetTransform(terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 20.0));
    terraingen::WriteGeoTiff(folder / "map.tif", map, terraingen::testing::Keys());
    const auto rejected = [&](terraingen::RasterAlbedoSpec spec) {
        TerrainConfig config = terraingen::testing::RasterConfig("dem.tif");
        config.albedo = terraingen::AlbedoSpec{std::move(spec), {}};
        return ValidationErrorPath(config, folder);
    };
    EXPECT_EQ(rejected({"map.tif", {0}, {}, {}, {}}), "valid");
    EXPECT_EQ(rejected({"map.tif", {1}, {}, {}, {}}), "albedo.source.bands");
    EXPECT_EQ(rejected({"map.tif", {0}, NAN, {}, {}}), "albedo.source.scale");
    EXPECT_EQ(rejected({"map.tif", {0}, {}, NAN, {}}), "albedo.source.scale");
    terraingen::RasterAlbedoSpec detail{"map.tif", {0}, {}, {}, {}};
    detail.options.detail.octaves = 0;
    EXPECT_EQ(rejected(detail), "albedo.source.detail");
    terraingen::Raster<float> rotated = map;
    rotated.SetTransform({1000.0, 20.0, 1.0, 2000.0, 0.0, -20.0});
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, terraingen::testing::Keys());
    EXPECT_EQ(rejected({"rotated.tif", {0}, {}, {}, {}}), "albedo.source.path");
}

TEST(ValidateConfig, ReportsSphereProblemsByPath) {
    using terraingen::testing::OnASphere;
    const auto error = [](auto mutate) {
        TerrainConfig config = OnASphere();
        mutate(config);
        return ValidationErrorPath(config);
    };
    EXPECT_EQ(error([](TerrainConfig&) {}), "valid");
    EXPECT_EQ(error([](TerrainConfig& c) { c.surface = terraingen::SphereSurfaceSpec{}; }),
              "surface.radius");
    EXPECT_EQ(error([](TerrainConfig& c) { Flat(c).centreY = 89.999; }), "source.height");
    EXPECT_EQ(error([](TerrainConfig& c) { Flat(c).centreX = NAN; }), "source.centre_x");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  Noise(c).stack[0].layer.fractal.basis = terraingen::NoiseBasis::Simplex;
              }),
              "layers[0].stack[0].basis");
    EXPECT_EQ(
        error([](TerrainConfig& c) { c.outputs = {terraingen::BoulderModelsOutput{"b.glb"}}; }),
        "valid");
    EXPECT_EQ(error([](TerrainConfig& c) {
                  terraingen::SyntheticAlbedoSpec synthetic;
                  synthetic.valueNoise = {terraingen::NoiseLayerConfig{}};
                  synthetic.valueNoise[0].layer.fractal.basis = terraingen::NoiseBasis::WorleyF1;
                  c.albedo = terraingen::AlbedoSpec{synthetic, {}};
              }),
              "albedo.source.value_noise[0].basis");
}

TEST(ValidateConfig, ReportsBodyMeshProblemsByPath) {
    const auto error = [](auto mutate) {
        TerrainConfig config;
        config.surface = terraingen::SphereSurfaceSpec{3389500.0};
        config.source = terraingen::FlatSourceSpec{4, 4, 1000.0};
        terraingen::BodyMeshOutput output{"m.glb", 8, std::nullopt};
        mutate(config, output);
        config.outputs = {output};
        return ValidationErrorPath(config);
    };
    EXPECT_EQ(error([](auto&, auto&) {}), "valid");
    EXPECT_EQ(error([](auto& c, auto&) { c.surface = terraingen::PlaneSurfaceSpec{}; }),
              "outputs[0]");
    EXPECT_EQ(error([](auto&, auto& o) { o.path = "m.obj"; }), "outputs[0].path");
    EXPECT_EQ(error([](auto&, auto& o) { o.cellsPerFace = 0; }), "outputs[0].cells_per_face");
    EXPECT_EQ(error([](auto&, auto& o) { o.spacing = -1.0; }), "outputs[0].spacing");
}

TEST(DescribeSource, ReadsTheRasterHeaderAndResampledSize) {
    const auto folder = TempPath("terraingen_describe");
    terraingen::testing::WriteDem(folder, "dem.tif");
    TerrainConfig config = terraingen::testing::RasterConfig("dem.tif");
    const auto grid = terraingen::DescribeSource(config.source, folder);
    EXPECT_EQ(grid.width, 40u);
    EXPECT_EQ(grid.height, 30u);
    EXPECT_EQ(grid.transform, terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 5.0));
    EXPECT_DOUBLE_EQ(grid.PixelSize(), 5.0);
    EXPECT_DOUBLE_EQ(grid.Area(), 200.0 * 150.0);
    const terraingen::Bounds2d bounds = grid.Bounds();
    EXPECT_EQ(bounds.minX, 1000.0);
    EXPECT_EQ(bounds.maxX, 1200.0);
    EXPECT_EQ(bounds.minY, 1850.0);
    EXPECT_EQ(bounds.maxY, 2000.0);
    terraingen::testing::RasterSpec(config).pixelSize = 2.5;
    const auto fine = terraingen::DescribeSource(config.source, folder);
    EXPECT_EQ(fine.width, 80u);
    EXPECT_EQ(fine.height, 60u);
    EXPECT_DOUBLE_EQ(fine.PixelSize(), 2.5);
    EXPECT_EQ(fine.transform.pixelHeight, -2.5) << "keeps the north-up sign";
}

TEST(BuildSurface, BuildsPlanesAndSpheresOfARadius) {
    EXPECT_EQ(terraingen::BuildSurface(terraingen::PlaneSurfaceSpec{}),
              terraingen::Surface::Plane());
    EXPECT_EQ(terraingen::BuildSurface(terraingen::SphereSurfaceSpec{3389500.0}),
              terraingen::Surface::Sphere(3389500.0));
    EXPECT_EQ(terraingen::BuildSurface(terraingen::SphereSurfaceSpec{1737400.0}),
              terraingen::Surface::Sphere(1737400.0));
}

TEST(BuildSurface, ASphereRadiusThatIsNotPositiveIsAnErrorAtTheRadius) {
    for (const double radius : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()}) {
        try {
            terraingen::BuildSurface(terraingen::SphereSurfaceSpec{radius});
            ADD_FAILURE() << "expected a ConfigError for " << radius;
        } catch (const ConfigError& error) {
            EXPECT_EQ(error.Path(), "surface.radius") << radius;
        }
    }
}

TEST(ValidateConfig, ReportsASphereWithoutARadiusAtTheRadius) {
    TerrainConfig config;
    config.surface = terraingen::SphereSurfaceSpec{};
    EXPECT_EQ(ValidationErrorPath(config), "surface.radius");
    config.surface = terraingen::SphereSurfaceSpec{-1737400.0};
    EXPECT_EQ(ValidationErrorPath(config), "surface.radius");
    config.surface = terraingen::SphereSurfaceSpec{1737400.0};
    EXPECT_EQ(ValidationErrorPath(config), "valid");
}
