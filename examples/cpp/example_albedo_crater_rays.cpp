/// Albedo for a cratered synthetic terrain: a `SyntheticAlbedo` of 0.11 with gentle noise, and a
/// `CraterAlbedo` layer that reads the terrain's crater layer, brightening fresh craters (optical
/// maturity) and giving young ones rays. A hand-placed 2 Myr old crater shows the rays. The same
/// terrain as examples/config/example_albedo_crater_rays.json.
///
///     example_albedo_crater_rays [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    terraingen::NoiseLayer fbm;
    fbm.fractal.seed = 1;
    fbm.fractal.wavelength = 3000.0f;
    fbm.fractal.amplitude = 60.0f;

    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.2);
    terraingen::CraterLayerOptions craterOptions;
    craterOptions.seed = 1;
    craterOptions.generation.dMin = 30.0;
    craterOptions.generation.dMax = 400.0;
    craterOptions.generation.manual.Append(
        terraingen::Crater{.x = 0.0, .y = 0.0, .diameter = 250.0, .ageGyr = 0.002});
    const auto craters = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{surface.density, surface.ages,
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     surface.ages, terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        craterOptions);

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 8.0);
    terrain.layers.push_back(
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
    terrain.layers.push_back(craters);

    terraingen::NoiseLayer variation;
    variation.fractal.seed = 4;
    variation.fractal.wavelength = 1500.0f;
    variation.fractal.amplitude = 0.02f;
    variation.fractal.octaves = 4;
    terraingen::SyntheticAlbedoParams base;
    base.value = 0.11;
    base.valueNoise.push_back(variation);

    terraingen::Albedo albedo;
    albedo.source = std::make_shared<terraingen::SyntheticAlbedo>(base);
    albedo.layers.push_back(std::make_shared<terraingen::CraterAlbedo>(craters));

    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    const terraingen::RasterGrid grid = terraingen::RasterGrid::Of(evaluated.heights);
    const std::vector<terraingen::Raster<float>> bands =
        terraingen::EvaluateAlbedo(albedo, grid, {.spacing = 8.0, .threads = 0});
    const terraingen::RasterStats stats = terraingen::ComputeStats(bands.front());
    std::cout << "Albedo from " << stats.min << " to " << stats.max << " over "
              << evaluated.features.craters.Size() << " craters\n";

    const auto heights = output / "example_albedo_crater_rays_heights.tif";
    const auto path = output / "example_albedo_crater_rays.tif";
    terraingen::WriteGeoTiff(heights, evaluated.heights);
    terraingen::WriteGeoTiff(path, bands);
    std::cout << "Wrote " << heights.string() << " and " << path.string() << "\n";
    return 0;
}
