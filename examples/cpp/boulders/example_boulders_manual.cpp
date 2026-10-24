// SPDX-License-Identifier: BSD-3-Clause

/// Hand-placed boulders on a small flat site, as for a rover test scene: one with its whole pose
/// and shape fixed, one with only its yaw and burial fixed and one given only its position and
/// diameter, among a sparse generated population. Fields left unset are drawn as for generated
/// boulders, and boulders 0.5 m or larger are placed as 3D boulder models. Writes the heights,
/// the boulder list and the models as a glTF scene. The same terrain as
/// examples/config/boulders/example_boulders_manual.json.
///
///     example_boulders_manual [--output-dir DIR] [--test]
///     terraingen-examples boulders_manual

#include "common/example.h"
#include "terraingen/terraingen.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

/// The name of `rest` as the configs write it.
const char* RestName(terraingen::BoulderRest rest) {
    switch (rest) {
    case terraingen::BoulderRest::Side:
        return "side";
    case terraingen::BoulderRest::End:
        return "end";
    default:
        return "face";
    }
}

/// The placed boulder of ID `id`, or none.
std::optional<terraingen::Boulder> Find(const terraingen::BoulderList& boulders, std::uint64_t id) {
    const std::vector<std::uint64_t>& ids = boulders.Ids();
    const auto found = std::find(ids.begin(), ids.end(), id);
    if (found == ids.end()) {
        return std::nullopt;
    }
    return boulders.At(static_cast<std::size_t>(found - ids.begin()));
}

class BouldersManual : public terraingen::examples::Example {
  public:
    using Example::Example;

    void Generate() override {
        manual_ = {
            terraingen::ManualBoulder(-6.0, 4.0, 1.6)
                .Id(101)
                .Height(0.9)
                .Burial(0.3)
                .YawDegrees(30.0)
                .LeanDegrees(5.0)
                .TiltDegrees(15.0)
                .TiltAzimuthDegrees(90.0)
                .Wear(0.2)
                .Rest(terraingen::BoulderRest::Side)
                .ShapeSeed(7u),
            terraingen::ManualBoulder(5.0, 3.0, 1.2).YawDegrees(90.0).Burial(0.5),
            terraingen::ManualBoulder(0.0, -6.0, 1.0),
        };

        terraingen::BoulderLayerOptions options;
        options.sizes.distribution = nullptr;
        options.sizes.coverage = 0.03;
        options.sizes.minDiameter = 0.1;
        options.sizes.maxDiameter = 1.0;
        options.rendering.modelThreshold.minDiameter = 0.5;
        options.manual = manual_;

        terraingen::TerrainGenerator terrain;
        terrain.source = std::make_shared<terraingen::FlatSource>(801, 801, 0.05);
        terrain.layers.push_back(std::make_shared<terraingen::BoulderLayer>(options));

        evaluated_ = terraingen::Evaluate(terrain);
        boulders_ =
            terraingen::PlacedBoulders(terrain, terraingen::RasterGrid::FromRaster(evaluated_));
        Out() << boulders_.boulders.Size() << " boulders, " << boulders_.models.size()
              << " of them boulder models\n";
        for (const terraingen::ManualBoulder& manual : manual_) {
            const std::optional<terraingen::Boulder> b =
                Find(boulders_.boulders, terraingen::ManualBoulderId(manual));
            if (!b) {
                continue;
            }
            Out() << "boulder " << b->id << " at (" << b->x << ", " << b->y << "), " << b->diameter
                  << " m long, " << b->height << " m high, burial " << b->burial << ", yaw "
                  << b->yaw * terraingen::kRadian << " deg, lean " << b->lean * terraingen::kRadian
                  << " deg, tilt " << b->tilt * terraingen::kRadian << " deg toward "
                  << b->tiltAzimuth * terraingen::kRadian << " deg, wear " << b->wear << ", rest "
                  << RestName(b->rest) << ", shape seed "
                  << (b->shapeSeed ? std::to_string(*b->shapeSeed) : std::string("from its ID"))
                  << "\n";
        }
        Show("heights", evaluated_);
    }

    void Save(const std::filesystem::path& dir) override {
        const auto heights = dir / "example_boulders_manual.tif";
        const auto list = dir / "example_boulders_manual.csv";
        const auto models = dir / "example_boulders_manual.glb";
        terraingen::WriteGeoTiff(heights, evaluated_);
        terraingen::WriteBoulderList(boulders_.boulders, list, terraingen::ListFormat::Csv);
        terraingen::WriteBoulderModels(boulders_.models, models);
        Out() << "Wrote " << heights.string() << ", " << list.string() << " and " << models.string()
              << "\n";
    }

    void Test() override {
        Check(evaluated_.Width() == 801 && evaluated_.Height() == 801, "the heights are 801 x 801");
        Check(terraingen::ComputeStats(evaluated_).validCount == evaluated_.Size(),
              "every height is finite");
        const terraingen::ManualBoulder& posed = manual_[0];
        const std::optional<terraingen::Boulder> p = Find(boulders_.boulders, 101);
        Check(p.has_value(), "the posed boulder keeps its ID");
        const double deg = terraingen::kDegree;
        Check(p.has_value() && p->x == posed.x && p->y == posed.y &&
                  p->diameter == posed.diameter && p->height == posed.height &&
                  p->burial == posed.burial && p->yaw == *posed.yawDegrees * deg &&
                  p->lean == *posed.leanDegrees * deg && p->tilt == *posed.tiltDegrees * deg &&
                  p->tiltAzimuth == *posed.tiltAzimuthDegrees * deg && p->wear == posed.wear &&
                  p->rest == terraingen::BoulderRest::Side && p->shapeSeed == posed.shapeSeed,
              "the posed boulder has every field it fixes");
        const terraingen::ManualBoulder& partly = manual_[1];
        const std::optional<terraingen::Boulder> q =
            Find(boulders_.boulders, terraingen::ManualBoulderId(partly));
        Check(q.has_value() && q->yaw == *partly.yawDegrees * deg && q->burial == partly.burial &&
                  q->diameter == partly.diameter && !q->shapeSeed,
              "the partly posed boulder has its yaw and burial");
        const std::optional<terraingen::Boulder> r =
            Find(boulders_.boulders, terraingen::ManualBoulderId(manual_[2]));
        Check(r.has_value() && r->diameter == 1.0 && r->height > 0.0,
              "the boulder given only a diameter is drawn the rest of the way");
        std::size_t manualModels = 0;
        for (const terraingen::BoulderModel& model : boulders_.models) {
            for (const terraingen::ManualBoulder& manual : manual_) {
                if (model.boulder.id == terraingen::ManualBoulderId(manual)) {
                    ++manualModels;
                }
            }
        }
        Check(manualModels == manual_.size(), "every manual boulder is a boulder model");
        Check(boulders_.boulders.Size() > 20 && boulders_.boulders.Size() < 2000,
              "a sparse generated population around them");
    }

  private:
    std::vector<terraingen::ManualBoulder> manual_;
    terraingen::Raster<float> evaluated_;
    terraingen::BoulderFeatures boulders_;
};

}  // namespace

TERRAINGEN_EXAMPLE(BouldersManual, "Hand-placed boulders with a fixed pose among generated ones")
