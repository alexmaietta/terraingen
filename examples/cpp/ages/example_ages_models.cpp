/// How crater age models change a cratered terrain, and an age model of your own.
///
/// `FreshAges` makes every crater new, `UniformAges` spreads ages over one lifetime, and
/// `SteadyStateAges` gives each size the time it survives before equilibrium cratering replaces it.
/// A custom model derives from `CraterAgeModel`. Diffusion degradation shows the difference, since
/// it smooths each crater by its absolute age. Prints age percentiles and the relief left, and
/// writes each terrain.
///
///     example_ages_models [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

/// A crater D metres across lasts D / 50 Gyr, up to the surface's 3.5 Gyr, its age uniform within.
class SizeLimitedLifetimes final : public terraingen::CraterAgeModel {
  protected:
    double LifetimeAt(double diameter) const override {
        return std::min(diameter / 50.0, 3.5);
    }

    double SampleAgeAt(terraingen::RandomStream& random, double /*diameter*/,
                       double lifetimeGyr) const override {
        return lifetimeGyr * random.NextDouble();
    }
};

double Percentile(std::vector<double> values, double share) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(share * static_cast<double>(values.size() - 1))];
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
    const auto production =
        std::make_shared<terraingen::SplicedSfd>(terraingen::ExtendedNeukumProductionFunction());
    const auto equilibrium =
        std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium());
    const std::vector<std::pair<std::string, std::shared_ptr<const terraingen::CraterAgeModel>>>
        models = {
            {"fresh", std::make_shared<terraingen::FreshAges>()},
            {"uniform_3.5_gyr", std::make_shared<terraingen::UniformAges>(3.5)},
            {"steady_state",
             std::make_shared<terraingen::SteadyStateAges>(production, equilibrium, 3.5)},
            {"custom", std::make_shared<SizeLimitedLifetimes>()},
        };
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(
        std::make_shared<terraingen::FassettThomsonProfile>());
    const auto diffusion = std::make_shared<terraingen::DiffusionDegradation>(
        terraingen::LunarFreshCraterMorphometry, terraingen::Fassett2022HartmannDiffusivity(),
        diffused);

    terraingen::CraterLayerOptions options;
    options.generation.dMin = 8.0;
    options.generation.dMax = 300.0;
    std::printf("%-16s %8s %8s %8s %10s\n", "ages", "10%", "50%", "90%", "relief");
    for (const auto& [name, ages] : models) {
        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(257, 257, 2.0);
        terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{surface.density, ages, diffusion,
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            options));
        const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
        const auto& craterAges = evaluated.features.craters.AgesGyr();
        const terraingen::RasterStats stats = terraingen::ComputeStats(evaluated.heights);
        std::printf("%-16s %8.3f %8.3f %8.3f %8.2f m\n", name.c_str(), Percentile(craterAges, 0.1),
                    Percentile(craterAges, 0.5), Percentile(craterAges, 0.9),
                    stats.max - stats.min);
        terraingen::WriteGeoTiff(output / ("example_ages_models_" + name + ".tif"),
                                 evaluated.heights);
    }
    return 0;
}
