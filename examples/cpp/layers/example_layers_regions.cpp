/// Regions: different layers in different parts of the terrain. A `RegionLayer` applies a layer
/// only inside a rectangle or circle, faded out over `feather` metres outside it. A circular mare
/// is flattened and lowered, the west half is an old, heavily cratered surface and the east half a
/// younger one. Writes the heights.
///
///     example_layers_regions [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <cstdio>
#include <filesystem>
#include <memory>

namespace {

std::shared_ptr<terraingen::CraterLayer> Craters(double ageGyr, std::uint32_t seed) {
    const terraingen::SurfaceModel surface = terraingen::LunarSurface(ageGyr);
    terraingen::CraterLayerOptions options;
    options.seed = seed;
    options.generation.dMin = 10.0;
    options.generation.dMax = 200.0;
    return std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{surface.density, surface.ages,
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     surface.ages, terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    constexpr double kHalf = 513 * 4.0 / 2;
    terraingen::NoiseLayer fbm;
    fbm.fractal.seed = 9;
    fbm.fractal.wavelength = 1200.0f;
    fbm.fractal.amplitude = 25.0f;
    fbm.fractal.octaves = 6;
    const auto mare = std::make_shared<terraingen::RegionLayer>(
        std::make_shared<terraingen::ScaleLayer>(0.2, -15.0),
        terraingen::RegionShape::Circle({0.0, 0.0}, 600.0), 150.0);

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 4.0);
    terrain.layers = {
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}), mare,
        std::make_shared<terraingen::RegionLayer>(
            Craters(3.9, 9), terraingen::RegionShape::Rectangle({-kHalf, -kHalf, 0.0, kHalf}),
            200.0),
        std::make_shared<terraingen::RegionLayer>(
            Craters(1.0, 10), terraingen::RegionShape::Rectangle({0.0, -kHalf, kHalf, kHalf}),
            200.0)};
    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    std::printf("%zu craters; mare weight at the centre %.2f, 700 m out %.2f\n",
                evaluated.features.craters.Size(), mare->Weight(0.0, 0.0),
                mare->Weight(700.0, 0.0));
    terraingen::WriteGeoTiff(output / "example_layers_regions.tif", evaluated.heights);
    return 0;
}
