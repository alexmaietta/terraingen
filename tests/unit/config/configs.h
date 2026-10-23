/// @file
/// @brief Small configurations and raster fixtures shared by the pipeline tests.
#pragma once

#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/json.h"
#include "terraingen/config/validate.h"
#include "terraingen/pipeline/features.h"
#include "terraingen/raster/geotiff/geotiff.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace terraingen::testing {

/// The heights generated over the first grid.
inline const Raster<float>& HeightsOf(const GeneratedOutputs& generated) {
    return generated.grids.at(0).heights;
}

/// The albedo bands generated over the first grid.
inline const std::vector<Raster<float>>& AlbedoOf(const GeneratedOutputs& generated) {
    return generated.grids.at(0).albedo;
}

/// The craters the generator's layers apply over the first grid.
inline CraterFeatures CratersOf(const GeneratedOutputs& generated) {
    return PlacedCraters(generated.generator, RasterGrid::FromRaster(HeightsOf(generated)));
}

/// The boulders the generator's layers place over the first grid, and the models among them.
inline BoulderFeatures BouldersOf(const GeneratedOutputs& generated) {
    return PlacedBoulders(generated.generator, RasterGrid::FromRaster(HeightsOf(generated)));
}

/// The craters the generator's layers apply over `region`.
inline CraterFeatures CratersOf(const GeneratedOutputs& generated, const GridRegionSpec& region) {
    return PlacedCraters(generated.generator,
                         config::RegionGrid(region, generated.generator.surface));
}

/// The boulders the generator's layers place over `region`.
inline BoulderFeatures BouldersOf(const GeneratedOutputs& generated, const GridRegionSpec& region) {
    return PlacedBoulders(generated.generator,
                          config::RegionGrid(region, generated.generator.surface));
}

/// The region of `Small`'s heightmap: 65 x 49 cells of 2 m.
inline GridRegionSpec SmallRegion() {
    return {65, 49, 2.0};
}

/// A heightmap output to `path` over `region`.
inline HeightmapOutput Heightmap(const std::string& path,
                                 std::optional<GridRegionSpec> region = SmallRegion()) {
    HeightmapOutput output;
    output.path = path;
    output.region = region;
    return output;
}

/// @brief The region of a configuration's first output, a heightmap with a region.
/// @param config A configuration such as `Small`.
/// @return Its region spec.
inline GridRegionSpec& Region(TerrainGenConfig& config) {
    return *std::get<HeightmapOutput>(config.outputs.at(0)).region;
}

/// @brief The noise layer of a `Small` configuration.
/// @param config A `Small` configuration.
/// @return Its first layer.
inline NoiseLayerSpec& Noise(TerrainGenConfig& config) {
    return std::get<NoiseLayerSpec>(config.layers.at(0));
}

/// @brief The crater layer of a `Small` configuration.
/// @param config A `Small` configuration.
/// @return Its second layer.
inline CraterLayerSpec& Craters(TerrainGenConfig& config) {
    return std::get<CraterLayerSpec>(config.layers.at(1));
}

/// @brief The density of a lunar surface: the least of an extended Neukum production surface and
/// Trask equilibrium.
/// @param ageGyr The surface age in Gyr.
/// @return The density spec.
inline MinimumSpec LunarDensitySpec(double ageGyr = 3.5) {
    return MinimumSpec{
        {ProductionSurfaceSpec{ProductionFunction::ExtendedNeukum, ChronologyModel::Neukum, ageGyr},
         TraskSpec{}}};
}

/// @brief The ages of a lunar surface: steady state from the extended Neukum production function
/// and Trask equilibrium.
/// @param ageGyr The surface age in Gyr.
/// @return The ages spec.
inline SteadyStateAgesSpec LunarAgesSpec(double ageGyr = 3.5) {
    return SteadyStateAgesSpec{ageGyr, ProductionFunction::ExtendedNeukum, EquilibriumModel::Trask,
                               ChronologyModel::Neukum};
}

/// @brief A small, fast configuration: a noise layer, then a crater layer of 8 to 60 m craters on
/// a 3.5 Gyr lunar surface, and a heightmap over 65 x 49 cells of 2 m (`SmallRegion`).
/// @return The configuration.
inline TerrainGenConfig Small() {
    TerrainGenConfig config;
    config.outputs = {Heightmap("terrain.tif")};
    NoiseLayerSpec noise;
    noise.stack[0].layer.fractal.wavelength = 200.0f;
    noise.stack[0].amplitude = 10.0f;
    CraterLayerSpec craters;
    craters.minDiameter = 8.0;
    craters.maxDiameter = 60.0;
    craters.density = LunarDensitySpec();
    craters.ages = LunarAgesSpec();
    craters.degradation = terraingen::FreshnessDegradationSpec{};
    craters.apply.irregularity.enabled = false;
    config.layers = {noise, craters};
    return config;
}

/// @brief `Small` with a synthetic albedo and bright crater material from its crater layer, and an
/// albedo output over its heightmap's region.
/// @return The configuration.
inline TerrainGenConfig WithAlbedo() {
    TerrainGenConfig config = Small();
    Craters(config).manual = {{10.0, -8.0, 30.0, 0.0, 77u, true}};
    SyntheticAlbedoSpec synthetic;
    synthetic.rgb = true;
    synthetic.saturation = 0.05;
    synthetic.valueNoise = {NoiseLayerConfig{}};
    synthetic.valueNoise[0].layer.fractal.amplitude = 0.2f;
    synthetic.valueNoise[0].layer.fractal.wavelength = 40.0f;
    config.albedo = AlbedoSpec{synthetic, {CraterAlbedoSpec{}}};
    AlbedoOutput output;
    output.path = "albedo.tif";
    output.region = SmallRegion();
    config.outputs.push_back(output);
    return config;
}

/// @brief `Small` on a lunar-sized sphere: its heightmap of 4 m cells at (30, -45).
/// @return The configuration.
inline TerrainGenConfig OnASphere() {
    TerrainGenConfig config = Small();
    config.body = SphereSurfaceSpec{1737400.0};
    Region(config).centreX = 30.0;
    Region(config).centreY = -45.0;
    Region(config).pixelSize = 4.0;
    return config;
}

/// @brief The JSON path of the `ConfigError` that validating a configuration throws.
/// @param config The configuration.
/// @param directory Folder relative paths resolve against.
/// @return The path, or "valid".
inline std::string ValidationErrorPath(const TerrainGenConfig& config,
                                       const std::filesystem::path& directory = {}) {
    try {
        ValidateConfig(config, directory);
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "valid";
}

/// @brief `ValidationErrorPath` of a changed `Small` configuration.
/// @param mutate Changes the configuration.
/// @return The path, or "valid".
template <typename Mutate> std::string ErrorPath(Mutate mutate) {
    TerrainGenConfig config = Small();
    mutate(config);
    return ValidationErrorPath(config);
}

/// @brief The JSON path of the `ConfigError` that parsing a configuration throws.
/// @param json The configuration JSON.
/// @return The path, or "no error".
inline std::string ParseErrorPath(const std::string& json) {
    try {
        ParseConfig(json);
    } catch (const ConfigError& error) {
        return error.Path();
    }
    return "no error";
}

/// @brief The message of the `ConfigError` that parsing a configuration throws.
/// @param json The configuration JSON.
/// @return The message, or "no error".
inline std::string ParseErrorMessage(const std::string& json) {
    try {
        ParseConfig(json);
    } catch (const ConfigError& error) {
        return error.what();
    }
    return "no error";
}

/// @brief The text of a file.
/// @param path The file.
/// @return Its contents.
inline std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// @brief GeoKeys for a projected, or geographic, CRS with pixel-is-area rasters.
/// @param geographic Whether the CRS is geographic.
/// @return The keys.
inline GeoKeys Keys(bool geographic = false) {
    return {{1, 1, 0, 2, 1024, 0, 1, static_cast<std::uint16_t>(geographic ? 2 : 1), 1025, 0, 1, 1},
            {},
            {}};
}

/// @brief Writes a 40 x 30 raster of 5 m cells with origin (1000, 2000).
/// @param folder The folder, created when missing.
/// @param name The file name.
/// @param keys The raster's GeoKeys.
/// @return The raster written.
inline Raster<float> WriteDem(const std::filesystem::path& folder, const std::string& name,
                              const GeoKeys& keys = Keys()) {
    Raster<float> dem(40, 30);
    dem.SetTransform(GeoTransform::NorthUp(1000.0, 2000.0, 5.0));
    for (std::size_t row = 0; row < dem.Height(); ++row) {
        for (std::size_t col = 0; col < dem.Width(); ++col) {
            dem(col, row) = static_cast<float>(10.0 * std::sin(0.2 * static_cast<double>(col)) +
                                               0.5 * static_cast<double>(row));
        }
    }
    std::filesystem::create_directories(folder);
    WriteGeoTiff(folder / name, dem, keys);
    return dem;
}

/// @brief A configuration with a raster source and a heightmap over its grid.
/// @param path The raster's path.
/// @return The configuration.
inline TerrainGenConfig RasterConfig(const std::string& path) {
    TerrainGenConfig config;
    config.source = RasterSourceSpec{path};
    config.outputs = {Heightmap("terrain.tif", std::nullopt)};
    return config;
}

/// @brief The raster source of a `RasterConfig`.
/// @param config A configuration with a raster source.
/// @return Its source spec.
inline RasterSourceSpec& RasterSpec(TerrainGenConfig& config) {
    return std::get<RasterSourceSpec>(*config.source);
}

}  // namespace terraingen::testing
