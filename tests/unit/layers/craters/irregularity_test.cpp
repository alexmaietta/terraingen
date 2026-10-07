#include "terraingen/layers/craters/irregularity.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <vector>

using terraingen::Crater;
using terraingen::CraterIrregularity;
using terraingen::OutlineOf;
using terraingen::ValueDistribution;

namespace {

Crater At(std::uint64_t id, double diameter = 100.0, double age = 1.0) {
    return {0.0, 0.0, diameter, age, id};
}

/// The RMS of an outline's stretch about 1 and its mean, over directions.
std::pair<double, double> Stretch(const terraingen::CraterOutline& outline) {
    double sum = 0.0;
    double squares = 0.0;
    constexpr int kSteps = 4096;
    for (int n = 0; n < kSteps; ++n) {
        const double theta = 2.0 * std::numbers::pi * n / kSteps;
        const double s = outline.Scale(std::cos(theta), std::sin(theta));
        sum += s;
        squares += (s - 1.0) * (s - 1.0);
    }
    return {std::sqrt(squares / kSteps), sum / kSteps};
}

}  // namespace

TEST(ValueDistribution, SamplesEachKindWithinItsBounds) {
    const ValueDistribution constant{ValueDistribution::Kind::Constant, 2.0};
    EXPECT_EQ(constant.Sample(0.3, 0.7), 2.0);
    EXPECT_EQ(constant.Lowest(), 2.0);
    EXPECT_EQ(constant.Highest(), 2.0);
    const ValueDistribution uniform{ValueDistribution::Kind::Uniform, 1.0, 3.0};
    EXPECT_EQ(uniform.Sample(0.25, 0.0), 1.5);
    EXPECT_EQ(uniform.Highest(), 3.0);
    const ValueDistribution normal{ValueDistribution::Kind::Normal, 5.0, 1.0, 4.0, 6.0};
    const ValueDistribution logNormal{ValueDistribution::Kind::LogNormal, 2.0, 0.5, 0.0, 10.0};
    double mean = 0.0;
    std::vector<double> logs;
    for (int n = 1; n < 2000; ++n) {
        const double u1 = n / 2000.0;
        const double u2 = std::fmod(n * 0.618034, 1.0);
        const double value = normal.Sample(u1, u2);
        EXPECT_GE(value, 4.0);
        EXPECT_LE(value, 6.0);
        mean += value;
        logs.push_back(std::log(logNormal.Sample(u1, u2)));
    }
    EXPECT_NEAR(mean / 1999.0, 5.0, 0.05);
    std::sort(logs.begin(), logs.end());
    EXPECT_NEAR(std::exp(logs[logs.size() / 2]), 2.0, 0.1) << "the median";
    EXPECT_EQ(normal.Lowest(), 4.0);
    EXPECT_EQ(logNormal.Lowest(), 0.0);
    EXPECT_EQ(logNormal.Highest(), 10.0);
    const std::vector<ValueDistribution> invalid{
        {ValueDistribution::Kind::Uniform, 3.0, 1.0},
        {ValueDistribution::Kind::Normal, 1.0, 1.0},
        {ValueDistribution::Kind::Normal, 1.0, -1.0, 0.0, 2.0},
        {ValueDistribution::Kind::LogNormal, 0.0, 1.0, 0.0, 2.0},
        {ValueDistribution::Kind::Constant, std::nan("")},
        {ValueDistribution::Kind::Constant, 1.0, 0.0, 2.0, 1.0}};
    for (const auto& distribution : invalid) {
        EXPECT_THROW(terraingen::ValidateValueDistribution(distribution), std::invalid_argument);
    }
}

TEST(OutlineOf, ShapesOnlyEligibleCratersAndStaysTheSameForAnID) {
    CraterIrregularity irregularity;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round()) << "off by default";
    irregularity.enabled = true;
    EXPECT_FALSE(OutlineOf(At(1), irregularity).Round());
    EXPECT_EQ(OutlineOf(At(1), irregularity).amplitudes, OutlineOf(At(1), irregularity).amplitudes);
    EXPECT_NE(OutlineOf(At(1), irregularity).phases, OutlineOf(At(2), irregularity).phases);
    irregularity.minDiameter = 200.0;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round());
    irregularity.minDiameter = 0.0;
    irregularity.maxDiameter = 50.0;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round());
    irregularity.maxDiameter = 1000.0;
    irregularity.minAgeGyr = 2.0;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round()) << "too young";
    irregularity.minAgeGyr = 0.0;
    irregularity.maxAgeGyr = 0.5;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round()) << "too old";
    irregularity.maxAgeGyr = 4.0;
    Crater manual = At(1);
    manual.manual = true;
    EXPECT_TRUE(OutlineOf(manual, irregularity).Round());
    irregularity.manual = true;
    EXPECT_FALSE(OutlineOf(manual, irregularity).Round());
    irregularity.seed = 5;
    EXPECT_NE(OutlineOf(At(1), irregularity).phases,
              OutlineOf(manual, CraterIrregularity{}).phases);
}

TEST(OutlineOf, MakesTheShareOfCratersAskedForElliptical) {
    CraterIrregularity irregularity;
    irregularity.enabled = true;
    irregularity.roughShare = 0.0;
    int elongated = 0;
    constexpr int kCount = 20000;
    for (std::uint64_t id = 0; id < kCount; ++id) {
        const auto outline = OutlineOf(At(id), irregularity);
        EXPECT_TRUE(outline.amplitudes.empty());
        if (outline.ellipticity > 1.0) {
            ++elongated;
            EXPECT_GE(outline.ellipticity, 1.1);
            EXPECT_LE(outline.ellipticity, 1.5);
        }
    }
    EXPECT_NEAR(elongated / static_cast<double>(kCount), 0.054, 0.006) << "Bottke et al. 2000";
}

TEST(CraterOutline, StretchesEllipsesAlongTheirHeadingKeepingTheirArea) {
    terraingen::CraterOutline ellipse;
    ellipse.ellipticity = 2.0;
    ellipse.heading = std::numbers::pi / 4.0;
    EXPECT_NEAR(ellipse.Scale(1.0, 1.0), std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(ellipse.Scale(-1.0, 1.0), 1.0 / std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(ellipse.Scale(1.0, 1.0) / ellipse.Scale(-1.0, 1.0), 2.0, 1e-12);
    double area = 0.0;
    constexpr int kSteps = 4096;
    for (int n = 0; n < kSteps; ++n) {
        const double theta = 2.0 * std::numbers::pi * n / kSteps;
        area += 0.5 * std::pow(ellipse.Scale(std::cos(theta), std::sin(theta)), 2.0) * 2.0 *
                std::numbers::pi / kSteps;
    }
    EXPECT_NEAR(area, std::numbers::pi, 1e-9);
    EXPECT_NEAR(ellipse.MaxScale(), std::sqrt(2.0), 1e-12);
    EXPECT_EQ(terraingen::CraterOutline{}.Scale(3.0, 4.0), 1.0);
}

TEST(CraterOutline, HasTheRoughnessAskedForAndStaysWithinItsBounds) {
    CraterIrregularity irregularity;
    irregularity.enabled = true;
    irregularity.elongatedShare = 0.0;
    irregularity.roughness = {ValueDistribution::Kind::Constant, 0.04};
    for (std::uint64_t id = 0; id < 20; ++id) {
        const auto outline = OutlineOf(At(id), irregularity);
        const auto [rms, mean] = Stretch(outline);
        EXPECT_NEAR(rms, 0.04, 1e-6) << id;
        EXPECT_NEAR(mean, 1.0, 1e-9);
        for (int n = 0; n < 360; ++n) {
            const double theta = n * std::numbers::pi / 180.0;
            EXPECT_LE(outline.Scale(std::cos(theta), std::sin(theta)), outline.MaxScale() + 1e-12);
        }
        EXPECT_LE(outline.MaxScale(), terraingen::MaxOutlineScale(irregularity) + 1e-12);
    }
    irregularity.elongatedShare = 1.0;
    EXPECT_NEAR(terraingen::MaxOutlineScale(irregularity),
                std::sqrt(1.5) * (1.0 + 0.04 * std::sqrt(22.0)), 1e-12);
    EXPECT_EQ(terraingen::MaxOutlineScale(CraterIrregularity{}), 1.0);
    irregularity.heading = 0.5;
    irregularity.headingSpread = 0.0;
    EXPECT_EQ(OutlineOf(At(3), irregularity).heading, 0.5);
}

TEST(ValidateCraterIrregularity, RejectsInvalidSettings) {
    EXPECT_NO_THROW(terraingen::ValidateCraterIrregularity({}));
    const std::vector<std::function<void(CraterIrregularity&)>> changes{
        [](CraterIrregularity& c) { c.roughShare = 2.0; },
        [](CraterIrregularity& c) { c.elongatedShare = -0.1; },
        [](CraterIrregularity& c) { c.minDiameter = 10.0, c.maxDiameter = 5.0; },
        [](CraterIrregularity& c) { c.minAgeGyr = -1.0; },
        [](CraterIrregularity& c) { c.minHarmonic = 0; },
        [](CraterIrregularity& c) { c.maxHarmonic = 65; },
        [](CraterIrregularity& c) { c.roughness = {ValueDistribution::Kind::Constant, 0.2}; },
        [](CraterIrregularity& c) { c.roughness = {ValueDistribution::Kind::Constant, -0.01}; },
        [](CraterIrregularity& c) { c.ellipticity = {ValueDistribution::Kind::Constant, 0.9}; },
        [](CraterIrregularity& c) { c.heading = std::nan(""); },
        [](CraterIrregularity& c) { c.headingSpread = -1.0; },
        [](CraterIrregularity& c) {
            c.spectralSlope = {ValueDistribution::Kind::Uniform, 2.0, 1.0};
        }};
    for (const auto& change : changes) {
        CraterIrregularity irregularity;
        change(irregularity);
        EXPECT_THROW(terraingen::ValidateCraterIrregularity(irregularity), std::invalid_argument);
    }
}

TEST(ApplyCraters, StretchesElongatedCratersAndKeepsPointQueriesOnTheGrid) {
    terraingen::CraterLayerOptions options;
    options.generate = false;
    options.generation.manual.Append({0.5, 0.5, 40.0, 0.0, 7u, true});
    options.apply.datum = terraingen::CraterDatum::Plane;
    options.apply.irregularity.enabled = true;
    options.apply.irregularity.manual = true;
    options.apply.irregularity.elongatedShare = 1.0;
    options.apply.irregularity.ellipticity = {ValueDistribution::Kind::Constant, 2.0};
    options.apply.irregularity.heading = 0.0;
    const terraingen::CraterLayer layer(
        {std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
         std::make_shared<terraingen::FreshAges>(),
         std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
         std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
    terraingen::Raster<float> heights(120, 120, 0.0f);
    heights.SetTransform(terraingen::GeoTransform::NorthUp(-60.0, 60.0, 1.0));
    terraingen::TerrainFeatures features;
    const class Flat final : public terraingen::Ground {
      public:
        float HeightAt(double, double, double) const override {
            return 0.0f;
        }
    } flat;
    layer.Apply(heights, {1.0, 2, &flat}, features);
    for (std::size_t row = 0; row < heights.Height(); row += 3) {
        for (std::size_t col = 0; col < heights.Width(); col += 3) {
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {1.0, 1, &flat}), heights(col, row));
        }
    }
    const double along = 20.0 * std::sqrt(2.0);
    const double across = 20.0 / std::sqrt(2.0);
    EXPECT_LT(layer.ApplyAt(0.0f, 0.5 + 0.5 * along, 0.5, {1.0, 1, &flat}), -1.0)
        << "inside the bowl along the long axis";
    EXPECT_GT(layer.ApplyAt(0.0f, 0.5, 0.5 + 1.05 * across, {1.0, 1, &flat}), 0.0)
        << "beyond the rim across it";
}
