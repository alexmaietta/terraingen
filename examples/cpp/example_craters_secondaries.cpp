/// Secondary craters around a young 2 km primary: a `SecondaryCraterLayer` reads the primaries of a
/// `CraterLayer` and throws secondaries whose largest size follows Singer et al. (2020), whose
/// density falls off with range (Chapman 1968), out to about Elliott et al.'s (2018) longest rays.
/// Prints the secondaries by range and writes the heights.
///
///     example_craters_secondaries [OUTPUT_DIR]

#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "output";
    std::filesystem::create_directories(output);

    const auto ages = std::make_shared<terraingen::FreshAges>();
    terraingen::CraterLayerOptions primaryOptions;
    primaryOptions.generate = false;
    primaryOptions.generation.manual.Append({0.0, 0.0, 2000.0, 0.05, 0u, true});
    const auto primaries = std::make_shared<terraingen::CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(1e-6, 2.0), ages,
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        primaryOptions);

    terraingen::SecondaryCraterOptions options;
    options.seed = 4;
    options.onsetCount = 20.0;
    options.minDiameter = 30.0;
    const auto secondaries = std::make_shared<terraingen::SecondaryCraterLayer>(
        primaries,
        terraingen::SecondaryCraterModels{std::make_shared<terraingen::FassettThomsonProfile>(),
                                          std::make_shared<terraingen::NoDegradation>(
                                              terraingen::LunarFreshCraterMorphometry, 0.11)},
        options);

    const double onset = terraingen::SecondaryOnsetRange(2000.0);
    const double outer = secondaries->OuterRange(2000.0);
    const terraingen::CraterList list = secondaries->Secondaries({-outer, -outer, outer, outer});
    std::printf("%zu secondaries from %.1f to %.1f km\n", list.Size(), onset / 1000.0,
                outer / 1000.0);
    for (double start = onset; start < outer; start += 2000.0) {
        int count = 0;
        double largest = 0.0;
        for (std::size_t i = 0; i < list.Size(); ++i) {
            const double range = std::hypot(list.X()[i], list.Y()[i]);
            if (range >= start && range < start + 2000.0) {
                ++count;
                largest = std::max(largest, list.Diameters()[i]);
            }
        }
        std::printf("  %5.1f km: %3d secondaries, largest %5.1f m (Singer limit %5.1f m)\n",
                    start / 1000.0, count, largest,
                    terraingen::SingerMaxSecondaryDiameter(2000.0, start));
    }

    terraingen::Terrain terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(1537, 1537, 12.0);
    terrain.layers = {primaries, secondaries};
    const auto path = output / "example_craters_secondaries.tif";
    terraingen::WriteGeoTiff(path, terraingen::Evaluate(terrain).heights);
    std::printf("Wrote %s\n", path.string().c_str());
    return 0;
}
