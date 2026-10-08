/// Where rocks lie: patches of richer and poorer cover, and rocks thrown out by a young crater.
///
/// `RockPatches` varies rock abundance from patch to patch; `RockEjecta` adds the blocks a crater
/// excavates when it is large or deep enough to reach bedrock, most at its rim and fading with its
/// age; height ratios, burial and lean shape each rock, and the largest are placed as 3D boulders.
/// Prints the abundance across the crater and writes the heights, rock list and boulders.
///
///     example_boulders_ejecta [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <cstdio>
#include <filesystem>
#include <memory>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    const auto ages = std::make_shared<terraingen::FreshAges>();
    terraingen::CraterLayerOptions craterOptions;
    craterOptions.generate = false;
    craterOptions.generation.manual.Append({0.0, 0.0, 20.0, 0.01, 0u, true});
    const auto craters = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(1e-6, 2.0), ages,
                                 std::make_shared<terraingen::FreshnessDegradation>(
                                     ages, terraingen::LunarFreshCraterMorphometry),
                                 std::make_shared<terraingen::FassettThomsonProfile>()},
        craterOptions);

    terraingen::RockLayerOptions options;
    options.coverage = 0.03;
    options.minDiameter = 0.2;
    options.maxDiameter = 3.0;
    options.patches = {.spread = 3.0, .size = 15.0};
    options.ejecta.minCraterDiameter = 5.0;
    options.ejecta.coverage = 0.25;
    options.ejecta.reach = 3.0;
    options.minHeightRatio = 0.6;
    options.maxHeightRatio = 0.9;
    options.minBurial = 0.0;
    options.maxBurial = 0.1;
    options.maxLean = 0.5;
    options.relief = terraingen::RockRelief::Shape;
    options.models.minDiameter = 0.8;
    const auto rocks = std::make_shared<terraingen::RockLayer>(options, craters);

    std::printf("abundance (background, ejecta) across the crater:\n");
    for (const double x : {0.0, 10.0, 15.0, 20.0, 30.0}) {
        const terraingen::RockAbundance a = rocks->AbundanceAt(x, 0.0);
        std::printf("  %4.0f m from the centre: %5.2f, %5.2f\n", x, a.background, a.ejecta);
    }

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(401, 401, 0.1);
    terrain.layers = {craters, rocks};
    const terraingen::EvaluatedTerrain evaluated = terraingen::Evaluate(terrain);
    std::printf("%zu rocks, %zu boulder models\n", evaluated.features.rocks.size(),
                evaluated.features.boulders.size());
    terraingen::WriteGeoTiff(output / "example_boulders_ejecta.tif", evaluated.heights);
    terraingen::WriteRockList(evaluated.features.rocks, output / "example_boulders_ejecta.csv",
                              terraingen::CraterListFormat::Csv);
    terraingen::WriteBoulderModels(evaluated.features.boulders,
                                   output / "example_boulders_ejecta.glb");
    return 0;
}
