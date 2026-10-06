#include "support/log_capture.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::ApplyCraters;
using terraingen::BlendShape;
using terraingen::Crater;
using terraingen::CraterApplyOptions;
using terraingen::CraterBlend;
using terraingen::CraterBlendWeight;
using terraingen::CraterDatum;
using terraingen::CraterList;
using terraingen::CraterOrder;
using terraingen::CraterState;
using terraingen::FassettThomsonProfile;
using terraingen::GeoTransform;
using terraingen::NoDegradation;
using terraingen::Raster;

namespace {

/// A `size` x `size` north-up raster of 1 m pixels centred on the origin.
Raster<float> Flat(std::size_t size, float value = 0.0f) {
    Raster<float> raster(size, size, value);
    const double half = 0.5 * static_cast<double>(size);
    raster.SetTransform(GeoTransform::NorthUp(-half, half, 1.0));
    return raster;
}

CraterList One(double x, double y, double diameter, double age = 0.0, std::uint64_t id = 1) {
    CraterList craters;
    craters.Append({x, y, diameter, age, id});
    return craters;
}

/// Height at the cell whose centre is nearest (x, y).
float At(const Raster<float>& raster, double x, double y) {
    const auto pixel = raster.Transform().WorldToPixel(x, y);
    return raster(static_cast<std::size_t>(pixel.x), static_cast<std::size_t>(pixel.y));
}

/// Profile height of a fresh crater `diameter` across at `distance` from its centre.
double Fresh(const FassettThomsonProfile& profile, double distance, double diameter) {
    const CraterState state = NoDegradation().State(Crater{0.0, 0.0, diameter, 0.0, 0u});
    return profile.Height(distance, diameter, state);
}

}  // namespace

TEST(ApplyCraters, StampsTheProfileOnFlatTerrain) {
    Raster<float> raster = Flat(100);
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(0.5, 0.5, 40.0), profile);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const auto centre = raster.CellCenter(col, row);
            const double r = std::hypot(centre.x - 0.5, centre.y - 0.5);
            EXPECT_NEAR(raster(col, row), Fresh(profile, r, 40.0), 1e-5) << col << " " << row;
        }
    }
    EXPECT_NEAR(At(raster, 0.5, 0.5), -(0.13 - terraingen::kFreshRimHeightRatio) * 40.0, 1e-5);
}

TEST(ApplyCraters, KeepsTheRegionalSlope) {
    Raster<float> raster = Flat(100);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = static_cast<float>(0.2 * raster.CellCenter(col, row).x + 5.0);
        }
    }
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(10.5, -4.5, 30.0), profile);
    for (const double dx : {0.0, 5.0, -9.0, 20.0}) {
        const double x = 10.5 + dx;
        const double expected = 0.2 * x + 5.0 + Fresh(profile, std::fabs(dx), 30.0);
        EXPECT_NEAR(At(raster, x, -4.5), expected, 1e-4) << dx;
    }
    EXPECT_NEAR(At(raster, -40.5, 40.5), 0.2 * -40.5 + 5.0, 1e-5);
}

TEST(ApplyCraters, ReplacesOlderReliefInsideTheRimAndDrapesEjectaOutside) {
    Raster<float> raster = Flat(80);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = (col + row) % 2 == 0 ? 0.5f : -0.5f;
        }
    }
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(0.0, 0.0, 40.0), profile, {.datum = CraterDatum::Plane});
    const float centre = At(raster, 0.5, 0.5);
    const float neighbour = At(raster, 1.5, 0.5);
    EXPECT_NEAR(centre, neighbour, 0.05);
    const double outside =
        static_cast<double>(At(raster, 26.5, 0.5)) - Fresh(profile, std::hypot(26.5, 0.5), 40.0);
    const double outsideNeighbour =
        static_cast<double>(At(raster, 27.5, 0.5)) - Fresh(profile, std::hypot(27.5, 0.5), 40.0);
    EXPECT_NEAR(std::fabs(outside - outsideNeighbour), 1.0, 1e-5);
}

TEST(ApplyCraters, OrderDecidesWhichCraterOverprints) {
    CraterList craters;
    craters.Append({0.0, 0.0, 50.0, 1.0, 1});
    craters.Append({3.0, 0.0, 10.0, 2.0, 2});
    const FassettThomsonProfile profile;

    Raster<float> big = Flat(100);
    ApplyCraters(big, One(0.0, 0.0, 50.0), profile, {.datum = CraterDatum::Plane});

    Raster<float> oldest = Flat(100);
    ApplyCraters(oldest, craters, profile,
                 {.order = CraterOrder::OldestFirst, .datum = CraterDatum::Plane});
    for (const double x : {0.5, 3.5, 6.5, -8.5, 15.5}) {
        EXPECT_NEAR(At(oldest, x, 0.5), At(big, x, 0.5), 0.02) << x;
    }

    Raster<float> largest = Flat(100);
    ApplyCraters(largest, craters, profile,
                 {.order = CraterOrder::LargestFirst, .datum = CraterDatum::Plane});
    EXPECT_LT(At(largest, 3.5, 0.5), At(big, 3.5, 0.5) - 0.5f);
}

TEST(ApplyCraters, BreaksTiesByAgeThenId) {
    const FassettThomsonProfile profile;
    const auto apply = [&](const CraterList& craters) {
        Raster<float> raster = Flat(60);
        ApplyCraters(raster, craters, profile, {.order = CraterOrder::LargestFirst});
        return raster;
    };
    CraterList byAge;
    byAge.Append({-4.0, 0.0, 20.0, 1.0, 1});
    byAge.Append({4.0, 0.0, 20.0, 2.0, 2});
    CraterList byAgeSwapped;
    byAgeSwapped.Append(byAge.At(1));
    byAgeSwapped.Append(byAge.At(0));
    const Raster<float> older = apply(byAge);
    const Raster<float> olderSwapped = apply(byAgeSwapped);
    EXPECT_TRUE(
        std::equal(older.Values().begin(), older.Values().end(), olderSwapped.Values().begin()));
    CraterList byId;
    byId.Append({-4.0, 0.0, 20.0, 1.0, 5});
    byId.Append({4.0, 0.0, 20.0, 1.0, 3});
    CraterList byIdSwapped;
    byIdSwapped.Append(byId.At(1));
    byIdSwapped.Append(byId.At(0));
    const Raster<float> first = apply(byId);
    const Raster<float> second = apply(byIdSwapped);
    EXPECT_TRUE(std::equal(first.Values().begin(), first.Values().end(), second.Values().begin()));
}

TEST(ApplyCraters, SkipsUnresolvedCratersAndCratersOffTheRaster) {
    Raster<float> raster = Flat(50);
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(0.5, 0.5, 1.5), profile);
    ApplyCraters(raster, One(500.0, 500.0, 20.0), profile);
    ApplyCraters(raster, CraterList(), profile);
    for (const float value : raster.Values()) {
        EXPECT_EQ(value, 0.0f);
    }
    ApplyCraters(raster, One(0.5, 0.5, 1.5), profile, {.fade = {0.5, 1.0}});
    EXPECT_LT(At(raster, 0.5, 0.5), 0.0f);
}

TEST(ApplyCraters, HandlesCratersOverlappingTheEdgeAndNoData) {
    Raster<float> raster = Flat(40);
    raster.SetNoData(-9999.0f);
    raster(20, 20) = -9999.0f;
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(19.5, 0.5, 20.0), profile);
    EXPECT_NEAR(At(raster, 19.5, 0.5), Fresh(profile, 0.0, 20.0), 1e-5);
    ApplyCraters(raster, One(0.5, -0.5, 10.0), profile);
    EXPECT_EQ(raster(20, 20), -9999.0f);
}

TEST(ApplyCraters, RejectsInvalidOptionsAndTransforms) {
    Raster<float> raster = Flat(10);
    const FassettThomsonProfile profile;
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile, {.fade = {-1.0, 2.0}}),
                 std::invalid_argument);
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile, {.fade = {2.0, 2.0}}),
                 std::invalid_argument);
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile,
                              {.fade = {1.0, std::numeric_limits<double>::infinity()}}),
                 std::invalid_argument);
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile, {}, -1.0),
                 std::invalid_argument);
    raster.SetTransform(GeoTransform{0.0, 0.0, 0.0, 0.0, 0.0, 1.0});
    EXPECT_THROW(ApplyCraters(raster, One(0.0, 0.0, 4.0), profile), std::domain_error);
}

TEST(ApplyCraters, LogsAppliedAndSkippedCounts) {
    const terraingen::testing::LogCapture capture;
    Raster<float> raster = Flat(50);
    CraterList craters;
    craters.Append({0.5, 0.5, 10.0, 0.0, 1});
    craters.Append({0.5, 0.5, 1.0, 0.0, 2});
    craters.Append({500.0, 500.0, 10.0, 0.0, 3});
    ApplyCraters(raster, craters, FassettThomsonProfile());
    const auto records = capture.Matching("craters", terraingen::LogLevel::Info);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].message.rfind(
                  "Applied 1 of 3 craters (1 unresolved at 1 m, 1 off the raster) in ", 0),
              0u)
        << records[0].message;
}

namespace {

/// A raster whose cells alternate between +0.5 and -0.5 m, like small-scale relief.
Raster<float> Checkerboard(std::size_t size) {
    Raster<float> raster = Flat(size);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = (col + row) % 2 == 0 ? 0.5f : -0.5f;
        }
    }
    return raster;
}

/// A raster tilted with height 0.2 x + 5.
Raster<float> Slope(std::size_t size) {
    Raster<float> raster = Flat(size);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = static_cast<float>(0.2 * raster.CellCenter(col, row).x + 5.0);
        }
    }
    return raster;
}

}  // namespace

TEST(ApplyCraters, WithoutADatumCratersAreAdditive) {
    Raster<float> raster = Checkerboard(80);
    const Raster<float> before = raster;
    const FassettThomsonProfile profile;
    ApplyCraters(raster, One(0.0, 0.0, 40.0), profile);
    for (const double x : {0.5, 1.5, 10.5, 26.5}) {
        const double expected =
            static_cast<double>(At(before, x, 0.5)) + Fresh(profile, std::hypot(x, 0.5), 40.0);
        EXPECT_NEAR(At(raster, x, 0.5), expected, 1e-5) << x;
    }
}

TEST(ApplyCraters, FlatDatumsLevelTheInteriorAndAPlaneKeepsTheSlope) {
    const FassettThomsonProfile profile;
    const auto interior = [&](CraterDatum datum) {
        Raster<float> raster = Slope(100);
        ApplyCraters(raster, One(10.5, 0.5, 30.0), profile, {.datum = datum});
        return std::pair{At(raster, 4.5, 0.5), At(raster, 16.5, 0.5)};
    };
    const auto [planeLeft, planeRight] = interior(CraterDatum::Plane);
    EXPECT_NEAR(planeRight - planeLeft, 0.2f * 12.0f, 1e-3f);
    const auto [meanLeft, meanRight] = interior(CraterDatum::MeanHeight);
    EXPECT_NEAR(meanRight - meanLeft, 0.0f, 1e-3f);
    const auto [centreLeft, centreRight] = interior(CraterDatum::CentreHeight);
    EXPECT_NEAR(centreLeft, centreRight, 1e-3f);
    EXPECT_NEAR(centreLeft - static_cast<float>(Fresh(profile, 6.0, 30.0)), 0.2f * 10.5f + 5.0f,
                1e-3f);
}

TEST(ApplyCraters, DatumFitRadiusChoosesTheFootprint) {
    const FassettThomsonProfile profile;
    const auto centre = [&](double fitRadii) {
        Raster<float> raster = Flat(100);
        for (std::size_t row = 0; row < raster.Height(); ++row) {
            for (std::size_t col = 0; col < raster.Width(); ++col) {
                const auto p = raster.CellCenter(col, row);
                raster(col, row) = std::hypot(p.x, p.y) > 12.0 ? 4.0f : 0.0f;
            }
        }
        ApplyCraters(raster, One(0.0, 0.0, 20.0), profile,
                     {.datum = CraterDatum::MeanHeight, .datumFitRadii = fitRadii});
        return At(raster, 0.5, 0.5) -
               static_cast<float>(Fresh(profile, std::hypot(0.5, 0.5), 20.0));
    };
    EXPECT_NEAR(centre(1.0), 0.0f, 1e-4f);
    EXPECT_GT(centre(2.0), 1.0f);
}

TEST(ApplyCraters, InteriorReplacementBlendsOldReliefWithTheDatum) {
    const FassettThomsonProfile profile;
    const auto relief = [&](double replacement) {
        Raster<float> raster = Checkerboard(80);
        ApplyCraters(raster, One(0.0, 0.0, 40.0), profile,
                     {.datum = CraterDatum::Plane, .interiorReplacement = replacement});
        return std::fabs(At(raster, 0.5, 0.5) - At(raster, 1.5, 0.5));
    };
    EXPECT_LT(relief(1.0), 0.05f);
    EXPECT_NEAR(relief(0.5), 0.5f, 0.05f);
    EXPECT_NEAR(relief(0.0), 1.0f, 0.05f);
}

TEST(CraterBlendWeight, FallsFromOneToZeroAcrossTheBand) {
    for (const BlendShape shape : {BlendShape::Linear, BlendShape::Smoothstep,
                                   BlendShape::Smootherstep, BlendShape::Gaussian}) {
        const CraterBlend blend{0.8, 1.2, shape};
        EXPECT_EQ(CraterBlendWeight(0.0, blend), 1.0);
        EXPECT_EQ(CraterBlendWeight(0.8, blend), 1.0);
        EXPECT_EQ(CraterBlendWeight(1.2, blend), 0.0);
        EXPECT_EQ(CraterBlendWeight(5.0, blend), 0.0);
        EXPECT_NEAR(CraterBlendWeight(0.8 + 1e-9, blend), 1.0, 1e-6);
        EXPECT_NEAR(CraterBlendWeight(1.2 - 1e-9, blend), 0.0, 1e-6);
        double previous = 1.0;
        for (int i = 1; i < 40; ++i) {
            const double weight = CraterBlendWeight(0.8 + 0.01 * i, blend);
            EXPECT_LE(weight, previous);
            previous = weight;
        }
    }
    EXPECT_DOUBLE_EQ(CraterBlendWeight(1.0, {0.8, 1.2, BlendShape::Linear}), 0.5);
    EXPECT_DOUBLE_EQ(CraterBlendWeight(1.0, {0.8, 1.2, BlendShape::Smoothstep}), 0.5);
    EXPECT_DOUBLE_EQ(CraterBlendWeight(1.0, {0.8, 1.2, BlendShape::Smootherstep}), 0.5);
    const double tail = std::exp(-4.5);
    EXPECT_NEAR(CraterBlendWeight(1.0, {0.8, 1.2, BlendShape::Gaussian}),
                (std::exp(-1.125) - tail) / (1.0 - tail), 1e-12);
    EXPECT_NEAR(CraterBlendWeight(0.9, {0.8, 1.2, BlendShape::Linear}), 0.75, 1e-12);
}

TEST(ApplyCraters, RejectsInvalidDatumAndBlendOptions) {
    Raster<float> raster = Flat(10);
    const FassettThomsonProfile profile;
    const auto apply = [&](const CraterApplyOptions& options) {
        ApplyCraters(raster, One(0.0, 0.0, 4.0), profile, options);
    };
    EXPECT_THROW(apply({.datumFitRadii = 0.0}), std::invalid_argument);
    EXPECT_THROW(apply({.interiorReplacement = 1.5}), std::invalid_argument);
    EXPECT_THROW(apply({.interiorReplacement = -0.1}), std::invalid_argument);
    EXPECT_THROW(apply({.blend = {1.2, 1.2, BlendShape::Linear}}), std::invalid_argument);
    EXPECT_THROW(apply({.blend = {-0.1, 1.2, BlendShape::Linear}}), std::invalid_argument);
}

TEST(ResolutionWeight, FadesFromStartToEndSamples) {
    const terraingen::ResolutionFade fade{};
    EXPECT_EQ(terraingen::ResolutionWeight(1.0, 1.0, fade), 0.0);
    EXPECT_EQ(terraingen::ResolutionWeight(2.0, 1.0, fade), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::ResolutionWeight(3.0, 1.0, fade), 0.5);
    EXPECT_EQ(terraingen::ResolutionWeight(4.0, 1.0, fade), 1.0);
    EXPECT_EQ(terraingen::ResolutionWeight(40.0, 10.0, fade), 1.0);
    double previous = 0.0;
    for (double samples = 2.0; samples <= 4.0; samples += 0.01) {
        const double weight = terraingen::ResolutionWeight(samples, 1.0, fade);
        EXPECT_GE(weight, previous);
        previous = weight;
    }
    const terraingen::ResolutionFade linear{1.0, 3.0, terraingen::BlendShape::Linear};
    EXPECT_DOUBLE_EQ(terraingen::ResolutionWeight(1.5, 1.0, linear), 0.25);
    EXPECT_NO_THROW(terraingen::ValidateResolutionFade({0.0, 0.5}));
    EXPECT_THROW(terraingen::ValidateResolutionFade({3.0, 3.0}), std::invalid_argument);
}

TEST(ApplyCraters, FadesCratersWithTheSpacing) {
    const FassettThomsonProfile profile;
    Raster<float> fine = Flat(50);
    ApplyCraters(fine, One(0.5, 0.5, 9.0), profile, {}, 1.0);
    Raster<float> coarse = Flat(50);
    ApplyCraters(coarse, One(0.5, 0.5, 9.0), profile, {}, 3.0);
    const double weight = terraingen::ResolutionWeight(9.0, 3.0, terraingen::ResolutionFade{});
    ASSERT_GT(weight, 0.0);
    ASSERT_LT(weight, 1.0);
    EXPECT_NEAR(At(coarse, 0.5, 0.5), weight * static_cast<double>(At(fine, 0.5, 0.5)), 1e-6);
    Raster<float> gone = Flat(50);
    ApplyCraters(gone, One(0.5, 0.5, 9.0), profile, {}, 5.0);
    EXPECT_EQ(At(gone, 0.5, 0.5), 0.0f);
}

TEST(ApplyCraters, GivesTheSameBitsOnAnyNumberOfThreads) {
    const terraingen::PowerLawSfd density(0.05, 2.0);
    const terraingen::UniformAges ages(1.0);
    const CraterList craters =
        terraingen::GenerateCraters(terraingen::PlanarCells(), {-50.0, -50.0, 50.0, 50.0}, density,
                                    ages, 4u, {.dMin = 3.0, .dMax = 40.0, .marginRadii = 1.5});
    const FassettThomsonProfile profile;
    for (const terraingen::CraterDatum datum :
         {terraingen::CraterDatum::None, terraingen::CraterDatum::Plane}) {
        Raster<float> serial = Flat(100);
        ApplyCraters(serial, craters, profile, {.datum = datum}, 0.0, 1);
        for (const unsigned threads : {3u, 0u}) {
            Raster<float> parallel = Flat(100);
            ApplyCraters(parallel, craters, profile, {.datum = datum}, 0.0, threads);
            EXPECT_TRUE(std::equal(parallel.Values().begin(), parallel.Values().end(),
                                   serial.Values().begin()));
        }
    }
}

TEST(FitCraterDatum, FitsTheGroundOnALatticeAroundTheCrater) {
    const terraingen::HeightSampler slope = [](double x, double y) {
        return static_cast<float>(10.0 + 0.5 * x - 0.25 * y);
    };
    const terraingen::Crater crater{100.0, -40.0, 20.0, 0.0, 1u};
    terraingen::CraterApplyOptions options;
    options.datum = terraingen::CraterDatum::Plane;
    const auto plane = terraingen::FitCraterDatum(crater, options, slope);
    ASSERT_TRUE(plane.has_value());
    EXPECT_NEAR((*plane)[0], 10.0 + 50.0 + 10.0, 1e-4) << "height under the centre";
    EXPECT_NEAR((*plane)[1], 0.5, 1e-6);
    EXPECT_NEAR((*plane)[2], -0.25, 1e-6);
    options.datum = terraingen::CraterDatum::MeanHeight;
    EXPECT_NEAR((*terraingen::FitCraterDatum(crater, options, slope))[0], 70.0, 1e-4);
    options.datum = terraingen::CraterDatum::CentreHeight;
    EXPECT_FLOAT_EQ(static_cast<float>((*terraingen::FitCraterDatum(crater, options, slope))[0]),
                    70.0f);
    const terraingen::HeightSampler none = [](double, double) { return std::nanf(""); };
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, none).has_value());
    options.datum = terraingen::CraterDatum::Plane;
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, none).has_value());
    options.datum = terraingen::CraterDatum::None;
    EXPECT_FALSE(terraingen::FitCraterDatum(crater, options, slope).has_value());
}

TEST(CraterInfluence, SumsEarlierCratersReachingAPointWhateverElseTheListHolds) {
    const FassettThomsonProfile profile;
    const terraingen::CraterState fresh = terraingen::NoDegradation().State({0, 0, 10, 0, 1});
    std::vector<terraingen::CraterInfluence::Entry> near{{{0.0, 0.0, 10.0, 0.0, 1u}, 1.0, fresh},
                                                         {{3.0, 1.0, 6.0, 0.0, 2u}, 0.5, fresh},
                                                         {{1.0, -2.0, 8.0, 0.0, 3u}, 1.0, fresh}};
    const terraingen::CraterInfluence small(near, profile);
    EXPECT_EQ(small.ReliefBefore(1.0, 0.5, 0), 0.0);
    const double first = small.ReliefBefore(1.0, 0.5, 1);
    EXPECT_DOUBLE_EQ(first, profile.Height(std::hypot(1.0, 0.5), 10.0, fresh));
    const double two = small.ReliefBefore(1.0, 0.5, 2);
    EXPECT_DOUBLE_EQ(two, first + 0.5 * profile.Height(std::hypot(2.0, 0.5), 6.0, fresh));
    std::vector<terraingen::CraterInfluence::Entry> more = near;
    more.insert(more.begin() + 1, {{500.0, 500.0, 40.0, 0.0, 9u}, 1.0, fresh});
    more.push_back({{-300.0, 0.0, 12.0, 0.0, 10u}, 1.0, fresh});
    const terraingen::CraterInfluence large(more, profile);
    EXPECT_EQ(large.ReliefBefore(1.0, 0.5, 4), small.ReliefBefore(1.0, 0.5, 3));
    EXPECT_EQ(large.Entries().size(), 5u);
}

TEST(DatumMargin, CoversTheFitFootprintAndReachOfTheLargestCrater) {
    const FassettThomsonProfile profile;
    EXPECT_EQ(terraingen::DatumMargin({}, profile, 100.0), 0.0);
    terraingen::CraterApplyOptions options;
    options.datum = terraingen::CraterDatum::Plane;
    options.datumFitRadii = 1.5;
    EXPECT_DOUBLE_EQ(terraingen::DatumMargin(options, profile, 100.0),
                     (1.5 + profile.ReachRadii()) * 50.0);
}
