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
        Out() << "Generated " << terrain_.heights.Width() << "x" << terrain_.heights.Height()
              << " heights with " << terrain_.features.craters.Size() << " craters and "
              << terrain_.features.boulders.size() << " boulders\n";
        Show(path_.stem().string(), terrain_.heights,
             terrain_.albedo.empty() ? nullptr : &terrain_.albedo.front(),
             terrain_.geoKeys.IsGeographic());
    }

    void Save(const std::filesystem::path& dir) override {
        for (const auto& written : terraingen::WriteOutputs(config_, terrain_, dir)) {
            Out() << "Wrote " << written.string() << "\n";
        }
    }

    void Test() override {
        Check(!terrain_.heights.Empty(), "the config generates heights");
        if (Args().positional.empty()) {
            Check(terrain_.heights.Width() == 513 && terrain_.heights.Height() == 513,
                  "example_craters_simple.json makes 513 x 513 heights");
            Check(terrain_.features.craters.Size() > 100,
                  "example_craters_simple.json makes more than 100 craters");
        }
    }

  private:
    std::filesystem::path path_;
    terraingen::TerrainConfig config_;
    terraingen::GeneratedTerrain terrain_;
};

}  // namespace

TERRAINGEN_EXAMPLE(ConfigLoad, "Generate the terrain a JSON config describes")
