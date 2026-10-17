/// Where boulders lie: patches of richer and poorer cover, and boulders thrown out by a young
/// crater.
///
/// `BoulderPatches` varies boulder abundance from patch to patch; `BoulderEjecta` adds the blocks a
/// crater excavates when it is large or deep enough to reach bedrock, most at its rim and fading
/// with its age; height ratios, burial and lean shape each boulder, and the largest are placed as
/// 3D boulder models. Prints the abundance across the crater and writes the heights, boulder list
/// and boulders.
///
///     example_boulders_ejecta [--output-dir DIR] [--test]
///     terraingen-examples boulders_ejecta

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

/// One line of the printed table, formatted as by printf.
template <typename... Args> std::string Format(const char* format, Args... args) {
    std::array<char, 256> buffer{};
    std::snprintf(buffer.data(), buffer.size(), format, args...);
    return buffer.data();
}

class BouldersEjecta : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
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

        terraingen::BoulderLayerOptions options;
        options.sizes = {.coverage = 0.03, .minDiameter = 0.2, .maxDiameter = 3.0};
        options.patches = {.spread = 3.0, .size = 15.0};
        options.ejecta.minCraterDiameter = 5.0;
        options.ejecta.coverage = 0.25;
        options.ejecta.reach = 3.0;
        options.pose = {.minHeightRatio = 0.6,
                        .maxHeightRatio = 0.9,
                        .minBurial = 0.0,
                        .maxBurial = 0.1,
                        .maxLean = 0.5};
        options.rendering.relief = terraingen::BoulderRelief::Shape;
        options.rendering.models.minDiameter = 0.8;
        const auto boulders = std::make_shared<terraingen::BoulderLayer>(options, craters);

        Out() << "abundance (background, ejecta) across the crater:\n";
        abundance_.clear();
        for (const double x : {0.0, 10.0, 15.0, 20.0, 30.0}) {
            const terraingen::BoulderAbundance a = boulders->AbundanceAt(x, 0.0);
            abundance_.emplace_back(x, a);
            Out() << Format("  %4.0f m from the centre: %5.2f, %5.2f\n", x, a.background, a.ejecta);
        }

        terraingen::Terrain terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(401, 401, 0.1);
        terrain.layers = {craters, boulders};
        evaluated_ = terraingen::Evaluate(terrain);
        boulders_ =
            terraingen::PlacedBoulders(terrain, terraingen::RasterGrid::FromRaster(evaluated_));
        Out() << Format("%zu boulders, %zu boulder models\n", boulders_.boulders.size(),
                        boulders_.models.size());
        Show("heights", evaluated_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto heights = dir / "example_boulders_ejecta.tif";
        const auto list = dir / "example_boulders_ejecta.csv";
        const auto models = dir / "example_boulders_ejecta.glb";
        terraingen::WriteGeoTiff(heights, evaluated_);
        terraingen::WriteBoulderList(boulders_.boulders, list, terraingen::ListFormat::Csv);
        terraingen::WriteBoulderModels(boulders_.models, models);
        Out() << "Wrote " << heights.string() << ", " << list.string() << " and " << models.string()
              << "\n";
    }

    void Test() override {
        const auto& boulders = boulders_.boulders;
        const auto& models = boulders_.models;
        Check(evaluated_.Width() == 401 && evaluated_.Height() == 401, "the heights are 401 x 401");
        Check(abundance_.size() == 5 && abundance_[4].second.ejecta < 0.1 &&
                  abundance_[3].second.ejecta > abundance_[4].second.ejecta,
              "ejecta are richer at the rim than 30 m out, where they have faded");
        Check(boulders.size() > 500, "more than 500 boulders");
        Check(!models.empty() && models.size() < boulders.size(),
              "some boulders, not all, are boulder models");
        Check(std::all_of(models.begin(), models.end(),
                          [](const terraingen::BoulderModel& b) {
                              return b.boulder.diameter >= 0.8 && b.boulder.diameter <= 3.0;
                          }),
              "every boulder model is 0.8 to 3 m across");
        Check(std::all_of(boulders.begin(), boulders.end(),
                          [](const terraingen::Boulder& r) {
                              return r.burial >= 0.0 && r.burial <= 0.1 && r.lean <= 0.5;
                          }),
              "burial and lean stay within their options");
    }

  private:
    std::vector<std::pair<double, terraingen::BoulderAbundance>> abundance_;
    terraingen::Raster<float> evaluated_;
    terraingen::BoulderFeatures boulders_;
};

}  // namespace

TERRAINGEN_EXAMPLE(BouldersEjecta, "Boulder patches and ejecta around a young crater")
