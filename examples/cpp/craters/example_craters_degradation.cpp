// SPDX-License-Identifier: BSD-3-Clause

/// Crater degradation models compared on the same crater population: none, a fixed depth ratio,
/// freshness (shallower with age relative to lifetime) and topographic diffusion with the
/// diffusivities of Fassett and Thomson (2014) and Fassett et al. (2022). Prints each model's mean
/// depth-to-diameter ratio and writes each terrain.
///
///     example_craters_degradation [--output-dir DIR] [--test]
///     terraingen-examples craters_degradation

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {

class CratersDegradation : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        const auto production = std::make_shared<terraingen::SplicedSfd>(
            terraingen::ExtendedNeukumProductionFunction());
        const auto equilibrium =
            std::make_shared<terraingen::PowerLawSfd>(terraingen::TraskEquilibrium());
        const auto density = std::make_shared<terraingen::MinimumSfd>(
            std::vector<std::shared_ptr<const terraingen::SizeFrequencyDistribution>>{
                std::make_shared<terraingen::ProductionSurface>(production, 3.5,
                                                                terraingen::NeukumLunarChronology),
                equilibrium});
        const auto ages = std::make_shared<terraingen::SteadyStateAges>(
            production, equilibrium, 3.5, terraingen::NeukumLunarChronology);
        const auto fresh = terraingen::LunarFreshCraterMorphometry;
        const auto profile = std::make_shared<terraingen::FassettThomsonProfile>();
        const auto diffused = std::make_shared<terraingen::DiffusedProfile>(profile);
        struct Model {
            std::string name;
            std::shared_ptr<const terraingen::DegradationModel> degradation;
            std::shared_ptr<const terraingen::CraterProfile> profile;
        };
        const std::vector<Model> models = {
            {"none", std::make_shared<terraingen::NoDegradation>(fresh), profile},
            {"fixed", std::make_shared<terraingen::NoDegradation>(fresh, 0.1), profile},
            {"freshness", std::make_shared<terraingen::FreshnessDegradation>(ages, fresh), profile},
            {"diffusion_ft2014",
             std::make_shared<terraingen::DiffusionDegradation>(
                 fresh, terraingen::FassettThomson2014Diffusivity(), diffused),
             diffused},
            {"diffusion_f2022",
             std::make_shared<terraingen::DiffusionDegradation>(
                 fresh, terraingen::Fassett2022HartmannDiffusivity(), diffused),
             diffused},
        };
        terraingen::CraterLayerOptions options;
        options.seed = 31;
        options.generation.dMin = 4.0;
        options.generation.dMax = 150.0;
        evaluated_.clear();
        meanRatio_.clear();
        for (const Model& model : models) {
            const std::string& name = model.name;
            terraingen::TerrainGenerator terrain;
            terrain.source = std::make_shared<terraingen::FlatSource>(257, 257, 1.0);
            terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(
                terraingen::CraterModels{density, ages, model.degradation, model.profile},
                options));
            terraingen::Raster<float> heights = terraingen::Evaluate(terrain);
            terraingen::CraterFeatures craters =
                terraingen::PlacedCraters(terrain, terraingen::RasterGrid::FromRaster(heights));
            evaluated_.push_back({name, std::move(heights), std::move(craters)});
            const auto& attributes = evaluated_.back().craters.attributes;
            const double sum =
                std::accumulate(attributes.begin(), attributes.end(), 0.0,
                                [](double total, const terraingen::CraterAttributes& a) {
                                    return total + a.depthRatio;
                                });
            meanRatio_[name] = sum / static_cast<double>(attributes.size());
            char line[128];
            std::snprintf(line, sizeof(line), "%-18s mean depth/diameter %.3f over %zu craters\n",
                          name.c_str(), meanRatio_[name], attributes.size());
            Out() << line;
        }
        for (const char* name :
             {"freshness", "none", "fixed", "diffusion_ft2014", "diffusion_f2022"}) {
            for (const Degraded& degraded : evaluated_) {
                if (degraded.name == name) {
                    Show(degraded.name, degraded.heights);
                }
            }
        }
    }

    void Save(const std::filesystem::path& dir) override {
        for (const Degraded& degraded : evaluated_) {
            terraingen::WriteGeoTiff(
                dir / ("example_craters_degradation_" + degraded.name + ".tif"), degraded.heights);
        }
    }

    void Test() override {
        Check(evaluated_.size() == 5, "five degradation models");
        const auto centered = [](const Degraded& degraded) {
            const terraingen::Bounds2d bounds =
                terraingen::RasterGrid::FromRaster(degraded.heights).Bounds();
            const terraingen::CraterList& craters = degraded.craters.craters;
            std::size_t count = 0;
            for (std::size_t n = 0; n < craters.Size(); ++n) {
                count += bounds.Contains(craters.At(n).x, craters.At(n).y) ? 1 : 0;
            }
            return count;
        };
        const std::size_t count = centered(evaluated_.front());
        bool sameCraters = true;
        bool finite = true;
        for (const Degraded& degraded : evaluated_) {
            sameCraters = sameCraters && centered(degraded) == count;
            finite = finite && terraingen::ComputeStats(degraded.heights).validCount ==
                                   degraded.heights.Size();
        }
        Check(count > 10, "more than 10 craters");
        Check(sameCraters, "every model has the same craters");
        Check(finite, "every height is finite");
        Check(std::abs(meanRatio_.at("fixed") - 0.1) < 1e-6, "the fixed model keeps d/D at 0.1");
        Check(meanRatio_.at("freshness") < meanRatio_.at("none") &&
                  meanRatio_.at("diffusion_ft2014") < meanRatio_.at("none") &&
                  meanRatio_.at("diffusion_f2022") < meanRatio_.at("none"),
              "every degradation model makes craters shallower than fresh");
    }

  private:
    /// One degradation model's terrain: its heights and the craters it placed.
    struct Degraded {
        std::string name;
        terraingen::Raster<float> heights;
        terraingen::CraterFeatures craters;
    };

    std::vector<Degraded> evaluated_;
    std::map<std::string, double> meanRatio_;
};

}  // namespace

TERRAINGEN_EXAMPLE(CratersDegradation, "Crater degradation models on the same craters")
