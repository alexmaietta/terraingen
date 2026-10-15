/// How crater age models change a cratered terrain, and an age model of your own.
///
/// `FreshAges` makes every crater new, `UniformAges` spreads ages over one lifetime, and
/// `SteadyStateAges` gives each size the time it survives before equilibrium cratering replaces it.
/// A custom model derives from `CraterAgeModel`. Diffusion degradation shows the difference, since
/// it smooths each crater by its absolute age. Prints age percentiles and the relief left, and
/// writes each terrain.
///
///     example_ages_models [--output-dir DIR] [--test]
///     terraingen-examples ages_models

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <array>
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

/// One line of the printed table, formatted as by printf.
template <typename... Args> std::string Format(const char* format, Args... args) {
    std::array<char, 256> buffer{};
    std::snprintf(buffer.data(), buffer.size(), format, args...);
    return buffer.data();
}

double Relief(const terraingen::Raster<float>& heights) {
    const terraingen::RasterStats stats = terraingen::ComputeStats(heights);
    return stats.max - stats.min;
}

class AgesModels : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
        const auto production = std::make_shared<terraingen::SplicedSfd>(
            terraingen::ExtendedNeukumProductionFunction());
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
        Out() << Format("%-16s %8s %8s %8s %10s\n", "ages", "10%", "50%", "90%", "relief");
        terrains_.clear();
        for (const auto& [name, ages] : models) {
            terraingen::Terrain terrain;
            terrain.source = std::make_shared<terraingen::FlatSource>(257, 257, 2.0);
            terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
                terraingen::CraterModels{surface.density, ages, diffusion,
                                         std::make_shared<terraingen::FassettThomsonProfile>()},
                options));
            terrains_.emplace_back(name, terraingen::Evaluate(terrain));
            const terraingen::EvaluatedTerrain& evaluated = terrains_.back().second;
            const auto& craterAges = evaluated.features.craters.AgesGyr();
            Out() << Format("%-16s %8.3f %8.3f %8.3f %8.2f m\n", name.c_str(),
                            Percentile(craterAges, 0.1), Percentile(craterAges, 0.5),
                            Percentile(craterAges, 0.9), Relief(evaluated.heights));
            Show(name, evaluated.heights);
        }
    }

    void Save(const std::filesystem::path& dir) override {
        for (const auto& [name, evaluated] : terrains_) {
            const auto path = dir / ("example_ages_models_" + name + ".tif");
            terraingen::WriteGeoTiff(path, evaluated.heights);
            Out() << "Wrote " << path.string() << "\n";
        }
    }

    void Test() override {
        Check(terrains_.size() == 4, "four age models");
        const auto& fresh = terrains_[0].second;
        const auto& uniform = terrains_[1].second;
        const auto& steady = terrains_[2].second;
        const auto& custom = terrains_[3].second;
        Check(fresh.heights.Width() == 257 && fresh.heights.Height() == 257,
              "the heights are 257 x 257");
        Check(fresh.features.craters.Size() > 50, "more than 50 craters");
        const auto& freshAges = fresh.features.craters.AgesGyr();
        Check(std::all_of(freshAges.begin(), freshAges.end(), [](double a) { return a == 0.0; }),
              "fresh ages are all zero");
        const auto& uniformAges = uniform.features.craters.AgesGyr();
        Check(std::all_of(uniformAges.begin(), uniformAges.end(),
                          [](double a) { return a >= 0.0 && a <= 3.5; }),
              "uniform ages lie within 3.5 Gyr");
        Check(Percentile(steady.features.craters.AgesGyr(), 0.5) <
                  0.5 * Percentile(uniformAges, 0.5),
              "steady-state craters are mostly younger than uniformly aged ones");
        const auto& diameters = custom.features.craters.Diameters();
        const auto& customAges = custom.features.craters.AgesGyr();
        bool withinLifetime = true;
        for (std::size_t i = 0; i < diameters.size(); ++i) {
            withinLifetime = withinLifetime && customAges[i] <= diameters[i] / 50.0 + 1e-9;
        }
        Check(withinLifetime, "custom ages lie within each crater's D / 50 Gyr lifetime");
        const double freshRelief = Relief(fresh.heights);
        Check(Relief(uniform.heights) < 0.8 * freshRelief &&
                  Relief(steady.heights) < 0.8 * freshRelief &&
                  Relief(custom.heights) < 0.8 * freshRelief,
              "diffusion leaves aged terrains less relief than the fresh one");
    }

  private:
    std::vector<std::pair<std::string, terraingen::EvaluatedTerrain>> terrains_;
};

}  // namespace

TERRAINGEN_EXAMPLE(AgesModels, "Crater age models and a custom one under diffusion")
