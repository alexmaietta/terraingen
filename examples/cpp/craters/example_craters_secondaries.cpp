/// Secondary craters around a young 2 km primary: a `SecondaryCraterLayer` reads the primaries of a
/// `CraterLayer` and throws secondaries whose largest size follows Singer et al. (2020), whose
/// density falls off with range (Chapman 1968), out to about Elliott et al.'s (2018) longest rays.
/// Prints the secondaries by range and writes the heights.
///
///     example_craters_secondaries [--output-dir DIR] [--test]
///     terraingen-examples craters_secondaries

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {

class CratersSecondaries : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        const auto ages = std::make_shared<terraingen::FreshAges>();
        terraingen::CraterLayerOptions primaryOptions;
        primaryOptions.generateRandom = false;
        primaryOptions.generation.manual.Append({0.0, 0.0, 2000.0, 0.05, 0u, true});
        const auto primaries = std::make_shared<terraingen::CraterLayer>(
            terraingen::CraterModels{std::make_shared<terraingen::PowerLawSfd>(1e-6, 2.0), ages,
                                     std::make_shared<terraingen::NoDegradation>(
                                         terraingen::LunarFreshCraterMorphometry),
                                     std::make_shared<terraingen::FassettThomsonProfile>()},
            primaryOptions);

        terraingen::SecondaryCraterOptions options;
        options.seed = 4;
        options.onsetCount = 20.0;
        options.minDiameter = 30.0;
        const auto secondaries = std::make_shared<terraingen::SecondaryCraterLayer>(
            primaries,
            terraingen::CraterShapeModels{std::make_shared<terraingen::FassettThomsonProfile>(),
                                          std::make_shared<terraingen::NoDegradation>(
                                              terraingen::LunarFreshCraterMorphometry, 0.11)},
            options);

        onset_ = terraingen::SecondaryOnsetRange(2000.0);
        outer_ = secondaries->OuterRange(2000.0);
        list_ = secondaries->Secondaries({-outer_, -outer_, outer_, outer_});
        char line[128];
        std::snprintf(line, sizeof(line), "%zu secondaries from %.1f to %.1f km\n", list_.Size(),
                      onset_ / 1000.0, outer_ / 1000.0);
        Out() << line;
        withinSinger_ = true;
        for (double start = onset_; start < outer_; start += 2000.0) {
            int count = 0;
            double largest = 0.0;
            for (std::size_t i = 0; i < list_.Size(); ++i) {
                const double range = std::hypot(list_.X()[i], list_.Y()[i]);
                if (range >= start && range < start + 2000.0) {
                    ++count;
                    largest = std::max(largest, list_.Diameters()[i]);
                }
            }
            const double limit = terraingen::SingerMaxSecondaryDiameter(2000.0, start);
            withinSinger_ = withinSinger_ && largest <= 1.01 * limit;
            std::snprintf(line, sizeof(line),
                          "  %5.1f km: %3d secondaries, largest %5.1f m (Singer limit %5.1f m)\n",
                          start / 1000.0, count, largest, limit);
            Out() << line;
        }

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(1537, 1537, 12.0);
        terrain.layers = {primaries, secondaries};
        heights_ = terraingen::Evaluate(terrain);
        Show("heights", heights_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto path = dir / "example_craters_secondaries.tif";
        terraingen::WriteGeoTiff(path, heights_);
        Out() << "Wrote " << path.string() << "\n";
    }

    void Test() override {
        Check(heights_.Width() == 1537 && heights_.Height() == 1537, "the heights are 1537 x 1537");
        Check(terraingen::ComputeStats(heights_).validCount == heights_.Size(),
              "every height is finite");
        Check(list_.Size() > 10, "more than 10 secondaries");
        bool inRange = true;
        for (std::size_t i = 0; i < list_.Size(); ++i) {
            const double range = std::hypot(list_.X()[i], list_.Y()[i]);
            inRange = inRange && range >= 0.99 * onset_ && range <= 1.01 * outer_ &&
                      list_.Diameters()[i] >= 30.0;
        }
        Check(inRange, "every secondary is 30 m or more, between the onset and outer ranges");
        Check(withinSinger_, "no secondary exceeds Singer's largest size for its range");
    }

  private:
    double onset_ = 0.0;
    double outer_ = 0.0;
    bool withinSinger_ = false;
    terraingen::CraterList list_;
    terraingen::Raster<float> heights_;
};

}  // namespace

TERRAINGEN_EXAMPLE(CratersSecondaries, "Secondary craters around a young primary")
