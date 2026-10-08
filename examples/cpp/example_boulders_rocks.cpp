/// Rocks on a cratered synthetic terrain: a `RockLayer` drawing rocks from Golombek and Rapp's
/// (1997) model for 5% rock cover, small rocks drawn into the heights and those 0.6 m or larger
/// placed as 3D boulder models. Writes the heights, the rock list and the boulders as a glTF scene.
/// Like examples/config/example_boulders_models.json, with craters as well.
///
///     example_boulders_rocks [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    terraingen::NoiseLayer fbm;
    fbm.fractal.seed = 1;
    fbm.fractal.wavelength = 40.0f;
    fbm.fractal.amplitude = 0.5f;
    fbm.fractal.octaves = 6;

    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
    terraingen::CraterLayerOptions craterOptions;
    craterOptions.generation.dMin = 3.0;
    craterOptions.generation.dMax = 20.0;
    const auto craters = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{surface.density, surface.ages,
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     surface.ages, terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        craterOptions);

    terraingen::RockLayerOptions rockOptions;
    rockOptions.coverage = 0.05;
    rockOptions.minDiameter = 0.2;
    rockOptions.maxDiameter = 3.0;
    rockOptions.models.minDiameter = 0.6;

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(401, 401, 0.1);
    terrain.layers.push_back(
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
    terrain.layers.push_back(craters);
    terrain.layers.push_back(std::make_shared<terraingen::RockLayer>(rockOptions, craters));

    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    const terraingen::TerrainFeatures& features = evaluated.features;
    std::cout << features.craters.Size() << " craters, " << features.rocks.size() << " rocks, "
              << features.boulders.size() << " of them boulder models\n";

    const auto heights = output / "example_boulders_rocks.tif";
    const auto rocks = output / "example_boulders_rocks.csv";
    const auto boulders = output / "example_boulders_rocks.glb";
    terraingen::WriteGeoTiff(heights, evaluated.heights);
    terraingen::WriteRockList(features.rocks, rocks, terraingen::CraterListFormat::Csv);
    terraingen::WriteBoulderModels(features.boulders, boulders);
    std::cout << "Wrote " << heights.string() << ", " << rocks.string() << " and "
              << boulders.string() << "\n";
    return 0;
}
