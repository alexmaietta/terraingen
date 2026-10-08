/// Crater profiles: each built-in profile across a 100 m crater, and a profile of your own.
///
/// Prints the height every 10 m from the centre for Fassett and Thomson's fresh profile (three rim
/// fits), Martin, Parkes and Dunstan's, the fresh profile after 0.1 Gyr of topographic diffusion,
/// and a parabolic bowl that derives from `CraterProfile`. Then shapes a crater population with the
/// custom profile and writes the terrain.
///
///     example_craters_profiles [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

/// A paraboloid bowl with a raised rim falling off as (r / R)^-3 outside it.
class ParabolicBowl final : public terraingen::CraterProfile {
  public:
    double Height(double distance, double diameter,
                  const terraingen::CraterState& state) const override {
        const double x = distance / (0.5 * diameter);
        const double depth = state.current.depthRatio * diameter;
        const double rim = state.current.rimHeightRatio * diameter;
        return x <= 1.0 ? rim - depth * (1.0 - x * x) : rim * std::pow(x, -3.0);
    }

    double ReachRadii() const override {
        return 2.0;
    }
};

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    constexpr double kDiameter = 100.0;
    const terraingen::CraterMorphometry fresh = terraingen::LunarFreshCraterMorphometry(kDiameter);
    const terraingen::CraterState state{fresh, fresh, 0.0};
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(
        std::make_shared<terraingen::FassettThomsonProfile>());
    const terraingen::DiffusionDegradation diffusion(terraingen::LunarFreshCraterMorphometry,
                                                     terraingen::FassettThomson2014Diffusivity(),
                                                     diffused);
    const terraingen::CraterState oldState = diffusion.State({0.0, 0.0, kDiameter, 0.1, 1u, true});

    using terraingen::FassettThomsonRim;
    const std::vector<std::tuple<std::string, std::shared_ptr<const terraingen::CraterProfile>,
                                 terraingen::CraterState>>
        profiles = {
            {"FT original rim",
             std::make_shared<terraingen::FassettThomsonProfile>(FassettThomsonRim::Original),
             state},
            {"FT rim at one radius",
             std::make_shared<terraingen::FassettThomsonProfile>(FassettThomsonRim::AtOneRadius),
             state},
            {"FT plateau rim",
             std::make_shared<terraingen::FassettThomsonProfile>(FassettThomsonRim::Plateau),
             state},
            {"Martin-Parkes-Dunstan", std::make_shared<terraingen::MartinParkesDunstanProfile>(),
             state},
            {"diffused 0.1 Gyr", diffused, oldState},
            {"custom parabolic bowl", std::make_shared<ParabolicBowl>(), state},
        };
    std::printf("%-22s", "distance (m)");
    for (int r = 0; r <= 100; r += 10) {
        std::printf("%7d", r);
    }
    std::printf("\n");
    for (const auto& [name, profile, crater] : profiles) {
        std::printf("%-22s", name.c_str());
        for (int r = 0; r <= 100; r += 10) {
            std::printf("%7.2f", profile->Height(r, kDiameter, crater));
        }
        std::printf("\n");
    }

    const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
    terraingen::CraterLayerOptions options;
    options.seed = 2;
    options.generation.dMin = 8.0;
    options.generation.dMax = 150.0;
    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(257, 257, 1.0);
    terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{surface.density, surface.ages,
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     surface.ages, terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<ParabolicBowl>()},
        options));
    const auto path = output / "example_craters_profiles.tif";
    terraingen::WriteGeoTiff(path, terraingen::Evaluate(terrain).heights);
    std::printf("Wrote %s\n", path.string().c_str());
    return 0;
}
