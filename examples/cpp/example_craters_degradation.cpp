/// Crater degradation models compared on the same crater population: none, a fixed depth ratio,
/// freshness (shallower with age relative to lifetime) and topographic diffusion with the
/// diffusivities of Fassett and Thomson (2014) and Fassett et al. (2022). Prints each model's mean
/// depth-to-diameter ratio and writes each terrain.
///
///     example_craters_degradation [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
    const auto fresh = terraingen::LunarFreshCraterMorphometry;
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(
        std::make_shared<terraingen::FassettThomsonProfile>());
    const std::vector<std::pair<std::string, std::shared_ptr<const terraingen::DegradationModel>>>
        models = {
            {"none", std::make_shared<terraingen::NoDegradation>(fresh)},
            {"fixed", std::make_shared<terraingen::NoDegradation>(fresh, 0.1)},
            {"freshness", std::make_shared<terraingen::FreshnessDegradation>(surface.ages, fresh)},
            {"diffusion_ft2014", std::make_shared<terraingen::DiffusionDegradation>(
                                     fresh, terraingen::FassettThomson2014Diffusivity(), diffused)},
            {"diffusion_f2022", std::make_shared<terraingen::DiffusionDegradation>(
                                    fresh, terraingen::Fassett2022HartmannDiffusivity(), diffused)},
        };
    terraingen::CraterLayerOptions options;
    options.seed = 31;
    options.generation.dMin = 4.0;
    options.generation.dMax = 150.0;
    for (const auto& [name, degradation] : models) {
        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(257, 257, 1.0);
        terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{surface.density, surface.ages, degradation,
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            options));
        const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
        const auto& attributes = evaluated.features.craterAttributes;
        const double sum = std::accumulate(attributes.begin(), attributes.end(), 0.0,
                                           [](double total, const terraingen::CraterAttributes& a) {
                                               return total + a.depthRatio;
                                           });
        std::printf("%-18s mean depth/diameter %.3f over %zu craters\n", name.c_str(),
                    sum / static_cast<double>(attributes.size()), attributes.size());
        terraingen::WriteGeoTiff(output / ("example_craters_degradation_" + name + ".tif"),
                                 evaluated.heights);
    }
    return 0;
}
