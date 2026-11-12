// SPDX-License-Identifier: BSD-3-Clause

#include "support/golombek_rapp.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;
using terraingen::Bounds2d;
using terraingen::Layer;
using terraingen::LayerContext;
using terraingen::Vec2d;

namespace {

/// A ramp boulders roll down, which records every point it is read at.
class RecordedRamp final : public terraingen::Ground {
  public:
    /// The points read so far.
    std::vector<Vec2d> Reads() const {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_reads;
    }

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double /*spacing*/,
                     unsigned /*threads*/) const override {
        const std::lock_guard<std::mutex> lock(m_mutex);
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const Vec2d p = samples.Position(i);
            m_reads.push_back(p);
            samples.Height(i) = static_cast<float>(0.8 * std::clamp(p.x, -40.0, 40.0));
        }
    }

  private:
    mutable std::mutex m_mutex;
    mutable std::vector<Vec2d> m_reads;
};

/// How far (m) the read farthest from `box` lies outside it.
double FarthestOutside(const std::vector<Vec2d>& reads, const Bounds2d& box) {
    double farthest = 0.0;
    for (const Vec2d& p : reads) {
        const double dx = std::fmax(0.0, std::fmax(box.minX - p.x, p.x - box.maxX));
        const double dy = std::fmax(0.0, std::fmax(box.minY - p.y, p.y - box.maxY));
        farthest = std::fmax(farthest, std::hypot(dx, dy));
    }
    return farthest;
}

/// Checks that evaluating `layer` over a grid and at a point reads the ground within its
/// `GroundReach` of the samples, and that it reads the ground at all.
void ExpectReadsWithinReach(const Layer& layer, const std::string& name) {
    constexpr double kSpacing = 1.0;
    const double exact = layer.GroundReach(kSpacing);
    EXPECT_GT(exact, 0.0) << name;
    const double reach = exact * (1.0 + 1e-12);

    const RecordedRamp ground;
    LayerContext context = ground.Context(kSpacing, 1);
    terraingen::Raster<float> grid(
        terraingen::RasterGrid{32, 32, terraingen::GeoTransform::NorthUp(-16.0, 16.0, kSpacing)},
        0.0f);
    layer.Apply(grid, context);
    const std::vector<Vec2d> reads = ground.Reads();
    EXPECT_FALSE(reads.empty()) << name;
    EXPECT_LE(FarthestOutside(reads, {-15.5, -15.5, 15.5, 15.5}), reach) << name << " on a grid";

    const RecordedRamp pointGround;
    context = pointGround.Context(kSpacing, 1);
    layer.ApplyAt(0.0f, 3.0, -2.0, context);
    EXPECT_LE(FarthestOutside(pointGround.Reads(), {3.0, -2.0, 3.0, -2.0}), reach)
        << name << " at a point";
}

/// Boulders that roll, break and gather on bedrock.
std::shared_ptr<const BoulderLayer> RollingBoulders() {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.08;
    options.sizes.minDiameter = 1.0;
    options.sizes.maxDiameter = 4.0;
    options.pose.maxSlopeDegrees = std::atan(0.5) * terraingen::kRadian;
    options.pose.rollDistance = 12.0;
    options.bedrock.exposure = terraingen::ExposureParams{};
    options.fragmentation = terraingen::BoulderFragmentation{};
    return std::make_shared<BoulderLayer>(options);
}

}  // namespace

TEST(GroundReach, IsZeroForLayersThatReadNoGroundAround) {
    const terraingen::StrataLayer strata(terraingen::StrataOptions{});
    EXPECT_EQ(strata.GroundReach(1.0), 0.0);
    EXPECT_EQ(terraingen::PavementLayer(terraingen::PavementOptions{}).GroundReach(1.0), 0.0);
    EXPECT_EQ(terraingen::BedformLayer(terraingen::BedformOptions{}).GroundReach(1.0), 0.0);
}

TEST(GroundReach, BoundsWhereSandFillAndExposureReadersReadTheGround) {
    terraingen::SandFillOptions sand;
    sand.reach = 7.0;
    const terraingen::SandFillLayer fill(sand);
    EXPECT_EQ(fill.GroundReach(1.0), 7.0);
    ExpectReadsWithinReach(fill, "sand fill");

    terraingen::PavementOptions pavement;
    pavement.plateSize = 4.0;
    pavement.exposure = terraingen::ExposureParams{};
    ExpectReadsWithinReach(terraingen::PavementLayer(pavement), "pavement");

    terraingen::BedformOptions bedforms;
    bedforms.exposure = terraingen::ExposureParams{};
    ExpectReadsWithinReach(terraingen::BedformLayer(bedforms), "bedforms");
}

TEST(GroundReach, BoundsWhereCraterDatumsReadTheGround) {
    terraingen::CraterLayerOptions options;
    options.seed = 5;
    options.generation.minDiameter = 6.0;
    options.generation.maxDiameter = 30.0;
    options.apply.datum = terraingen::CraterDatum::Plane;
    options.apply.irregularity.enabled = true;
    const terraingen::CraterModels models{
        std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
        std::make_shared<terraingen::UniformAges>(1.0),
        std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
        std::make_shared<terraingen::FassettThomsonProfile>()};
    const terraingen::CraterLayer craters(models, options);
    ExpectReadsWithinReach(craters, "craters with a datum");
    options.apply.datum = terraingen::CraterDatum::None;
    EXPECT_EQ(terraingen::CraterLayer(models, options).GroundReach(1.0), 0.0)
        << "craters without a datum or replacement read no ground";
}

TEST(GroundReach, BoundsWhereBouldersAndTheirShadowsReadTheGround) {
    const std::shared_ptr<const BoulderLayer> boulders = RollingBoulders();
    ExpectReadsWithinReach(*boulders, "boulders");
    const terraingen::SandShadowLayer shadows(boulders, terraingen::SandShadowOptions{});
    EXPECT_GT(shadows.GroundReach(1.0), boulders->GroundReach(1.0));
    ExpectReadsWithinReach(shadows, "sand shadows");
}

TEST(GroundReach, MaskedLayersReachAsTheirInnerLayer) {
    const auto fill = std::make_shared<terraingen::SandFillLayer>(terraingen::SandFillOptions{});
    const terraingen::MaskedLayer masked(
        fill, terraingen::RegionShape::Rectangle({-8.0, -8.0, 8.0, 8.0}), 2.0);
    EXPECT_EQ(masked.GroundReach(0.5), fill->GroundReach(0.5));
}
