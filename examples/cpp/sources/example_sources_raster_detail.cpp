/// Detail on a coarse DEM: a `GeoTiffSource` resampled to a finer pixel, plus fine noise and
/// craters smaller than the DEM's cells. The example first writes a coarse 16 m DEM of its own to
/// stand in for orbital data, then builds the 2 m terrain on it. A `SourceChain` fills any gaps in
/// the DEM from a flat datum.
///
///     example_sources_raster_detail [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <cstdio>
#include <filesystem>
#include <memory>

namespace {

terraingen::CraterModels LunarModels() {
    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
    return {surface.density, surface.ages,
            std::make_shared<terraingen::FreshnessDegradation>(
                surface.ages, terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);
    const std::filesystem::path dem = output / "example_sources_raster_detail_coarse.tif";

    terraingen::NoiseLayer broad;
    broad.fractal.seed = 4;
    broad.fractal.wavelength = 800.0f;
    broad.fractal.amplitude = 40.0f;
    terraingen::CraterLayerOptions large;
    large.seed = 4;
    large.generation.dMin = 100.0;
    large.generation.dMax = 600.0;
    terraingen::Terrain coarse;
    coarse.source = std::make_shared<terraingen::FlatSource>(65, 65, 16.0);
    coarse.layers = {
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{broad}}),
        std::make_shared<terraingen::CraterLayer>(LunarModels(), large)};
    terraingen::WriteGeoTiff(dem, terraingen::Evaluate(coarse).heights);

    const auto raster =
        std::make_shared<terraingen::GeoTiffSource>(dem, 0, 2.0, terraingen::ResampleMethod::Cubic);
    const auto chain = std::make_shared<terraingen::SourceChain>(
        std::vector<std::shared_ptr<const terraingen::Source>>{
            raster, std::make_shared<terraingen::FlatSource>(1, 1, 2.0)});
    const terraingen::RasterGrid grid = raster->Grid();
    std::printf("16 m DEM resampled to %zu x %zu cells of 2 m\n", grid.width, grid.height);

    terraingen::NoiseLayer detail;
    detail.fractal.seed = 6;
    detail.fractal.wavelength = 60.0f;
    detail.fractal.amplitude = 1.5f;
    detail.fractal.octaves = 5;
    terraingen::CraterLayerOptions small;
    small.seed = 5;
    small.generation.dMin = 6.0;
    small.generation.dMax = 100.0;
    terraingen::Terrain terrain;
    terrain.source = chain;
    terrain.layers = {
        std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{detail}}),
        std::make_shared<terraingen::CraterLayer>(LunarModels(), small)};
    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    std::printf("added %zu craters of 6 to 100 m\n", evaluated.features.craters.Size());
    terraingen::WriteGeoTiff(output / "example_sources_raster_detail.tif", evaluated.heights);
    return 0;
}
