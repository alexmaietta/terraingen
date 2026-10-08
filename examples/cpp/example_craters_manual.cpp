/// Hand-placed craters on a synthetic terrain, with no generated population: each crater has a
/// position, diameter and age, and its age sets how degraded it is. The same terrain as
/// examples/config/example_craters_manual.json.
///
///     example_craters_manual [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    terraingen::NoiseLayer fbm;
    fbm.fractal.seed = 1;
    fbm.fractal.wavelength = 800.0f;
    fbm.fractal.amplitude = 40.0f;

    const auto ages = std::make_shared<terraingen::UniformAges>(3.5);
    const terraingen::CraterModels models{std::make_shared<terraingen::PowerLawSfd>(1e-6, 2.0),
                                          ages,
                                          std::make_shared<terraingen::FreshnessDegradation>(
                                              ages, terraingen::LunarFreshCraterMorphometry),
                                          std::make_shared<terraingen::FassettThomsonProfile>()};
    terraingen::CraterLayerOptions options;
    options.generate = false;
    for (const terraingen::Crater& crater : {
             terraingen::Crater{.x = 0.0, .y = 0.0, .diameter = 200.0, .ageGyr = 0.5},
             terraingen::Crater{.x = -250.0, .y = 180.0, .diameter = 90.0, .ageGyr = 3.0},
             terraingen::Crater{.x = 260.0, .y = -200.0, .diameter = 120.0, .ageGyr = 1.5},
             terraingen::Crater{.x = 150.0, .y = 250.0, .diameter = 40.0, .ageGyr = 0.1},
         }) {
        options.generation.manual.Append(crater);
    }

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
    terrain.layers.push_back(
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(models, options));

    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    for (std::size_t i = 0; i < evaluated.features.craters.Size(); ++i) {
        const terraingen::Crater crater = evaluated.features.craters.At(i);
        const terraingen::CraterAttributes& attributes = evaluated.features.craterAttributes[i];
        std::cout << crater.diameter << " m crater, " << crater.ageGyr
                  << " Gyr old: depth/diameter " << attributes.depthRatio << "\n";
    }

    const auto path = output / "example_craters_manual.tif";
    terraingen::WriteGeoTiff(path, evaluated.heights);
    std::cout << "Wrote " << path.string() << "\n";
    return 0;
}
