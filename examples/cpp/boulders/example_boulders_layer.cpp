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

        const terraingen::SurfaceModel surface = terraingen::LunarSurface(3.5);
        terraingen::CraterLayerOptions craterOptions;
        craterOptions.generation.dMin = 3.0;
        craterOptions.generation.dMax = 20.0;
        const auto craters = std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{surface.density, surface.ages,
                                     std::make_shared<terraingen::FreshnessDegradation>(
                                         surface.ages, terraingen::LunarFreshCraterMorphometry),
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
        const terraingen::TerrainFeatures& features = evaluated_.features;
        Out() << features.craters.Size() << " craters, " << features.boulders.size()
              << " boulders, " << features.boulderModels.size() << " of them boulder models\n";
        Show("heights", evaluated_.heights);
    }

    void Save(const std::filesystem::path& dir) override {
        const terraingen::TerrainFeatures& features = evaluated_.features;
        const auto heights = dir / "example_boulders_layer.tif";
        const auto list = dir / "example_boulders_layer.csv";
        const auto models = dir / "example_boulders_layer.glb";
        terraingen::WriteGeoTiff(heights, evaluated_.heights);
        terraingen::WriteBoulderList(features.boulders, list, terraingen::ListFormat::Csv);
        terraingen::WriteBoulderModels(features.boulderModels, models);
        Out() << "Wrote " << heights.string() << ", " << list.string() << " and " << models.string()
              << "\n";
    }

    void Test() override {
        const terraingen::TerrainFeatures& features = evaluated_.features;
        Check(evaluated_.heights.Width() == 401 && evaluated_.heights.Height() == 401,
              "the heights are 401 x 401");
        Check(features.craters.Size() > 5, "more than 5 craters");
        Check(features.boulders.size() > 150 && features.boulders.size() < 1500,
              "a few hundred boulders for 5% cover");
        Check(std::all_of(features.boulders.begin(), features.boulders.end(),
                          [](const terraingen::Boulder& r) {
                              return r.diameter >= 0.2 && r.diameter <= 3.0;
                          }),
              "every boulder is 0.2 to 3 m across");
        Check(!features.boulderModels.empty() &&
                  features.boulderModels.size() < features.boulders.size() / 10,
              "a few of the boulders are boulder models");
        Check(std::all_of(
                  features.boulderModels.begin(), features.boulderModels.end(),
                  [](const terraingen::BoulderModel& b) { return b.boulder.diameter >= 0.6; }),
              "every boulder model is at least 0.6 m across");
    }

  private:
    terraingen::EvaluatedTerrain evaluated_;
};

}  // namespace

TERRAINGEN_EXAMPLE(BouldersLayer, "Boulders and boulder models on a cratered terrain")
