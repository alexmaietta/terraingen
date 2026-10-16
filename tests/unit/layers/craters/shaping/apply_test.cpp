#include "support/log_capture.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::BlendShape;
using terraingen::Crater;
using terraingen::CraterApplyOptions;
using terraingen::CraterDatum;
using terraingen::CraterList;
using terraingen::CraterOrder;
using terraingen::CraterState;
using terraingen::FassettThomsonProfile;
using terraingen::GeoTransform;
using terraingen::NoDegradation;
using terraingen::Raster;

namespace {

const NoDegradation kFresh(terraingen::LunarFreshCraterMorphometry);

/// `terraingen::ApplyCraters` with every crater a fresh lunar one.
void ApplyCraters(Raster<float>& raster, const CraterList& craters,
                  const terraingen::CraterProfile& profile, const CraterApplyOptions& options = {},
                  double spacing = 0.0, unsigned threads = 1,
                  const terraingen::HeightSampler& ground = {}) {
    const terraingen::CraterShapeModels models{
        std::shared_ptr<const terraingen::CraterProfile>(
            std::shared_ptr<const terraingen::CraterProfile>{}, &profile),
        std::shared_ptr<const terraingen::DegradationModel>(
            std::shared_ptr<const terraingen::DegradationModel>{}, &kFresh)};
    terraingen::ApplyCraters(raster, craters, models, options, {spacing, threads, ground});
}

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
    const CraterState state = kFresh.State(Crater{0.0, 0.0, diameter, 0.0, 0u});
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
    EXPECT_NEAR(At(raster, 0.5, 0.5), -(0.13 - terraingen::kLunarFreshRimHeightRatio) * 40.0, 1e-5);
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

TEST(CraterBlend, WeighsTheInteriorAlongItsFalloff) {
    const terraingen::CraterBlend blend{0.8, 1.2, BlendShape::Linear};
    EXPECT_EQ(blend.Weight(0.5), 1.0);
    EXPECT_NEAR(blend.Weight(0.9), 0.75, 1e-12);
    EXPECT_EQ(blend.Weight(1.3), 0.0);
    EXPECT_EQ(blend.Weight(1.0), terraingen::Falloff(1.0, 0.8, 1.2, BlendShape::Linear));
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

TEST(PrepareCraters, KeepsResolvedCratersInApplyOrderWithStateAndOutline) {
    CraterList craters;
    craters.Append({0.0, 0.0, 10.0, 0.5, 1u});
    craters.Append({5.0, 0.0, 1.0, 0.0, 2u});
    craters.Append({9.0, 0.0, 20.0, 1.0, 3u});
    const auto prepared = terraingen::PrepareCraters(craters, {}, 1.0, kFresh);
    ASSERT_EQ(prepared.size(), 2u) << "the 1 m crater spans a single sample";
    EXPECT_EQ(prepared[0].crater.id, 3u) << "oldest first";
    EXPECT_EQ(prepared[1].crater.id, 1u);
    EXPECT_EQ(prepared[1].resolution, 1.0);
    EXPECT_EQ(prepared[1].state, kFresh.State(craters.At(0)));
    EXPECT_TRUE(prepared[1].outline.Round());
    EXPECT_FALSE(prepared[1].datum.has_value());
}

TEST(ApplyCraterTo, AddsTheProfileAndMovesTowardTheDatumInside) {
    const FassettThomsonProfile profile;
    const Crater crater{0.0, 0.0, 20.0, 0.0, 1u};
    terraingen::PreparedCrater prepared{crater, 0.5, kFresh.State(crater)};
    const double height = profile.Height(3.0, 20.0, prepared.state);
    EXPECT_FLOAT_EQ(terraingen::ApplyCraterTo(2.0f, 3.0, 0.0, prepared, profile, {}),
                    static_cast<float>(2.0 + 0.5 * height));
    EXPECT_EQ(terraingen::ApplyCraterTo(2.0f, 40.0, 0.0, prepared, profile, {}), 2.0f)
        << "beyond the profile's reach";
    prepared.datum = terraingen::Plane{1.0, 0.0, 0.0};
    EXPECT_FLOAT_EQ(terraingen::ApplyCraterTo(2.0f, 3.0, 0.0, prepared, profile, {}),
                    static_cast<float>(2.0 + 0.5 * height + 0.5 * (1.0 - 2.0)));
}

TEST(ApplyCraters, RoundsRimsByTheSmoothedProfileOnlyNearTheRim) {
    const FassettThomsonProfile profile;
    CraterApplyOptions options;
    options.rimSmoothing.sigmaRadii = 0.1;
    Raster<float> sharp = Flat(100);
    ApplyCraters(sharp, One(0.5, 0.5, 40.0), profile);
    Raster<float> rounded = Flat(100);
    ApplyCraters(rounded, One(0.5, 0.5, 40.0), profile, options);
    float sharpCrest = -1e9f;
    float roundedCrest = -1e9f;
    for (std::size_t row = 0; row < rounded.Height(); ++row) {
        for (std::size_t col = 0; col < rounded.Width(); ++col) {
            const auto centre = rounded.CellCenter(col, row);
            const double radii = std::hypot(centre.x - 0.5, centre.y - 0.5) / 20.0;
            sharpCrest = std::max(sharpCrest, sharp(col, row));
            roundedCrest = std::max(roundedCrest, rounded(col, row));
            if (radii < 0.8 || radii > 1.2) {
                ASSERT_EQ(rounded(col, row), sharp(col, row)) << "outside the band";
            }
        }
    }
    EXPECT_LT(roundedCrest, sharpCrest - 0.05f) << "the crest is lowered";
    EXPECT_EQ(options.rimSmoothing.Weight(1.0), 1.0);
    EXPECT_EQ(options.rimSmoothing.Weight(0.8), 0.0);
    EXPECT_EQ(options.rimSmoothing.Weight(1.2), 0.0);
    EXPECT_EQ(terraingen::RimSmoothing{}.Weight(1.0), 0.0) << "off by default";
    options.rimSmoothing.strength = 2.0;
    EXPECT_THROW(terraingen::ValidateCraterApplyOptions(options), std::invalid_argument);
    options.rimSmoothing = {.sigmaRadii = 0.1, .outerRadii = 0.9};
    EXPECT_THROW(terraingen::ValidateRimSmoothing(options.rimSmoothing), std::invalid_argument);
}

TEST(ApplyCraterTo, AddsFootprintNoiseFadingOutBeyondTheRim) {
    const FassettThomsonProfile profile;
    const Crater crater{0.0, 0.0, 20.0, 0.0, 1u};
    const terraingen::PreparedCrater prepared{crater, 1.0, kFresh.State(crater)};
    CraterApplyOptions noisy;
    noisy.noise.amplitude = 0.05;
    double largest = 0.0;
    for (int i = 0; i < 40; ++i) {
        const double x = 0.4 * i - 8.0;
        const float shift = terraingen::ApplyCraterTo(0.0f, x, 3.0, prepared, profile, noisy) -
                            terraingen::ApplyCraterTo(0.0f, x, 3.0, prepared, profile, {});
        largest = std::max(largest, static_cast<double>(std::fabs(shift)));
        ASSERT_LE(std::fabs(shift), 0.05 * 20.0 + 1e-4) << "within the amplitude";
    }
    EXPECT_GT(largest, 0.01) << "inside the rim";
    const double reach = profile.ReachRadii() * 10.0;
    EXPECT_EQ(terraingen::ApplyCraterTo(0.0f, reach - 1e-6, 0.0, prepared, profile, noisy),
              terraingen::ApplyCraterTo(0.0f, reach - 1e-6, 0.0, prepared, profile, {}))
        << "faded out at the reach";
    const Crater other{0.0, 0.0, 20.0, 0.0, 2u};
    const terraingen::PreparedCrater second{other, 1.0, kFresh.State(other)};
    EXPECT_NE(terraingen::ApplyCraterTo(0.0f, 3.0, 2.0, prepared, profile, noisy),
              terraingen::ApplyCraterTo(0.0f, 3.0, 2.0, second, profile, noisy))
        << "keyed to the crater ID";
}

TEST(ApplyCraterTo, AddsRimNoiseOnlyInItsBand) {
    const FassettThomsonProfile profile;
    const Crater crater{0.0, 0.0, 20.0, 0.0, 1u};
    const terraingen::PreparedCrater prepared{crater, 1.0, kFresh.State(crater)};
    CraterApplyOptions noisy;
    noisy.noise.rimAmplitude = 0.05;
    bool changed = false;
    for (int i = 0; i <= 60; ++i) {
        const double radii = 0.025 * i;
        const float plain =
            terraingen::ApplyCraterTo(0.0f, 10.0 * radii, 1.0, prepared, profile, {});
        const float rough =
            terraingen::ApplyCraterTo(0.0f, 10.0 * radii, 1.0, prepared, profile, noisy);
        const double at = std::hypot(10.0 * radii, 1.0) / 10.0;
        if (at <= 0.8 || at >= 1.2) {
            ASSERT_EQ(rough, plain) << "outside the band at " << at;
        }
        changed = changed || rough != plain;
    }
    EXPECT_TRUE(changed);
    noisy.noise.rimInnerRadii = 1.0;
    EXPECT_THROW(terraingen::ValidateCraterNoise(noisy.noise), std::invalid_argument);
    noisy.noise = {.octaves = 13};
    EXPECT_THROW(terraingen::ValidateCraterApplyOptions(noisy), std::invalid_argument);
    noisy.noise = {.wavelengthRadii = 0.0};
    EXPECT_THROW(terraingen::ValidateCraterNoise(noisy.noise), std::invalid_argument);
    noisy.noise = {.amplitude = -1.0};
    EXPECT_THROW(terraingen::ValidateCraterNoise(noisy.noise), std::invalid_argument);
}
