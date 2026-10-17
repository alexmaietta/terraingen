/// Boulders on a cratered synthetic terrain: a `BoulderLayer` drawing boulders from Golombek and
/// Rapp's (1997) model for 5% boulder cover, small boulders drawn into the heights and those 0.6 m
/// or larger placed as 3D boulder models. Writes the heights, the boulder list and the boulders as
/// a glTF scene. Like examples/config/boulders/example_boulders_models.json, with craters as well.
///
///     example_boulders_layer [--output-dir DIR] [--test]
///     terraingen-examples boulders_layer

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace {

class BouldersLayer : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        terraingen::NoiseLayer fbm;
        fbm.fractal.seed = 1;
        fbm.fractal.wavelength = 40.0f;
        fbm.fractal.amplitude = 0.5f;
        fbm.fractal.octaves = 6;

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
        terraingen::CraterLayerOptions craterOptions;
        craterOptions.generation.dMin = 3.0;
        craterOptions.generation.dMax = 20.0;
        const auto craters = std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{density, ages,
                                     std::make_shared<terraingen::FreshnessDegradation>(
                                         ages, terraingen::LunarFreshCraterMorphometry),
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            craterOptions);

        terraingen::BoulderLayerOptions boulderOptions;
        boulderOptions.sizes.coverage = 0.05;
        boulderOptions.sizes.minDiameter = 0.2;
        boulderOptions.sizes.maxDiameter = 3.0;
        boulderOptions.rendering.models.minDiameter = 0.6;

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(401, 401, 0.1);
        terrain.layers.push_back(
            std::make_shared<terraingen::NoiseRelief>(terraingen::SyntheticTerrainParams{{fbm}}));
        terrain.layers.push_back(craters);
        terrain.layers.push_back(
            std::make_shared<terraingen::BoulderLayer>(boulderOptions, craters));

        evaluated_ = terraingen::Evaluate(terrain);
        craters_ =
            terraingen::PlacedCraters(terrain, terraingen::RasterGrid::FromRaster(evaluated_));
        boulders_ =
            terraingen::PlacedBoulders(terrain, terraingen::RasterGrid::FromRaster(evaluated_));
        Out() << craters_.craters.Size() << " craters, " << boulders_.boulders.size()
              << " boulders, " << boulders_.models.size() << " of them boulder models\n";
        Show("heights", evaluated_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto heights = dir / "example_boulders_layer.tif";
        const auto list = dir / "example_boulders_layer.csv";
        const auto models = dir / "example_boulders_layer.glb";
        terraingen::WriteGeoTiff(heights, evaluated_);
        terraingen::WriteBoulderList(boulders_.boulders, list, terraingen::ListFormat::Csv);
        terraingen::WriteBoulderModels(boulders_.models, models);
        Out() << "Wrote " << heights.string() << ", " << list.string() << " and " << models.string()
              << "\n";
    }

    void Test() override {
        Check(evaluated_.Width() == 401 && evaluated_.Height() == 401, "the heights are 401 x 401");
        Check(craters_.craters.Size() > 5, "more than 5 craters");
        Check(boulders_.boulders.size() > 150 && boulders_.boulders.size() < 1500,
              "a few hundred boulders for 5% cover");
        Check(std::all_of(boulders_.boulders.begin(), boulders_.boulders.end(),
                          [](const terraingen::Boulder& r) {
                              return r.diameter >= 0.2 && r.diameter <= 3.0;
                          }),
              "every boulder is 0.2 to 3 m across");
        Check(!boulders_.models.empty() && boulders_.models.size() < boulders_.boulders.size() / 10,
              "a few of the boulders are boulder models");
        Check(std::all_of(
                  boulders_.models.begin(), boulders_.models.end(),
                  [](const terraingen::BoulderModel& b) { return b.boulder.diameter >= 0.6; }),
              "every boulder model is at least 0.6 m across");
    }

  private:
    terraingen::Raster<float> evaluated_;
    terraingen::CraterFeatures craters_;
    terraingen::BoulderFeatures boulders_;
};

}  // namespace

TERRAINGEN_EXAMPLE(BouldersLayer, "Boulders and boulder models on a cratered terrain")
