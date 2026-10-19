#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::PiecewiseUniformSfd;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::UniformBand;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST(PiecewiseUniformSfd, FallsLinearlyAcrossBandsAndHoldsFlatInGaps) {
    const PiecewiseUniformSfd sfd({{0.5, 1.0, 0.5}, {1.5, 3.0, 0.05}, {3.0, 5.0, 0.025}});
    EXPECT_EQ(sfd.Kind(), SfdKind::Density);
    EXPECT_EQ(sfd.ValidRange().min, 0.0);
    EXPECT_EQ(sfd.ValidRange().max, 5.0);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(0.1), 0.575);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(0.5), 0.575);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(0.75), 0.325);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(1.2), 0.075);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(2.25), 0.05);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(4.0), 0.0125);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(5.0), 0.0);
    EXPECT_DOUBLE_EQ(sfd.Incremental(0.75), 1.0);
    EXPECT_DOUBLE_EQ(sfd.Incremental(1.2), 0.0);
    EXPECT_DOUBLE_EQ(sfd.Incremental(2.0), 0.05 / 1.5);
    EXPECT_DOUBLE_EQ(sfd.ExpectedCount(100.0, 0.5, 1.0), 50.0);
    EXPECT_DOUBLE_EQ(sfd.ExpectedCount(100.0, 1.0, 1.5), 0.0);
    EXPECT_TRUE(sfd.AllowsExtrapolation());
    EXPECT_DOUBLE_EQ(sfd.Cumulative(6.0), 0.0);
    EXPECT_DOUBLE_EQ(sfd.ExpectedCount(1.0, 0.5, kInf), 0.575);
}

TEST(PiecewiseUniformSfd, OverlappingBandsAdd) {
    const PiecewiseUniformSfd sfd({{1.0, 3.0, 2.0}, {2.0, 4.0, 1.0}}, SfdKind::ProductionRate);
    EXPECT_EQ(sfd.Kind(), SfdKind::ProductionRate);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(2.5), 0.5 + 0.75);
    EXPECT_DOUBLE_EQ(sfd.Incremental(2.5), 1.0 + 0.5);
    EXPECT_DOUBLE_EQ(sfd.Incremental(3.5), 0.5);
}

TEST(PiecewiseUniformSfd, TotalSharesCountsByWeight) {
    const PiecewiseUniformSfd sfd({{1.0, 2.0, 1.0}, {2.0, 4.0, 3.0}}, SfdKind::Density, 0.2);
    ASSERT_EQ(sfd.Bands().size(), 2u);
    EXPECT_DOUBLE_EQ(sfd.Bands()[0].density, 0.05);
    EXPECT_DOUBLE_EQ(sfd.Bands()[1].density, 0.15);
    EXPECT_DOUBLE_EQ(sfd.Cumulative(1.0), 0.2);
    const PiecewiseUniformSfd asGiven({{1.0, 2.0, 1.0}, {2.0, 4.0, 3.0}});
    EXPECT_DOUBLE_EQ(asGiven.Bands()[1].density, 3.0);
}

TEST(PiecewiseUniformSfd, SamplesUniformlyWithinEachBand) {
    const PiecewiseUniformSfd sfd({{1.0, 2.0, 1.0}, {3.0, 4.0, 3.0}});
    RandomStream stream(4u, 9u);
    constexpr int kSamples = 40000;
    int upper = 0;
    std::vector<int> bins(4, 0);
    for (int i = 0; i < kSamples; ++i) {
        const double d = sfd.SampleDiameter(stream, 1.0, 4.0);
        ASSERT_TRUE((d >= 1.0 && d <= 2.0) || (d >= 3.0 && d <= 4.0)) << d;
        if (d >= 3.0) {
            ++upper;
            ++bins[static_cast<std::size_t>(std::fmin((d - 3.0) * 4.0, 3.0))];
        }
    }
    EXPECT_NEAR(static_cast<double>(upper) / kSamples, 0.75, 0.01);
    for (const int count : bins) {
        EXPECT_NEAR(static_cast<double>(count) / upper, 0.25, 0.015);
    }
    RandomStream inner(4u, 10u);
    for (int i = 0; i < 100; ++i) {
        const double d = sfd.SampleDiameter(inner, 1.25, 1.5);
        EXPECT_GE(d, 1.25);
        EXPECT_LE(d, 1.5);
    }
}

namespace {

/// Exposes a distribution's protected `InvertCumulative`.
class Inverter final : public terraingen::SizeFrequencyDistribution {
  public:
    Inverter() : SizeFrequencyDistribution(SfdKind::Density, terraingen::DiameterRange{1.0, 2.0}) {}

    static double Invert(const SizeFrequencyDistribution& sfd, double target) {
        return UncheckedInverse(sfd, target, 0.0, kInf);
    }

  protected:
    double CumulativeAt(double /*diameter*/) const override {
        return 0.0;
    }
};

}  // namespace

TEST(PiecewiseUniformSfd, InvertsCountsBeyondItsEndsToItsEdges) {
    const PiecewiseUniformSfd sfd({{1.0, 2.0, 1.0}, {3.0, 4.0, 1.0}});
    EXPECT_EQ(Inverter::Invert(sfd, 2.0), 1.0);
    EXPECT_EQ(Inverter::Invert(sfd, 5.0), 1.0);
    EXPECT_EQ(Inverter::Invert(sfd, -1.0), 4.0);
    EXPECT_DOUBLE_EQ(Inverter::Invert(sfd, 1.5), 1.5);
    EXPECT_DOUBLE_EQ(Inverter::Invert(sfd, 0.25), 3.75);
}

TEST(PiecewiseUniformSfd, RejectsInvalidBands) {
    EXPECT_THROW(PiecewiseUniformSfd({}), std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{0.0, 1.0, 1.0}}), std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{2.0, 1.0, 1.0}}), std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{1.0, kInf, 1.0}}), std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{1.0, 2.0, -1.0}}), std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{1.0, 2.0, kNaN}}), std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{1.0, 2.0, 0.0}}), std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{1.0, 2.0, 1.0}}, SfdKind::Density, -1.0),
                 std::invalid_argument);
    EXPECT_THROW(PiecewiseUniformSfd({{1.0, 2.0, 1.0}}, SfdKind::Density, kInf),
                 std::invalid_argument);
    EXPECT_NO_THROW(PiecewiseUniformSfd({{1.0, 2.0, 0.0}, {2.0, 3.0, 1.0}}));
}
