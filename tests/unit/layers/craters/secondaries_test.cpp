#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <set>
#include <stdexcept>

using terraingen::Crater;
using terraingen::CraterLayer;
using terraingen::CraterLayerOptions;
using terraingen::CraterList;
using terraingen::SecondaryCraterLayer;
using terraingen::SecondaryCraterOptions;
using terraingen::SingerMaxSecondaryDiameter;
using terraingen::Surface;

namespace {

std::shared_ptr<const CraterLayer> Primaries(const Crater& primary, Surface surface = {}) {
    CraterLayerOptions options;
    options.generate = false;
    options.surface = surface;
    options.generation.manual.Append(primary);
    return std::make_shared<CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::FreshAges>(),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
}

terraingen::CraterShapeModels Models() {
    return {
        std::make_shared<terraingen::FassettThomsonProfile>(),
        std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry, 0.11)};
}

SecondaryCraterOptions Options(double onsetCount = 20.0, double minDiameter = 20.0) {
    SecondaryCraterOptions options;
    options.seed = 5;
    options.onsetCount = onsetCount;
    options.minDiameter = minDiameter;
    return options;
}

}  // namespace

TEST(SingerMaxSecondaryDiameter, FollowsTheirGeneralizedFit) {
    const double d = 93.0;
    const double range = 167.4;
    const double expected =
        1000.0 * 0.022 * std::pow(d, 1.96) * std::pow(range, -0.14 * std::log(d));
    EXPECT_NEAR(SingerMaxSecondaryDiameter(93000.0, 167400.0), expected, 1e-9 * expected);
    const double copernicus =
        SingerMaxSecondaryDiameter(93000.0, terraingen::SecondaryOnsetRange(93000.0));
    EXPECT_GT(copernicus, 0.5 * 5500.0);
    EXPECT_LT(copernicus, 1.5 * 5500.0);
    EXPECT_GT(SingerMaxSecondaryDiameter(10000.0, 30000.0),
              SingerMaxSecondaryDiameter(10000.0, 90000.0));
    EXPECT_LE(SingerMaxSecondaryDiameter(800.0, 2000.0), SingerMaxSecondaryDiameter(800.0, 4000.0));
}

TEST(SecondaryOnsetRange, BeginsWhereSingerEtAlSawTheFirstSecondaries) {
    EXPECT_DOUBLE_EQ(terraingen::SecondaryOnsetRange(93000.0), 1.8 * 93000.0);
    EXPECT_DOUBLE_EQ(terraingen::SecondaryOnsetRange(31000.0), 1.8 * 31000.0);
    EXPECT_DOUBLE_EQ(terraingen::SecondaryOnsetRange(2000.0), 2.3 * 2000.0);
    EXPECT_DOUBLE_EQ(terraingen::SecondaryOnsetRange(3000.0), 2.3 * 3000.0);
    const double middle = terraingen::SecondaryOnsetRange(std::sqrt(3000.0 * 31000.0));
    EXPECT_NEAR(middle / std::sqrt(3000.0 * 31000.0), 0.5 + 1.55, 1e-12);
}

TEST(SecondaryCraterLayer, HasTheGivenCountAtTheOnsetAndSingersSizes) {
    const Crater primary{0.0, 0.0, 2000.0, 0.0, 9u, true};
    const SecondaryCraterLayer layer(Primaries(primary), Models(), Options(20.0, 20.0));
    const double onset = terraingen::SecondaryOnsetRange(2000.0);
    const double outer = layer.OuterRange(2000.0);
    ASSERT_GT(outer, onset);
    const CraterList all = layer.Secondaries({-outer, -outer, outer, outer});
    ASSERT_GT(all.Size(), 100u);
    double expected = 0.0;
    const double ringWidth = 400.0;
    const double density0 = 20.0 / (0.25 * std::numbers::pi * 2000.0 * 2000.0);
    for (double r = onset; r < onset + ringWidth; r += 1.0) {
        const double rr = 1.0 + (r - onset) / 2000.0;
        const double largest = SingerMaxSecondaryDiameter(2000.0, r);
        const double share = 1.0 - std::pow(largest / 20.0, -4.0);
        expected += density0 / (rr * rr) * share * 2.0 * std::numbers::pi * r;
    }
    int inRing = 0;
    int atLeast40 = 0;
    std::set<std::uint64_t> ids;
    for (std::size_t n = 0; n < all.Size(); ++n) {
        const Crater crater = all.At(n);
        const double range = std::hypot(crater.x, crater.y);
        EXPECT_GE(range, onset);
        EXPECT_LE(range, outer);
        EXPECT_GE(crater.diameter, 20.0);
        EXPECT_LT(crater.diameter, SingerMaxSecondaryDiameter(2000.0, range));
        EXPECT_EQ(crater.ageGyr, 0.0);
        inRing += range < onset + ringWidth ? 1 : 0;
        atLeast40 += crater.diameter >= 40.0 ? 1 : 0;
        ids.insert(crater.id);
    }
    EXPECT_EQ(ids.size(), all.Size());
    EXPECT_NEAR(inRing, expected, 4.0 * std::sqrt(expected));
    const double ratio = static_cast<double>(all.Size()) / std::max(1, atLeast40);
    EXPECT_GT(ratio, 10.0);
    EXPECT_LT(ratio, 24.0);
    EXPECT_GT(layer.MaxFeatureDiameter(), 20.0);
}

TEST(SecondaryCraterLayer, GridsTilesThreadsAndPointsAgree) {
    for (const Surface& surface : {Surface{}, Surface::Sphere(1737400.0)}) {
        const double unit = surface.UnitLength();
        const Crater primary{0.0, 0.0, 1500.0, 0.1, 4u, true};
        const SecondaryCraterLayer layer(Primaries(primary, surface), Models(), Options(30.0, 6.0));
        const double step = 4.0 / unit;
        terraingen::Raster<float> whole(96, 96, 0.0f);
        whole.SetTransform(terraingen::GeoTransform::NorthUp(3300.0 / unit, 400.0 / unit, step));
        terraingen::Raster<float> threaded = whole;
        layer.Apply(whole, {.spacing = 4.0, .threads = 1});
        layer.Apply(threaded, {.spacing = 4.0, .threads = 0});
        const auto secondaries = layer.Secondaries(terraingen::RasterGrid::Of(whole).Bounds(), 4.0);
        ASSERT_GT(secondaries.Size(), 3u);
        EXPECT_LE(layer.Attributes(secondaries.At(0)).freshness, 1.0);
        int changed = 0;
        for (std::size_t i = 0; i < whole.Size(); ++i) {
            EXPECT_EQ(whole.Values()[i], threaded.Values()[i]);
            changed += whole.Values()[i] != 0.0f ? 1 : 0;
        }
        EXPECT_GT(changed, 50);
        terraingen::Raster<float> tile(32, 32, 0.0f);
        tile.SetTransform(terraingen::GeoTransform::NorthUp(3300.0 / unit + 32.0 * step,
                                                            400.0 / unit - 32.0 * step, step));
        layer.Apply(tile, {.spacing = 4.0, .threads = 1});
        for (std::size_t row = 0; row < 32; ++row) {
            for (std::size_t col = 0; col < 32; ++col) {
                EXPECT_NEAR(tile(col, row), whole(col + 32, row + 32), 1e-5f);
            }
        }
        for (const auto& [col, row] : {std::pair<std::size_t, std::size_t>{5, 5},
                                       std::pair<std::size_t, std::size_t>{48, 60},
                                       std::pair<std::size_t, std::size_t>{90, 30}}) {
            const auto centre = whole.CellCenter(col, row);
            EXPECT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {4.0}), whole(col, row));
        }
        EXPECT_TRUE(std::isnan(layer.ApplyAt(NAN, 0.0, 0.0, {4.0})));
    }
}

TEST(SecondaryCraterLayer, KeepsOnlyEligiblePrimaries) {
    const Crater primary{0.0, 0.0, 2000.0, 0.5, 9u, true};
    const double outer = 20000.0;
    SecondaryCraterOptions options = Options();
    options.selection.maxAgeGyr = 0.4;
    EXPECT_EQ(SecondaryCraterLayer(Primaries(primary), Models(), options)
                  .Secondaries({-outer, -outer, outer, outer})
                  .Size(),
              0u);
    options = Options();
    options.selection.minDiameter = 3000.0;
    EXPECT_EQ(SecondaryCraterLayer(Primaries(primary), Models(), options)
                  .Secondaries({-outer, -outer, outer, outer})
                  .Size(),
              0u);
    options = Options();
    options.selection.manual = false;
    EXPECT_EQ(SecondaryCraterLayer(Primaries(primary), Models(), options)
                  .Secondaries({-outer, -outer, outer, outer})
                  .Size(),
              0u);
    options = Options();
    options.outerRange = 2.0;
    EXPECT_EQ(SecondaryCraterLayer(Primaries(primary), Models(), options)
                  .Secondaries({-outer, -outer, outer, outer})
                  .Size(),
              0u);
    options = Options();
    options.maxExpected = 10.0;
    EXPECT_THROW(SecondaryCraterLayer(Primaries(primary), Models(), options)
                     .Secondaries({-outer, -outer, outer, outer}),
                 std::invalid_argument);
    EXPECT_GT(SecondaryCraterLayer(Primaries(primary), Models(), Options())
                  .Secondaries({-outer, -outer, outer, outer}, 20.0)
                  .Size(),
              0u);
    EXPECT_LT(SecondaryCraterLayer(Primaries(primary), Models(), Options())
                  .Secondaries({-outer, -outer, outer, outer}, 20.0)
                  .Size(),
              SecondaryCraterLayer(Primaries(primary), Models(), Options())
                  .Secondaries({-outer, -outer, outer, outer})
                  .Size());
}

TEST(SecondaryCraterLayer, RejectsInvalidSettings) {
    const auto primaries = Primaries({0.0, 0.0, 2000.0, 0.0, 9u, true});
    const auto rejects = [&](auto change) {
        SecondaryCraterOptions options = Options();
        change(options);
        EXPECT_THROW(SecondaryCraterLayer(primaries, Models(), options), std::invalid_argument);
    };
    rejects([](auto& o) { o.onsetCount.reset(); });
    rejects([](auto& o) { o.onsetCount = 0.0; });
    rejects([](auto& o) { o.countFraction = 0.0; });
    rejects([](auto& o) { o.sizeSlope = -1.0; });
    rejects([](auto& o) { o.minDiameter = 0.0; });
    rejects([](auto& o) { o.maxDiameter = 10.0; });
    rejects([](auto& o) { o.outerRange = -1.0; });
    rejects([](auto& o) { o.selection.maxDiameter = -1.0; });
    rejects([](auto& o) {
        o.selection.minAgeGyr = 2.0;
        o.selection.maxAgeGyr = 1.0;
    });
    rejects([](auto& o) { o.cellSizeFactor = 0.0; });
    EXPECT_THROW(SecondaryCraterLayer(nullptr, Models(), Options()), std::invalid_argument);
    EXPECT_THROW(SecondaryCraterLayer(primaries, {}, Options()), std::invalid_argument);
    terraingen::CraterApplyOptions datum;
    datum.datum = terraingen::CraterDatum::Plane;
    EXPECT_THROW(SecondaryCraterLayer(primaries, Models(), Options(), datum),
                 std::invalid_argument);
    EXPECT_THROW(
        SecondaryCraterLayer(primaries, Models(), Options()).ApplyAt(0.0f, 0.0, 0.0, {0.0}),
        std::invalid_argument);
}

TEST(SingerRangeForSize, InvertsTheLargestSecondaryInRange) {
    for (const double range : {30000.0, 80000.0, 200000.0}) {
        const double largest = terraingen::SingerMaxSecondaryDiameter(20000.0, range);
        EXPECT_NEAR(terraingen::SingerRangeForSize(20000.0, largest), range, 1e-6 * range);
    }
    EXPECT_TRUE(std::isinf(terraingen::SingerRangeForSize(800.0, 5.0)))
        << "below 1 km the largest secondary does not shrink with range";
}

TEST(ElliottMedianRayLength, FollowsTheirPowerLawInRadiusInKilometres) {
    EXPECT_DOUBLE_EQ(terraingen::ElliottMedianRayLength(2000.0), 6590.0);
    EXPECT_DOUBLE_EQ(terraingen::ElliottMedianRayLength(4000.0, 2.0, 1.0), 4000.0);
    const SecondaryCraterLayer layer(Primaries({0.0, 0.0, 2000.0, 0.0, 9u, true}), Models(),
                                     Options());
    EXPECT_DOUBLE_EQ(layer.OuterRange(2000.0), 1.6 * 6590.0);
}
