/// A stack of noise layers, each combined with the heights so far by its blend mode: rolling fBm,
/// domain-warped ridged crests added on top, and broad value noise as a mask that roughens some
/// regions and calms others. The gain of the crests is set from a Hurst exponent. The same terrain
/// as examples/config/terrain/example_terrain_layered.json, plus the warp.
///
///     example_terrain_layered [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <filesystem>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    terraingen::NoiseLayer rolling;
    rolling.fractal.seed = 1;
    rolling.fractal.wavelength = 800.0f;
    rolling.fractal.amplitude = 40.0f;

    terraingen::NoiseLayer crests;
    crests.fractal.seed = 2;
    crests.fractal.type = terraingen::FractalType::Ridged;
    crests.fractal.wavelength = 250.0f;
    crests.fractal.amplitude = 8.0f;
    crests.fractal.octaves = 6;
    crests.fractal.gain = terraingen::GainFromHurst(0.7f, crests.fractal.lacunarity);
    crests.blend = terraingen::LayerBlend::Add;
    crests.warpAmplitude = 60.0f;
    crests.warpWavelength = 400.0f;

    terraingen::NoiseLayer mask;
    mask.fractal.seed = 3;
    mask.fractal.basis = terraingen::NoiseBasis::Value;
    mask.fractal.wavelength = 1200.0f;
    mask.fractal.amplitude = 1.0f;
    mask.fractal.octaves = 2;
    mask.blend = terraingen::LayerBlend::Mask;

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
    terrain.layers.push_back(std::make_shared<terraingen::NoiseRelief>(
        terraingen::SyntheticTerrainParams{{rolling, crests, mask}}));

    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    const terraingen::RasterStats stats = terraingen::ComputeStats(evaluated.heights);
    std::cout << "Heights from " << stats.min << " to " << stats.max << " m\n";

    const auto path = output / "example_terrain_layered.tif";
    terraingen::WriteGeoTiff(path, evaluated.heights);
    std::cout << "Wrote " << path.string() << "\n";
    return 0;
}
