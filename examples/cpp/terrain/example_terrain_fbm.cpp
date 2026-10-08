/// A synthetic terrain from library objects: a flat source with one fBm noise layer, composed into
/// a `Terrain`. Evaluates it over the source's grid, queries single points as a physics engine
/// would, and writes a GeoTIFF. The same terrain as
/// examples/config/terrain/example_terrain_fbm.json.
///
///     example_terrain_fbm [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    terraingen::NoiseLayer fbm;
    fbm.fractal.seed = 1;
    fbm.fractal.wavelength = 800.0f;
    fbm.fractal.amplitude = 40.0f;
    fbm.fractal.octaves = 8;

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
    terrain.layers.push_back(
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));

    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    const terraingen::RasterStats stats = terraingen::ComputeStats(evaluated.heights);
    std::cout << "Heights from " << stats.min << " to " << stats.max << " m\n";

    for (const auto& [x, y] : {std::pair{0.0, 0.0}, std::pair{120.5, -310.25}}) {
        std::cout << "Height at (" << x << ", " << y
                  << "): " << terraingen::EvaluateAt(terrain, x, y, 2.0) << " m\n";
    }

    const auto path = output / "example_terrain_fbm.tif";
    terraingen::WriteGeoTiff(path, evaluated.heights);
    std::cout << "Wrote " << path.string() << "\n";
    return 0;
}
