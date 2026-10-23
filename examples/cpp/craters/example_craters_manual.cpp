// SPDX-License-Identifier: BSD-3-Clause

/// Hand-placed craters on a synthetic terrain, with no generated population: each crater has a
/// position, diameter and age, and its age sets how degraded it is. The same terrain as
/// examples/config/craters/example_craters_manual.json.
///
///     example_craters_manual [--output-dir DIR] [--test]
///     terraingen-examples craters_manual

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

class CratersManual : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 800.0f;
        fbm.fractal.amplitude = 40.0f;

        const auto ages = std::make_shared<terraingen::UniformAges>(3.5);
        const terraingen::CraterModels models{
            std::make_shared<terraingen::PowerLawSfd>(1e-6, 2.0), ages,
            std::make_shared<terraingen::FreshnessDegradation>(
                ages, terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()};
        terraingen::CraterList manual;
        for (const terraingen::Crater& crater : {
                 terraingen::Crater{0.0, 0.0, 200.0, 0.5},
                 terraingen::Crater{-250.0, 180.0, 90.0, 3.0},
                 terraingen::Crater{260.0, -200.0, 120.0, 1.5},
                 terraingen::Crater{150.0, 250.0, 40.0, 0.1},
             }) {
            manual.Append(crater);
        }

        terraingen::TerrainGenerator terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
        terrain.layers.push_back(
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
        terrain.layers.push_back(terraingen::CraterLayer::Manual(models, manual));

        evaluated_ = terraingen::Evaluate(terrain);
        craters_ =
            terraingen::PlacedCraters(terrain, terraingen::RasterGrid::FromRaster(evaluated_));
        for (std::size_t i = 0; i < craters_.craters.Size(); ++i) {
            const terraingen::Crater crater = craters_.craters.At(i);
            const terraingen::CraterAttributes& attributes = craters_.attributes[i];
            Out() << crater.diameter << " m crater, " << crater.ageGyr
                  << " Gyr old: depth/diameter " << attributes.depthRatio << "\n";
        }
        Show("heights", evaluated_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto path = dir / "example_craters_manual.tif";
        terraingen::WriteGeoTiff(path, evaluated_);
        Out() << "Wrote " << path.string() << "\n";
    }

    void Test() override {
        const terraingen::CraterList& craters = craters_.craters;
        const auto& attributes = craters_.attributes;
        Check(evaluated_.Width() == 513 && evaluated_.Height() == 513, "the heights are 513 x 513");
        Check(terraingen::ComputeStats(evaluated_).validCount == evaluated_.Size(),
              "every height is finite");
        std::vector<double> diameters(craters.Diameters().begin(), craters.Diameters().end());
        std::sort(diameters.begin(), diameters.end());
        Check(diameters == std::vector<double>{40.0, 90.0, 120.0, 200.0},
              "exactly the four hand-placed craters");
        Check(attributes.size() == craters.Size(), "every crater has attributes");
        std::vector<std::size_t> order(craters.Size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return craters.At(a).ageGyr < craters.At(b).ageGyr;
        });
        bool fading = true;
        for (std::size_t i = 1; i < order.size(); ++i) {
            fading = fading && attributes[order[i]].freshness <= attributes[order[i - 1]].freshness;
        }
        Check(fading, "older craters are less fresh");
        Check(std::all_of(attributes.begin(), attributes.end(),
                          [&](const terraingen::CraterAttributes& a) {
                              return a.depthRatio >= attributes[order.back()].depthRatio;
                          }),
              "the oldest crater is the shallowest");
    }

  private:
    terraingen::Raster<float> evaluated_;
    terraingen::CraterFeatures craters_;
};

}  // namespace

TERRAINGEN_EXAMPLE(CratersManual, "Hand-placed craters of chosen sizes and ages")
