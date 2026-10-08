/// Lunar craters on a synthetic terrain: a `CraterLayer` with the DSNE lunar surface model for a
/// 3.5 Gyr surface (crater density and ages), freshness degradation and the Fassett and Thomson
/// profile, added to the fBm terrain of example_terrain_fbm. The same terrain as
/// examples/config/craters/example_craters_simple.json.
///
///     example_craters_simple [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <algorithm>
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

    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
    const terraingen::CraterModels models{
        surface.density, surface.ages,
        std::make_shared<terraingen::FreshnessDegradation>(surface.ages,
                                                           terraingen::LunarFreshCraterMorphometry),
        std::make_shared<terraingen::FassettThomsonProfile>()};
    terraingen::CraterLayerOptions options;
    options.seed = 1;
    options.generation.dMin = 8.0;
    options.generation.dMax = 300.0;

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
    terrain.layers.push_back(
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(models, options));

    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    const terraingen::CraterList& craters = evaluated.features.craters;
    const auto& diameters = craters.Diameters();
    std::cout << craters.Size() << " craters, the largest "
              << *std::max_element(diameters.begin(), diameters.end()) << " m across\n";
    for (const double d : {8.0, 16.0, 32.0, 64.0, 128.0}) {
        std::cout << "  N(>= " << d << " m) = "
                  << std::count_if(diameters.begin(), diameters.end(),
                                   [d](double diameter) { return diameter >= d; })
                  << "\n";
    }

    const auto heights = output / "example_craters_simple.tif";
    const auto list = output / "example_craters_simple.csv";
    terraingen::WriteGeoTiff(heights, evaluated.heights);
    terraingen::WriteCraterList(craters, list, terraingen::CraterListFormat::Csv);
    std::cout << "Wrote " << heights.string() << " and " << list.string() << "\n";
    return 0;
}
