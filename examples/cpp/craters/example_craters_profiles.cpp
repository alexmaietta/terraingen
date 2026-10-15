/// Crater profiles: each built-in profile across a 100 m crater, and a profile of your own.
///
/// Prints the height every 10 m from the centre for Fassett and Thomson's fresh profile (three rim
/// fits), Martin, Parkes and Dunstan's, the fresh profile after 0.1 Gyr of topographic diffusion,
/// and a parabolic bowl that derives from `CraterProfile`. Then shapes a crater population with the
/// custom profile and writes the terrain.
///
///     example_craters_profiles [--output-dir DIR] [--test]
///     terraingen-examples craters_profiles

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tuple>
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

class CratersProfiles : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        constexpr double kDiameter = 100.0;
        const terraingen::CraterMorphometry fresh =
            terraingen::LunarFreshCraterMorphometry(kDiameter);
        const terraingen::CraterState state{fresh, fresh, 0.0};
        const auto diffused = std::make_shared<terraingen::DiffusedProfile>(
            std::make_shared<terraingen::FassettThomsonProfile>());
        const terraingen::DiffusionDegradation diffusion(
            terraingen::LunarFreshCraterMorphometry, terraingen::FassettThomson2014Diffusivity(),
            diffused);
        const terraingen::CraterState oldState =
            diffusion.State({0.0, 0.0, kDiameter, 0.1, 1u, true});

        using terraingen::FassettThomsonRim;
        const std::vector<std::tuple<std::string, std::shared_ptr<const terraingen::CraterProfile>,
                                     terraingen::CraterState>>
            profiles = {
                {"FT original rim",
                 std::make_shared<terraingen::FassettThomsonProfile>(FassettThomsonRim::Original),
                 state},
                {"FT rim at one radius",
                 std::make_shared<terraingen::FassettThomsonProfile>(
                     FassettThomsonRim::AtOneRadius),
                 state},
                {"FT plateau rim",
                 std::make_shared<terraingen::FassettThomsonProfile>(FassettThomsonRim::Plateau),
                 state},
                {"Martin-Parkes-Dunstan",
                 std::make_shared<terraingen::MartinParkesDunstanProfile>(), state},
                {"diffused 0.1 Gyr", diffused, oldState},
                {"custom parabolic bowl", std::make_shared<ParabolicBowl>(), state},
            };
        char cell[64];
        std::snprintf(cell, sizeof(cell), "%-22s", "distance (m)");
        Out() << cell;
        for (int r = 0; r <= 100; r += 10) {
            std::snprintf(cell, sizeof(cell), "%7d", r);
            Out() << cell;
        }
        Out() << "\n";
        table_.clear();
        for (const auto& [name, profile, crater] : profiles) {
            std::snprintf(cell, sizeof(cell), "%-22s", name.c_str());
            Out() << cell;
            for (int r = 0; r <= 100; r += 10) {
                table_[name].push_back(profile->Height(r, kDiameter, crater));
                std::snprintf(cell, sizeof(cell), "%7.2f", table_[name].back());
                Out() << cell;
            }
            Out() << "\n";
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
        evaluated_ = terraingen::Evaluate(terrain);
        Show("parabolic bowl craters", evaluated_.heights);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto path = dir / "example_craters_profiles.tif";
        terraingen::WriteGeoTiff(path, evaluated_.heights);
        Out() << "Wrote " << path.string() << "\n";
    }

    void Test() override {
        Check(table_.size() == 6, "six profiles");
        bool shaped = true;
        for (const auto& [name, heights] : table_) {
            const double highest = *std::max_element(heights.begin(), heights.end());
            shaped = shaped && std::isfinite(heights.front()) && heights.front() < 0.0 &&
                     (name == "diffused 0.1 Gyr" || highest > 1.0);
        }
        Check(shaped, "every profile has a floor below the datum, every fresh one a rim above it");
        const auto& fresh = table_.at("FT original rim");
        const auto& old = table_.at("diffused 0.1 Gyr");
        Check(old.front() > fresh.front() && *std::max_element(old.begin(), old.end()) <
                                                 *std::max_element(fresh.begin(), fresh.end()),
              "diffusion raises the floor and lowers the rim");
        Check(evaluated_.heights.Width() == 257 && evaluated_.heights.Height() == 257,
              "the terrain is 257 x 257");
        Check(evaluated_.features.craters.Size() > 10, "more than 10 craters shaped as bowls");
    }

  private:
    std::map<std::string, std::vector<double>> table_;
    terraingen::EvaluatedTerrain evaluated_;
};

}  // namespace

TERRAINGEN_EXAMPLE(CratersProfiles, "Built-in crater profiles and a custom one")
