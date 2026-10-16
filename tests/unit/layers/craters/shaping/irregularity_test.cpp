#include "terraingen/layers/craters/shaping/irregularity.h"

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
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

TEST(CraterProfileDistance, StretchesTheDistanceByTheOutline) {
    const Crater crater = At(1);
    EXPECT_DOUBLE_EQ(terraingen::CraterProfileDistance(crater, {}, 3.0, 4.0), 5.0);
    terraingen::CraterOutline elliptical;
    elliptical.ellipticity = 4.0;
    EXPECT_DOUBLE_EQ(terraingen::CraterProfileDistance(crater, elliptical, 10.0, 0.0),
                     10.0 / elliptical.Scale(10.0, 0.0));
    EXPECT_DOUBLE_EQ(terraingen::CraterProfileDistance(crater, elliptical, 10.0, 0.0), 5.0)
        << "twice as long along the heading";
}

TEST(OutlineOf, ShapesOnlyEligibleCratersAndStaysTheSameForAnID) {
    CraterIrregularity irregularity;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round()) << "off by default";
    irregularity.enabled = true;
    EXPECT_FALSE(OutlineOf(At(1), irregularity).Round());
    EXPECT_EQ(OutlineOf(At(1), irregularity).amplitudes, OutlineOf(At(1), irregularity).amplitudes);
    EXPECT_NE(OutlineOf(At(1), irregularity).phases, OutlineOf(At(2), irregularity).phases);
    terraingen::CraterSelection& selection = irregularity.selection;
    selection.minDiameter = 200.0;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round());
    selection.minDiameter = 0.0;
    selection.maxDiameter = 50.0;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round());
    selection.maxDiameter = 1000.0;
    selection.minAgeGyr = 2.0;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round()) << "too young";
    selection.minAgeGyr = 0.0;
    selection.maxAgeGyr = 0.5;
    EXPECT_TRUE(OutlineOf(At(1), irregularity).Round()) << "too old";
    selection.maxAgeGyr = 4.0;
    Crater manual = At(1);
    manual.manual = true;
    EXPECT_TRUE(OutlineOf(manual, irregularity).Round());
    selection.manual = true;
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

TEST(OutlineOf, FollowsDuEtAlsFittedRimSpectra) {
    CraterIrregularity irregularity;
    irregularity.enabled = true;
    irregularity.elongatedShare = 0.0;
    irregularity.model = terraingen::RimRoughnessModel::Du2024;
    irregularity.scatter = false;
    const auto outline = OutlineOf(At(9, 10000.0), irregularity);
    ASSERT_EQ(outline.table.size(), 1024u);
    EXPECT_TRUE(outline.amplitudes.empty());
    const auto amplitude = [&](int k) {
        std::complex<double> sum = 0.0;
        for (std::size_t n = 0; n < outline.table.size(); ++n) {
            sum += outline.table[n] *
                   std::polar(1.0, -2.0 * std::numbers::pi * k * static_cast<double>(n) / 1024.0);
        }
        return 2.0 * std::abs(sum) / 1024.0;
    };
    const auto expected = [](double logPower) {
        return std::sqrt(std::pow(10.0, logPower) * 2.0 * std::numbers::pi) / 5000.0;
    };
    EXPECT_NEAR(amplitude(1), expected(0.066 * 10.0 - 1.1), 1e-12) << "degree-1 power";
    EXPECT_NEAR(amplitude(2), expected(0.055 * 10.0 + 2.3), 1e-12) << "degree-2 power";
    const double logWavelength2 = 0.010 * 10.0 - 0.57;
    const double logPower2 = 0.043 * 10.0 + 0.59;
    const double logPi = std::log10(std::numbers::pi);
    const double at10 = std::log10(2.0 * std::numbers::pi / 10.0);
    EXPECT_NEAR(amplitude(10),
                expected(logPower2 + (at10 - logWavelength2) / (logPi - logWavelength2) *
                                         (0.055 * 10.0 + 2.3 - logPower2)),
                1e-12);
    const double at200 = std::log10(2.0 * std::numbers::pi / 200.0);
    EXPECT_NEAR(amplitude(200),
                expected(logPower2 + (-0.022 * 10.0 + 3.3) * (at200 - logWavelength2)), 1e-12);
    EXPECT_NEAR(outline.Scale(1.0, 0.0), 1.0 + outline.table[0], 1e-12);
    EXPECT_NEAR(outline.Scale(std::cos(2.0 * std::numbers::pi * 5.0 / 1024.0),
                              std::sin(2.0 * std::numbers::pi * 5.0 / 1024.0)),
                1.0 + outline.table[5], 1e-9);
    EXPECT_NEAR(terraingen::MaxOutlineScale(irregularity), 1.3, 1e-12);

    irregularity.scatter = true;
    irregularity.maxDeviation = 0.01;
    for (std::uint64_t id = 0; id < 50; ++id) {
        const auto capped = OutlineOf(At(id, 60000.0), irregularity);
        for (const double deviation : capped.table) {
            ASSERT_LE(std::fabs(deviation), 0.01 + 1e-12);
        }
        EXPECT_LE(capped.MaxScale(), 1.01 + 1e-12);
    }
    EXPECT_NE(OutlineOf(At(1, 60000.0), irregularity).table,
              OutlineOf(At(2, 60000.0), irregularity).table);
}

TEST(ValidateCraterIrregularity, RejectsInvalidSettings) {
    EXPECT_NO_THROW(terraingen::ValidateCraterIrregularity({}));
    const std::vector<std::function<void(CraterIrregularity&)>> changes{
        [](CraterIrregularity& c) { c.roughShare = 2.0; },
        [](CraterIrregularity& c) { c.elongatedShare = -0.1; },
        [](CraterIrregularity& c) {
            c.selection.minDiameter = 10.0;
            c.selection.maxDiameter = 5.0;
        },
        [](CraterIrregularity& c) { c.selection.minAgeGyr = -1.0; },
        [](CraterIrregularity& c) { c.minHarmonic = 0; },
        [](CraterIrregularity& c) { c.maxHarmonic = 65; },
        [](CraterIrregularity& c) { c.roughness = {ValueDistribution::Kind::Constant, 0.2}; },
        [](CraterIrregularity& c) { c.roughness = {ValueDistribution::Kind::Constant, -0.01}; },
        [](CraterIrregularity& c) { c.ellipticity = {ValueDistribution::Kind::Constant, 0.9}; },
        [](CraterIrregularity& c) { c.heading = std::nan(""); },
        [](CraterIrregularity& c) { c.headingSpread = -1.0; },
        [](CraterIrregularity& c) { c.maxDeviation = 0.0; },
        [](CraterIrregularity& c) { c.maxDeviation = 0.6; },
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
    options.apply.irregularity.selection.manual = true;
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
