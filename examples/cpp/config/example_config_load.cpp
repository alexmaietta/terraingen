/// Generates the terrain a JSON config describes and writes its outputs, as the terraingen command
/// does: LoadConfig, ValidateConfig, GenerateTerrain, WriteOutputs. The other examples build the
/// same kinds of terrain from library objects instead. Without CONFIG, generates
/// examples/config/craters/example_craters_simple.json.
///
///     example_config_load [CONFIG] [--output-dir DIR] [--test]
///     terraingen-examples config_load [CONFIG]

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <filesystem>

namespace {

class ConfigLoad : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        path_ = Args().positional.empty() ? terraingen::examples::ExamplesDir() / "config" /
                                                "craters" / "example_craters_simple.json"
                                          : std::filesystem::path(Args().positional.front());
        config_ = terraingen::LoadConfig(path_);
        terraingen::ValidateConfig(config_, path_.parent_path());
        terrain_ = terraingen::GenerateTerrain(config_, path_.parent_path());
        Out() << terraingen::Summary(terrain_) << "\n";
        if (terrain_.grids.empty()) {
            return;
        }
        const terraingen::GeneratedGrid& first = terrain_.grids.front();
        craters_ =
            terraingen::PlacedCraters(terrain_.terrain, terraingen::RasterGrid::Of(first.heights));
        Out() << craters_.craters.Size() << " craters over the first grid\n";
        Show(path_.stem().string(), first.heights,
             first.albedo.empty() ? nullptr : &first.albedo.front(),
             terrain_.geoKeys.IsGeographic());
    }

    void Save(const std::filesystem::path& dir) override {
        for (const auto& written : terraingen::WriteOutputs(config_, terrain_, dir)) {
            Out() << "Wrote " << written.string() << "\n";
        }
    }

    void Test() override {
        if (Args().positional.empty()) {
            Check(terrain_.grids.size() == 1, "example_craters_simple.json makes one grid");
            const terraingen::Raster<float>& heights = terrain_.grids.front().heights;
            Check(heights.Width() == 513 && heights.Height() == 513,
                  "example_craters_simple.json makes 513 x 513 heights");
            Check(craters_.craters.Size() > 100,
                  "example_craters_simple.json makes more than 100 craters");
        }
    }

  private:
    std::filesystem::path path_;
    terraingen::TerrainConfig config_;
    terraingen::GeneratedTerrain terrain_;
    terraingen::CraterFeatures craters_;
};

}  // namespace

TERRAINGEN_EXAMPLE(ConfigLoad, "Generate the terrain a JSON config describes")
