// SPDX-License-Identifier: BSD-3-Clause

/// Hand-placed craters given an explicit rim-to-floor depth, one with its rim height as well, on a
/// small flat site under the Martin-Parkes-Dunstan profile, which honors both; one more crater
/// gives only its diameter and takes its depth from the degradation model. The same terrain as
/// examples/config/craters/example_craters_manual_depth.json.
///
///     example_craters_manual_depth [--output-dir DIR] [--test]
///     terraingen-examples craters_manual_depth

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

/// A manual crater at (`x`, `y`), `diameter` across, `depth` deep from rim to floor, with its
/// rim `rimHeight` above the surface (NaN derives it from the depth).
terraingen::Crater Deep(double x, double y, double diameter, double depth,
                        double rimHeight = std::nan("")) {
    return terraingen::Crater{x, y, diameter, 0.0, 0u, true, depth, rimHeight};
}

class CratersManualDepth : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        const auto ages = std::make_shared<terraingen::UniformAges>(1.0);
        const terraingen::CraterModels models{
            std::make_shared<terraingen::PowerLawSfd>(1e-6, 2.0), ages,
            std::make_shared<terraingen::FreshnessDegradation>(
                ages, terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::MartinParkesDunstanProfile>()};
        terraingen::CraterLayerOptions options;
        options.generateRandom = false;
        manual_ = {
            Deep(-22.0, 20.0, 20.0, 3.0, 0.8),
            Deep(22.0, 22.0, 16.0, 1.2),
            Deep(-20.0, -22.0, 24.0, 2.0, 0.3),
            terraingen::Crater{22.0, -20.0, 18.0},
        };
        for (const terraingen::Crater& crater : manual_) {
            options.generation.manual.Append(crater);
        }

        terraingen::TerrainGenerator terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(401, 401, 0.25);
        terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(models, options));

        evaluated_ = terraingen::Evaluate(terrain);
        craters_ =
            terraingen::PlacedCraters(terrain, terraingen::RasterGrid::FromRaster(evaluated_));
        for (std::size_t i = 0; i < craters_.craters.Size(); ++i) {
            const terraingen::Crater crater = craters_.craters.At(i);
            const terraingen::CraterAttributes& attributes = craters_.attributes[i];
            Out() << crater.diameter << " m crater, ";
            if (std::isnan(crater.depth)) {
                Out() << "depth from the model";
            } else {
                Out() << "asked " << crater.depth << " m deep";
            }
            if (!std::isnan(crater.rimHeight)) {
                Out() << " with a " << crater.rimHeight << " m rim";
            }
            Out() << ": floor " << attributes.floorDepth << " m below and rim "
                  << attributes.rimHeight << " m above the surface, rim to floor "
                  << attributes.floorDepth + attributes.rimHeight << " m\n";
        }
        Show("heights", evaluated_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto path = dir / "example_craters_manual_depth.tif";
        terraingen::WriteGeoTiff(path, evaluated_);
        Out() << "Wrote " << path.string() << "\n";
    }

    void Test() override {
        const terraingen::CraterList& craters = craters_.craters;
        Check(evaluated_.Width() == 401 && evaluated_.Height() == 401, "the heights are 401 x 401");
        Check(terraingen::ComputeStats(evaluated_).validCount == evaluated_.Size(),
              "every height is finite");
        Check(craters.Size() == manual_.size() && craters_.attributes.size() == craters.Size(),
              "exactly the four hand-placed craters, each with attributes");
        bool depths = true;
        bool rims = true;
        bool modeled = false;
        for (std::size_t i = 0; i < craters.Size(); ++i) {
            const terraingen::Crater crater = craters.At(i);
            const terraingen::CraterAttributes& a = craters_.attributes[i];
            const double tolerance = 1e-3 * crater.diameter;
            if (std::isnan(crater.depth)) {
                modeled = a.floorDepth + a.rimHeight > 1.0;
                continue;
            }
            depths = depths && std::fabs(a.floorDepth + a.rimHeight - crater.depth) < tolerance;
            rims = rims && (std::isnan(crater.rimHeight) ||
                            std::fabs(a.rimHeight - crater.rimHeight) < tolerance);
        }
        Check(depths, "each crater given a depth is that deep from rim to floor");
        Check(rims, "each crater given a rim height has that rim");
        Check(modeled, "the crater given only a diameter takes its depth from the model");
        Check(terraingen::ComputeStats(evaluated_).min < -1.0, "the craters are cut into the site");
    }

  private:
    std::vector<terraingen::Crater> manual_;
    terraingen::Raster<float> evaluated_;
    terraingen::CraterFeatures craters_;
};

}  // namespace

TERRAINGEN_EXAMPLE(CratersManualDepth, "Hand-placed craters of chosen depth and rim height")
