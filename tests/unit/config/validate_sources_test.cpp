// SPDX-License-Identifier: BSD-3-Clause

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
#include <vector>

using terraingen::ConfigError;
using terraingen::ConfigSpec;
using terraingen::CraterLayerSpec;
using terraingen::ValidateConfig;
using terraingen::testing::Noise;
using terraingen::testing::Region;
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
        ConfigSpec config = RasterConfig("dem.tif");
        mutate(config);
        try {
            ValidateConfig(config, folder);
        } catch (const ConfigError& e) {
            return std::string(e.Path()) + ": " + e.what();
        }
        return std::string("valid");
    };
    EXPECT_EQ(error([](ConfigSpec&) {}), "valid");
    EXPECT_EQ(error([](ConfigSpec& c) { RasterSpec(c).path.clear(); }).rfind("source.path", 0), 0u);
    EXPECT_EQ(error([](ConfigSpec& c) {
                  RasterSpec(c).read.upsampler = terraingen::DefaultUpsampler();
              }).rfind("source.upsampling", 0),
              0u)
        << "the upsampling gives the upsampler";
    EXPECT_NE(error([](ConfigSpec& c) { RasterSpec(c).path = "missing.tif"; }).find("cannot open"),
              std::string::npos);
    EXPECT_NE(error([](ConfigSpec& c) { RasterSpec(c).read.band = 1; }).find("source.band"),
              std::string::npos);
    EXPECT_NE(error([](ConfigSpec& c) { RasterSpec(c).path = "degrees.tif"; }).find("geographic"),
              std::string::npos);
    EXPECT_NE(error([](ConfigSpec& c) {
                  c.body = terraingen::SphereSurfaceSpec{1737400.0};
              }).find("source.path: on a body the raster must be in longitude and latitude"),
              std::string::npos);
    EXPECT_EQ(error([](ConfigSpec& c) {
                  RasterSpec(c).read.effectivePixelSize = -1.0;
              }).rfind("source.effective_pixel_size_m", 0),
              0u);
    EXPECT_EQ(error([](ConfigSpec& c) { RasterSpec(c).read.effectivePixelSize = 20.0; }), "valid");
    EXPECT_EQ(error([](ConfigSpec& c) {
                  terraingen::NoiseLayerSpec noise;
                  noise.stack[0].fractal.octaves = 0;
                  c.layers = {noise};
              }).rfind("layers[0].stack[0]", 0),
              0u);
    EXPECT_EQ(error([](ConfigSpec& c) {
                  CraterLayerSpec craters;
                  craters.options.generation.minDiameter = 0.0;
                  c.layers = {craters};
              }).rfind("layers[0].min_diameter_m", 0),
              0u);
    terraingen::Raster<float> rotated(
        terraingen::RasterGrid{8, 8, terraingen::GeoTransform{0.0, 1.0, 0.2, 0.0, 0.1, -1.0}},
        1.0f);
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, terraingen::testing::Keys());
    EXPECT_EQ(error([](ConfigSpec& c) { RasterSpec(c).path = "rotated.tif"; }), "valid");
    terraingen::WriteGeoTiff(folder / "tiny.tif", terraingen::Raster<float>(1, 1, 0.0f),
                             terraingen::testing::Keys());
    EXPECT_EQ(error([](ConfigSpec& c) { RasterSpec(c).path = "tiny.tif"; }).rfind("source.path", 0),
              0u);
    EXPECT_EQ(error([](ConfigSpec& c) {
                  RasterSpec(c).read.crop = terraingen::Bounds2d{1000.0, 1900.0, 1100.0, 1950.0};
              }),
              "valid");
    EXPECT_EQ(error([](ConfigSpec& c) {
                  RasterSpec(c).read.crop = terraingen::Bounds2d{0.0, 0.0, 10.0, 10.0};
              }).rfind("source.crop", 0),
              0u)
        << "a crop that misses the file";
    EXPECT_EQ(error([](ConfigSpec& c) {
                  RasterSpec(c).read.crop = terraingen::Bounds2d{1100.0, 1900.0, 1050.0, 1950.0};
              }).rfind("source.crop", 0),
              0u)
        << "an empty crop";
    EXPECT_EQ(error([](ConfigSpec& c) {
                  c.source = terraingen::ShapeModelSourceSpec{};
              }).rfind("source: ", 0),
              0u)
        << "a shape model source needs a shape model body";
}

TEST(ValidateConfig, ReportsChainProblemsByPath) {
    const auto folder = TempPath("terraingen_chain_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    const auto raster = [](const std::string& path) { return terraingen::RasterSourceSpec{path}; };
    const auto error = [&](terraingen::ChainSourceSpec chain) {
        ConfigSpec config;
        config.source = std::move(chain);
        config.outputs = {terraingen::testing::Heightmap("h.tif", std::nullopt)};
        return ValidationErrorPath(config, folder);
    };
    EXPECT_EQ(error({{raster("dem.tif"), raster("dem.tif")}}), "valid");
    EXPECT_EQ(error({{raster("dem.tif"), terraingen::SyntheticSourceSpec{}}}), "valid")
        << "a DEM over a flat datum";
    EXPECT_EQ(error({{terraingen::SyntheticSourceSpec{}, raster("dem.tif")}}),
              "outputs[0].extent_m")
        << "a chain without bounds has no grid to cover";
    EXPECT_EQ(error({}), "source.sources");
    EXPECT_EQ(error({{raster("dem.tif"), raster("missing.tif")}}), "source.sources[1].path");
    EXPECT_EQ(error({{terraingen::ChainSourceSpec{{raster("")}}}}),
              "source.sources[0].sources[0].path");
}

TEST(ValidateConfig, ReportsChainSeamProblemsByPath) {
    const auto folder = TempPath("terraingen_chain_seam_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    const auto error = [&](terraingen::ChainSeamMode mode, double width, double decayLength) {
        terraingen::ChainSourceSpec chain{
            {terraingen::RasterSourceSpec{"dem.tif"}, terraingen::SyntheticSourceSpec{}}};
        chain.seams.mode = mode;
        chain.seams.width = width;
        chain.seams.decayLength = decayLength;
        ConfigSpec config;
        config.source = std::move(chain);
        config.outputs = {terraingen::testing::Heightmap("h.tif", std::nullopt)};
        return ValidationErrorPath(config, folder);
    };
    using terraingen::ChainSeamMode;
    EXPECT_EQ(error(ChainSeamMode::None, 50.0, 50.0), "valid");
    EXPECT_EQ(error(ChainSeamMode::Feather, 20.0, 50.0), "valid");
    EXPECT_EQ(error(ChainSeamMode::Height, 50.0, 100.0), "valid");
    EXPECT_EQ(error(ChainSeamMode::Slope, 50.0, 100.0), "valid");
    EXPECT_EQ(error(ChainSeamMode::Feather, 0.0, 50.0), "source.seams.width_m");
    EXPECT_EQ(error(ChainSeamMode::Height, 50.0, -1.0), "source.seams.decay_length_m");
    EXPECT_EQ(error(ChainSeamMode::None, 50.0, 0.0), "source.seams.decay_length_m")
        << "checked in every mode";
    EXPECT_EQ(error(ChainSeamMode::Height, 50.0, 1e6), "source.seams.decay_length_m")
        << "a lattice of 5 m posts reaching 3000 km";
    EXPECT_EQ(error(ChainSeamMode::Feather, 1e6, 50.0), "source.seams.width_m");
    EXPECT_EQ(error(ChainSeamMode::None, 1e6, 1e6), "valid") << "no lattice without seams";
}

TEST(ValidateConfig, ReportsOutputGridProblemsByPath) {
    const auto folder = TempPath("terraingen_output_grids");
    terraingen::testing::WriteDem(folder, "dem.tif");
    const auto error = [&](terraingen::OutputSpec output, auto mutate) {
        ConfigSpec config;
        config.outputs = {std::move(output)};
        mutate(config);
        return ValidationErrorPath(config, folder);
    };
    const auto unchanged = [](ConfigSpec&) {};
    const auto moon = [](ConfigSpec& c) { c.body = terraingen::SphereSurfaceSpec{1737400.0}; };
    const auto dem = [](ConfigSpec& c) { c.source = terraingen::RasterSourceSpec{"dem.tif"}; };
    const terraingen::OutputSpec heightmap = terraingen::testing::Heightmap("h.tif", std::nullopt);
    EXPECT_EQ(error(heightmap, unchanged), "outputs[0].extent_m");
    EXPECT_EQ(error(heightmap, moon), "outputs[0].extent_deg");
    EXPECT_EQ(error(heightmap, dem), "valid") << "the source's grid";
    terraingen::HeightmapOutput finer;
    finer.area.pixelSize = 2.0;
    EXPECT_EQ(error(finer, dem), "valid") << "the source's extent at 2 m";
    EXPECT_EQ(error(finer, unchanged), "outputs[0].extent_m") << "synthetic noise has no extent";
    terraingen::HeightmapOutput boxed;
    boxed.area.extentM = terraingen::ExtentSpec{{}, {}, {{1010.0, 1900.0}}, {{1050.0, 1950.0}}};
    EXPECT_EQ(error(boxed, dem), "valid") << "an extent at the source's 5 m";
    EXPECT_EQ(error(boxed, unchanged), "outputs[0].pixel_size_m") << "nor a cell size";
    EXPECT_EQ(error(terraingen::testing::Heightmap("h.tif", {{0, 4, 1.0}}), unchanged),
              "outputs[0].extent_m.size[0]");
    EXPECT_EQ(error(terraingen::testing::Heightmap("h.tif", {{4, 4, -1.0}}), unchanged),
              "outputs[0].pixel_size_m");
    EXPECT_EQ(error(terraingen::TerrainMeshOutput{"t.glb"}, unchanged), "outputs[0].extent_m");
    EXPECT_EQ(error(terraingen::testing::Heightmap("h.tif"), moon), "outputs[0].extent_m")
        << "a body takes extent_deg";

    terraingen::OutputAreaSpec offBody =
        terraingen::testing::GridRegion{65, 49, 4.0, 0.0, 89.999}.Area(
            terraingen::Surface::Sphere(1737400.0));
    EXPECT_EQ(error(terraingen::HeightmapOutput{"h.tif", {}, 256, offBody}, moon),
              "outputs[0].extent_deg")
        << "past the pole";

    const terraingen::OutputSpec list = terraingen::CraterListOutput{{"c.csv"}};
    EXPECT_EQ(error(list, unchanged), "outputs[0].extent_m");
    EXPECT_EQ(error(list, moon), "outputs[0].extent_deg") << "the whole body needs a pixel size";
    EXPECT_EQ(error(list, dem), "valid");
    terraingen::OutputAreaSpec wholeArea;
    wholeArea.pixelSize = 1000.0;
    const terraingen::OutputSpec whole = terraingen::CraterListOutput{{"c.csv", {}, wholeArea}};
    EXPECT_EQ(error(whole, moon), "valid");
    EXPECT_EQ(error(whole, unchanged), "outputs[0].extent_m") << "a plane has no whole body";
    wholeArea.pixelSize = -1.0;
    EXPECT_EQ(error(terraingen::CraterListOutput{{"c.csv", {}, wholeArea}}, moon),
              "outputs[0].pixel_size_m");
    wholeArea.pixelSize = 1e-9;
    EXPECT_EQ(error(terraingen::CraterListOutput{{"c.csv", {}, wholeArea}}, moon),
              "outputs[0].pixel_size_m");
    wholeArea.pixelSize = 1000.0;
    EXPECT_EQ(error(terraingen::BoulderModelsOutput{"b.glb", 35.0, wholeArea}, moon), "valid");
}

TEST(ValidateConfig, ReportsRasterAlbedoProblemsByPath) {
    const auto folder = TempPath("terraingen_albedo_errors");
    terraingen::testing::WriteDem(folder, "dem.tif");
    terraingen::Raster<float> map(
        terraingen::RasterGrid{10, 8, terraingen::GeoTransform::NorthUp(1000.0, 2000.0, 20.0)},
        1000.0f);
    terraingen::WriteGeoTiff(folder / "map.tif", map, terraingen::testing::Keys());
    const auto rejected = [&](terraingen::RasterAlbedoSpec spec) {
        ConfigSpec config = terraingen::testing::RasterConfig("dem.tif");
        config.albedo = terraingen::AlbedoSpec{std::move(spec), {}};
        return ValidationErrorPath(config, folder);
    };
    EXPECT_EQ(rejected({"map.tif", {0}, {}, {}, {}}), "valid");
    EXPECT_EQ(rejected({"map.tif", {1}, {}, {}, {}}), "albedo.source.bands");
    EXPECT_EQ(rejected({"map.tif", {0}, NAN, {}, {}}), "albedo.source.scale");
    EXPECT_EQ(rejected({"map.tif", {0}, {}, NAN, {}}), "albedo.source.offset");
    terraingen::RasterAlbedoSpec detail{"map.tif", {0}, {}, {}, {}};
    detail.options.detail.octaves = 0;
    EXPECT_EQ(rejected(detail), "albedo.source.detail.octaves");
    const terraingen::Raster<float> rotated(
        terraingen::RasterGrid{map.Width(), map.Height(), {1000.0, 20.0, 1.0, 2000.0, 0.0, -20.0}},
        map.Values());
    terraingen::WriteGeoTiff(folder / "rotated.tif", rotated, terraingen::testing::Keys());
    EXPECT_EQ(rejected({"rotated.tif", {0}, {}, {}, {}}), "albedo.source.path");
}

TEST(ValidateConfig, ReportsBodyProblemsByPath) {
    using terraingen::testing::OnASphere;
    const auto error = [](auto mutate) {
        ConfigSpec config = OnASphere();
        mutate(config);
        return ValidationErrorPath(config);
    };
    EXPECT_EQ(error([](ConfigSpec&) {}), "valid");
    EXPECT_EQ(error([](ConfigSpec& c) { c.body = terraingen::SphereSurfaceSpec{}; }),
              "body.radius_m");
    EXPECT_EQ(error([](ConfigSpec& c) { Region(c).extentDeg->center = {30.0, 89.999}; }),
              "outputs[0].extent_deg");
    EXPECT_EQ(error([](ConfigSpec& c) {
                  Region(c).extentDeg->center = {std::numeric_limits<double>::quiet_NaN(), 0.0};
              }),
              "outputs[0].extent_deg.center[0]");
    EXPECT_EQ(error([](ConfigSpec& c) { Region(c).extentDeg->size = {361.0, 1.0}; }),
              "outputs[0].pixel_size_m")
        << "too many cells before off the body";
    EXPECT_EQ(error([](ConfigSpec& c) {
                  Region(c).pixelSize = 5000.0;
                  Region(c).extentDeg->size = {361.0, 1.0};
              }),
              "outputs[0].extent_deg")
        << "more than one turn of longitude";
    EXPECT_EQ(error([](ConfigSpec& c) {
                  Region(c).extentM = Region(c).extentDeg;
                  Region(c).extentDeg.reset();
              }),
              "outputs[0].extent_m");
    EXPECT_EQ(error([](ConfigSpec& c) {
                  terraingen::TerrainMeshOutput mesh{"t.glb", Region(c)};
                  mesh.siteDeg = {30.0, -45.0};
                  c.outputs.push_back(mesh);
              }),
              "valid");
    for (std::uint32_t b = 0; b < terraingen::kNoiseBasisCount; ++b) {
        const auto basis = static_cast<terraingen::NoiseBasis>(b);
        SCOPED_TRACE(b);
        EXPECT_EQ(error([basis](ConfigSpec& c) { Noise(c).stack[0].fractal.basis = basis; }),
                  "valid");
        EXPECT_EQ(error([basis](ConfigSpec& c) {
                      terraingen::SyntheticAlbedoSpec synthetic;
                      synthetic.valueNoise = {terraingen::AlbedoFractalSpec{}};
                      synthetic.valueNoise[0].fractal.basis = basis;
                      synthetic.valueNoise[0].amplitude = 0.02f;
                      c.albedo = terraingen::AlbedoSpec{synthetic, {}};
                  }),
                  "valid");
    }
    EXPECT_EQ(error([](ConfigSpec& c) {
                  terraingen::SyntheticAlbedoSpec synthetic;
                  synthetic.valueNoise = {terraingen::AlbedoFractalSpec{}};
                  c.albedo = terraingen::AlbedoSpec{synthetic, {}};
              }),
              "albedo.source.value_noise[0].amplitude");
    EXPECT_EQ(error([](ConfigSpec& c) {
                  c.outputs.push_back(terraingen::BoulderModelsOutput{"b.glb", 35.0, Region(c)});
              }),
              "valid");
}

TEST(ValidateConfig, ReportsMeshProblemsByPath) {
    const auto error = [](auto mutate) {
        ConfigSpec config;
        config.body = terraingen::SphereSurfaceSpec{3389500.0};
        terraingen::MeshOutput output{"m.glb", 8, std::nullopt};
        mutate(config, output);
        config.outputs = {output};
        return ValidationErrorPath(config);
    };
    EXPECT_EQ(error([](auto&, auto&) {}), "valid");
    EXPECT_EQ(error([](auto& c, auto&) { c.body = terraingen::PlaneSurfaceSpec{}; }), "outputs[0]");
    EXPECT_EQ(error([](auto&, auto& o) { o.path = "m.fbx"; }), "outputs[0].path");
    EXPECT_EQ(error([](auto&, auto& o) { o.cellsPerFace = 0; }), "outputs[0].cells_per_face");
    EXPECT_EQ(error([](auto&, auto& o) { o.spacing = -1.0; }), "outputs[0].spacing_m");
    EXPECT_EQ(error([](auto&, auto& o) { o.path.clear(); }), "valid") << "returned, not written";

    ConfigSpec twice;
    twice.body = terraingen::SphereSurfaceSpec{3389500.0};
    twice.outputs = {terraingen::MeshOutput{"m.glb", 8, std::nullopt},
                     terraingen::MeshOutput{"m.obj", 8, std::nullopt},
                     terraingen::MeshOutput{"m.icq", 16, std::nullopt}};
    EXPECT_EQ(ValidationErrorPath(twice), "valid") << "one mesh in two formats, and an ICQ model";
    twice.outputs.push_back(terraingen::MeshOutput{"m.stl", 16, std::nullopt});
    EXPECT_EQ(ValidationErrorPath(twice), "outputs[3]") << "body meshes share one result";
}

TEST(ValidateConfig, ReadsTheRasterSourcesHeader) {
    const auto folder = TempPath("terraingen_describe");
    terraingen::testing::WriteDem(folder, "dem.tif");
    ConfigSpec config = terraingen::testing::RasterConfig("dem.tif");
    const terraingen::RasterGrid grid = terraingen::ValidateConfig(config, folder).source.value();
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
    terraingen::testing::RasterSpec(config).upsampling.type = "lanczos3";
    EXPECT_EQ(terraingen::ValidateConfig(config, folder).source, grid)
        << "upsampling leaves the grid alone";
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
            EXPECT_EQ(error.Path(), "body.radius_m") << radius;
        }
    }
}

TEST(ValidateConfig, ReportsASphereWithoutARadiusAtTheRadius) {
    ConfigSpec config;
    config.body = terraingen::SphereSurfaceSpec{};
    EXPECT_EQ(ValidationErrorPath(config), "body.radius_m");
    config.body = terraingen::SphereSurfaceSpec{-1737400.0};
    EXPECT_EQ(ValidationErrorPath(config), "body.radius_m");
    config.body = terraingen::SphereSurfaceSpec{1737400.0};
    EXPECT_EQ(ValidationErrorPath(config), "valid");
}

TEST(BuildSurface, BuildsEllipsoidsOfThreeRadii) {
    EXPECT_EQ(terraingen::BuildSurface(terraingen::EllipsoidSurfaceSpec{{270.0, 147.0, 104.0}}),
              terraingen::Surface::Ellipsoid(270.0, 147.0, 104.0));
    EXPECT_EQ(terraingen::BuildSurface(terraingen::EllipsoidSurfaceSpec{{5.0, 5.0, 5.0}}),
              terraingen::Surface::Sphere(5.0));
}

TEST(BuildSurface, EllipsoidRadiiOfTheWrongCountOrSignAreErrorsAtThem) {
    const auto path = [](std::vector<double> radii) {
        try {
            terraingen::BuildSurface(terraingen::EllipsoidSurfaceSpec{std::move(radii)});
        } catch (const ConfigError& error) {
            return error.Path();
        }
        return std::string("valid");
    };
    EXPECT_EQ(path({}), "body.radii_m");
    EXPECT_EQ(path({1.0, 2.0}), "body.radii_m");
    EXPECT_EQ(path({1.0, 2.0, 3.0, 4.0}), "body.radii_m");
    EXPECT_EQ(path({1.0, -2.0, 3.0}), "body.radii_m[1]");
    EXPECT_EQ(path({1.0, 2.0, std::numeric_limits<double>::quiet_NaN()}), "body.radii_m[2]");
    EXPECT_EQ(path({1.0, 2.0, 3.0}), "valid");
}

TEST(ValidateConfig, ReportsEllipsoidRadiiByPath) {
    ConfigSpec config;
    config.body = terraingen::EllipsoidSurfaceSpec{{270.0, 0.0, 104.0}};
    EXPECT_EQ(ValidationErrorPath(config), "body.radii_m[1]");
    config.body = terraingen::EllipsoidSurfaceSpec{{270000.0, 147000.0, 104000.0}};
    EXPECT_EQ(ValidationErrorPath(config), "valid");
}
