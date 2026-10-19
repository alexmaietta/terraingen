#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_map>

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
        const auto secondaries =
            layer.Secondaries(terraingen::RasterGrid::FromRaster(whole).Bounds(), 4.0);
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

TEST(SingerMaxRange, InvertsTheLargestSecondaryInRange) {
    for (const double range : {30000.0, 80000.0, 200000.0}) {
        const double largest = terraingen::SingerMaxSecondaryDiameter(20000.0, range);
        EXPECT_NEAR(terraingen::SingerMaxRange(20000.0, largest), range, 1e-6 * range);
    }
    EXPECT_TRUE(std::isinf(terraingen::SingerMaxRange(800.0, 5.0)))
        << "below 1 km the largest secondary does not shrink with range";
}

TEST(ElliottMedianRayLength, FollowsTheirPowerLawInRadiusInKilometres) {
    EXPECT_DOUBLE_EQ(terraingen::ElliottMedianRayLength(2000.0), 6590.0);
    EXPECT_DOUBLE_EQ(terraingen::ElliottMedianRayLength(4000.0, 2.0, 1.0), 4000.0);
    const SecondaryCraterLayer layer(Primaries({0.0, 0.0, 2000.0, 0.0, 9u, true}), Models(),
                                     Options());
    EXPECT_DOUBLE_EQ(layer.OuterRange(2000.0), 1.6 * 6590.0);
}

TEST(RayDirections, AreDrawnFromTheCraterAndTheLayout) {
    const Crater crater{0.0, 0.0, 2000.0, 0.0, 9u, true};
    const terraingen::RayLayout layout;
    const auto rays = terraingen::RayDirections(crater, layout);
    ASSERT_GE(rays.size(), 6u);
    ASSERT_LE(rays.size(), 20u);
    for (const auto& ray : rays) {
        EXPECT_GE(ray.azimuth, 0.0);
        EXPECT_LT(ray.azimuth, 2.0 * std::numbers::pi);
        EXPECT_GT(ray.sinHalfWidth, std::sin(0.5 * 5.4 * std::numbers::pi / 180.0) - 1e-12);
        EXPECT_LT(ray.sinHalfWidth, std::sin(0.5 * 38.6 * std::numbers::pi / 180.0) + 1e-12);
    }
    const auto again = terraingen::RayDirections(crater, layout);
    ASSERT_EQ(again.size(), rays.size());
    EXPECT_EQ(again[0].azimuth, rays[0].azimuth);
    terraingen::RayLayout other = layout;
    other.seed = 1;
    EXPECT_NE(terraingen::RayDirections(crater, other)[0].azimuth, rays[0].azimuth);
    other.count = {terraingen::ValueDistribution::Kind::Constant, 65.0};
    EXPECT_THROW(terraingen::ValidateRayLayout(other), std::invalid_argument);
    other = layout;
    other.widthDegrees = {terraingen::ValueDistribution::Kind::Constant, 180.0};
    EXPECT_THROW(terraingen::ValidateRayLayout(other), std::invalid_argument);
}

TEST(SecondaryCraterLayer, RaysGatherSecondariesWithoutAddingAny) {
    const Crater primary{0.0, 0.0, 2000.0, 0.0, 9u, true};
    SecondaryCraterOptions even = Options(60.0, 20.0);
    SecondaryCraterOptions rayed = even;
    rayed.rays.enabled = true;
    rayed.rays.share = 1.0;
    const SecondaryCraterLayer evenLayer(Primaries(primary), Models(), even);
    const SecondaryCraterLayer rayLayer(Primaries(primary), Models(), rayed);
    const double outer = evenLayer.OuterRange(2000.0);
    const terraingen::Bounds2d all{-outer, -outer, outer, outer};
    const CraterList spread = evenLayer.Secondaries(all);
    const CraterList gathered = rayLayer.Secondaries(all);
    ASSERT_GT(spread.Size(), 400u);
    const double n = static_cast<double>(spread.Size());
    EXPECT_NEAR(static_cast<double>(gathered.Size()), n, 0.15 * n) << "moved, not added";

    const auto rays = terraingen::RayDirections(primary, rayed.rays.layout);
    const auto nearRay = [&](const CraterList& craters) {
        int near = 0;
        for (std::size_t i = 0; i < craters.Size(); ++i) {
            const Crater c = craters.At(i);
            const double range = std::hypot(c.x, c.y);
            const double azimuth = std::atan2(c.y, c.x);
            for (const auto& ray : rays) {
                const double delta = std::remainder(azimuth - ray.azimuth, 2.0 * std::numbers::pi);
                const double width = std::fmax(2000.0 * ray.sinHalfWidth,
                                               range * std::sin(2.0 * std::numbers::pi / 180.0));
                if (std::cos(delta) > 0.0 && range * std::sin(std::fabs(delta)) < width) {
                    ++near;
                    break;
                }
            }
        }
        return static_cast<double>(near) / static_cast<double>(craters.Size());
    };
    EXPECT_GT(nearRay(gathered), 0.7);
    EXPECT_LT(nearRay(spread), 0.5 * nearRay(gathered));
}

TEST(SecondaryCraterLayer, ElongatesSecondariesTowardTheirPrimary) {
    const Crater primary{0.0, 0.0, 2000.0, 0.0, 9u, true};
    SecondaryCraterOptions options = Options(60.0, 20.0);
    options.shapes.elongatedShare = 1.0;
    options.shapes.ellipticity = {terraingen::ValueDistribution::Kind::Constant, 1.6};
    const SecondaryCraterLayer layer(Primaries(primary), Models(), options);
    const double outer = layer.OuterRange(2000.0);
    const double onset = terraingen::SecondaryOnsetRange(2000.0);
    std::unordered_map<std::uint64_t, terraingen::SecondaryOutline> outlines;
    const CraterList all = layer.Secondaries({-outer, -outer, outer, outer}, 0.0, outlines);
    ASSERT_GT(all.Size(), 100u);
    ASSERT_EQ(outlines.size(), all.Size());
    for (std::size_t i = 0; i < all.Size(); ++i) {
        const Crater c = all.At(i);
        const auto& outline = outlines.at(c.id);
        const double range = std::hypot(c.x, c.y);
        EXPECT_NEAR(outline.ellipticity, 1.0 + 0.6 * (outer - range) / (outer - onset), 1e-9);
        EXPECT_NEAR(std::sin(outline.heading - std::atan2(c.y, c.x)), 0.0, 1e-9)
            << "the long axis points back to the primary";
    }
    EXPECT_DOUBLE_EQ(terraingen::SecondaryStretch(options.shapes), std::sqrt(1.6));
    EXPECT_DOUBLE_EQ(terraingen::SecondaryStretch({}), 1.0);
}

TEST(SecondaryCraterLayer, RaysAndShapesKeepGridsTilesAndPointsInStep) {
    const Crater primary{0.0, 0.0, 1500.0, 0.1, 4u, true};
    SecondaryCraterOptions options = Options(60.0, 6.0);
    options.rays.enabled = true;
    options.shapes.elongatedShare = 0.8;
    const SecondaryCraterLayer layer(Primaries(primary), Models(), options);
    terraingen::Raster<float> whole(96, 96, 0.0f);
    whole.SetTransform(terraingen::GeoTransform::NorthUp(3300.0, 400.0, 4.0));
    layer.Apply(whole, {.spacing = 4.0, .threads = 0});
    int changed = 0;
    for (const float v : whole.Values()) {
        changed += v != 0.0f ? 1 : 0;
    }
    EXPECT_GT(changed, 20);
    terraingen::Raster<float> tile(32, 32, 0.0f);
    tile.SetTransform(terraingen::GeoTransform::NorthUp(3300.0 + 128.0, 400.0 - 128.0, 4.0));
    layer.Apply(tile, {.spacing = 4.0, .threads = 1});
    for (std::size_t row = 0; row < 32; ++row) {
        for (std::size_t col = 0; col < 32; ++col) {
            ASSERT_NEAR(tile(col, row), whole(col + 32, row + 32), 1e-5f);
            const auto centre = tile.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {4.0}), tile(col, row));
        }
    }
}

TEST(SecondaryCraterLayer, RejectsInvalidRaysAndShapes) {
    const auto rejects = [](auto change) {
        SecondaryCraterOptions options = Options();
        change(options);
        EXPECT_THROW(terraingen::ValidateSecondaryCraterOptions(options), std::invalid_argument);
    };
    rejects([](SecondaryCraterOptions& o) { o.rays.share = 1.5; });
    rejects([](SecondaryCraterOptions& o) { o.rays.clustering = -0.1; });
    rejects([](SecondaryCraterOptions& o) { o.rays.minHalfAngleDegrees = 0.0; });
    rejects([](SecondaryCraterOptions& o) { o.rays.clusterSpacing = 0.0; });
    rejects([](SecondaryCraterOptions& o) {
        o.rays.layout.count = {terraingen::ValueDistribution::Kind::Constant, -1.0};
    });
    rejects([](SecondaryCraterOptions& o) { o.shapes.elongatedShare = 2.0; });
    rejects([](SecondaryCraterOptions& o) {
        o.shapes.ellipticity = {terraingen::ValueDistribution::Kind::Constant, 0.9};
    });
    rejects([](SecondaryCraterOptions& o) {
        o.shapes.ellipticity = {terraingen::ValueDistribution::Kind::LogNormal, 1.2, 0.3};
    });
}

TEST(SecondaryCraterLayer, PrimariesWithoutRaysSpreadTheirSecondaries) {
    const Crater primary{0.0, 0.0, 2000.0, 0.0, 9u, true};
    SecondaryCraterOptions options = Options(30.0, 20.0);
    const SecondaryCraterLayer even(Primaries(primary), Models(), options);
    options.rays.enabled = true;
    options.rays.layout.count = {terraingen::ValueDistribution::Kind::Constant, 0.0};
    const SecondaryCraterLayer none(Primaries(primary), Models(), options);
    const double outer = even.OuterRange(2000.0);
    const terraingen::Bounds2d all{-outer, -outer, outer, outer};
    const CraterList a = even.Secondaries(all);
    const CraterList b = none.Secondaries(all);
    ASSERT_EQ(a.Size(), b.Size());
    for (std::size_t i = 0; i < a.Size(); ++i) {
        EXPECT_EQ(a.At(i), b.At(i));
    }
}

TEST(SecondaryDensity, MatchesTheSecondariesDrawn) {
    const Crater primary{0.0, 0.0, 2000.0, 0.0, 9u, true};
    for (const bool rayed : {false, true}) {
        SecondaryCraterOptions options = Options(60.0, 10.0);
        options.rays.enabled = rayed;
        const SecondaryCraterLayer layer(Primaries(primary), Models(), options);
        const terraingen::SecondaryDensity density(primary, options);
        const double onset = terraingen::SecondaryOnsetRange(2000.0);
        EXPECT_DOUBLE_EQ(density.Reach(), layer.OuterRange(2000.0));
        const terraingen::Bounds2d box{onset, -1500.0, onset + 3000.0, 1500.0};
        const CraterList drawn = layer.Secondaries(box);
        double counted = 0.0;
        for (std::size_t n = 0; n < drawn.Size(); ++n) {
            const Crater crater = drawn.At(n);
            counted += box.Contains(crater.x, crater.y) && crater.diameter < 15.0 ? 1.0 : 0.0;
        }
        double expected = 0.0;
        for (double x = box.minX + 5.0; x < box.maxX; x += 10.0) {
            for (double y = box.minY + 5.0; y < box.maxY; y += 10.0) {
                expected += 100.0 * density.At({x, y}, 10.0, 15.0);
            }
        }
        ASSERT_GT(expected, 100.0);
        EXPECT_NEAR(counted, expected, 4.0 * std::sqrt(expected)) << "rays " << rayed;
    }
    SecondaryCraterOptions larger = Options();
    larger.selection.minDiameter = 5000.0;
    EXPECT_EQ(terraingen::SecondaryDensity(primary, larger).Reach(), 0.0)
        << "a primary its selection leaves out throws none";
}

TEST(SecondaryDensity, CoversAsItsCountsAddUp) {
    const Crater primary{0.0, 0.0, 3000.0, 0.0, 9u, true};
    for (const double slope : {2.0, 3.5}) {
        SecondaryCraterOptions options = Options(25.0, 5.0);
        options.sizeSlope = slope;
        const terraingen::SecondaryDensity density(primary, options);
        const terraingen::Vec2d at{8000.0, 1000.0};
        double cover = 0.0;
        for (double d = 10.0; d < 30.0; d += 0.01) {
            cover +=
                0.25 * std::numbers::pi * (d + 0.005) * (d + 0.005) * density.At(at, d, d + 0.01);
        }
        EXPECT_NEAR(density.CoverAt(at, 10.0, 30.0), cover, 1e-6 * cover) << slope;
        EXPECT_GT(density.Largest(), 30.0);
        EXPECT_EQ(density.At({100.0, 0.0}, 10.0, 30.0), 0.0) << "inside the onset";
    }
}

TEST(CraterLayer, TakesTheSecondariesItThrowsOutOfATotalMap) {
    const Crater primary{0.0, 0.0, 2000.0, 0.0, 9u, true};
    const SecondaryCraterOptions secondaries = Options(100.0, 10.0);
    const double total = 2.0 * 0.05 * (1.0 / 100.0 - 1.0 / 1600.0);
    CraterLayerOptions options;
    options.seed = 4;
    options.generation.dMin = 10.0;
    options.generation.dMax = 40.0;
    options.generation.manual.Append(primary);
    options.densityMap = terraingen::DensityMap{std::make_shared<terraingen::ConstantField>(total),
                                                terraingen::DensityMeasure::Count, 10.0, 40.0};
    options.secondaries = {secondaries};
    const auto primaries = std::make_shared<CraterLayer>(
        terraingen::CraterModels{
            std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
            std::make_shared<terraingen::FreshAges>(),
            std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
            std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
    const SecondaryCraterLayer layer(primaries, Models(), secondaries);
    const terraingen::SecondaryDensity density(primary, secondaries);
    const double onset = terraingen::SecondaryOnsetRange(2000.0);
    const auto count = [](const CraterList& craters, const terraingen::Bounds2d& box) {
        double n = 0.0;
        for (std::size_t i = 0; i < craters.Size(); ++i) {
            const Crater crater = craters.At(i);
            n += !crater.manual && box.Contains(crater.x, crater.y) && crater.diameter < 40.0 ? 1.0
                                                                                              : 0.0;
        }
        return n;
    };
    const terraingen::Bounds2d near{onset, -1000.0, onset + 2000.0, 1000.0};
    double thrown = 0.0;
    for (double x = near.minX + 5.0; x < near.maxX; x += 10.0) {
        for (double y = near.minY + 5.0; y < near.maxY; y += 10.0) {
            thrown += 100.0 * density.At({x, y}, 10.0, 40.0);
        }
    }
    const double area = 2000.0 * 2000.0;
    ASSERT_GT(thrown, 0.2 * total * area) << "the secondaries are a good share of the map";
    const double generated = count(primaries->Craters(near), near);
    const double sum = generated + count(layer.Secondaries(near), near);
    EXPECT_NEAR(generated, total * area - thrown, 4.0 * std::sqrt(total * area));
    EXPECT_NEAR(sum, total * area, 4.0 * std::sqrt(total * area)) << "the map, not more";
    const terraingen::Bounds2d far{-30000.0, -1000.0, -28000.0, 1000.0};
    EXPECT_NEAR(count(primaries->Craters(far), far), total * area, 4.0 * std::sqrt(total * area))
        << "beyond the secondaries";
    options.generation.maxCraters = 10;
    EXPECT_THROW(CraterLayer(primaries->Models(), options), std::invalid_argument);
}
