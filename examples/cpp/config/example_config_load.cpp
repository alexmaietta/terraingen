/// Generates the terrain a JSON config describes and writes its outputs, as the terraingen command
/// does: LoadConfig, ValidateConfig, GenerateTerrain, WriteOutputs. The other examples build the
/// same kinds of terrain from library objects instead.
///
///     example_config_load CONFIG [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <exception>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: example_config_load CONFIG [OUTPUT_DIR]\n";
        return 2;
    }
    try {
        const std::filesystem::path path = argv[1];
        const std::filesystem::path output = argc > 2 ? argv[2] : "output";
        std::filesystem::create_directories(output);

        const terraingen::TerrainConfig config = terraingen::LoadConfig(path);
        terraingen::ValidateConfig(config, path.parent_path());
        const terraingen::GeneratedTerrain terrain =
            terraingen::GenerateTerrain(config, path.parent_path());

        std::cout << "Generated " << terrain.heights.Width() << "x" << terrain.heights.Height()
                  << " heights with " << terrain.features.craters.Size() << " craters and "
                  << terrain.features.rocks.size() << " rocks\n";
        for (const auto& written : terraingen::WriteOutputs(config, terrain, output)) {
            std::cout << "Wrote " << written.string() << "\n";
        }
    } catch (const terraingen::ConfigError& error) {
        std::cerr << argv[1] << ": " << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
