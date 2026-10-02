#include "terraingen/models/size_frequency.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::DiameterRange;
using terraingen::PiecewisePowerLaw;
using terraingen::PowerLaw;
using terraingen::PowerLawSegment;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SizeFrequencyDistribution;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

class ExponentialDecay final : public SizeFrequencyDistribution {
  public:
    explicit ExponentialDecay(double scale = 1.0)
        : SizeFrequencyDistribution(SfdKind::ProductionRate, DiameterRange{}), scale_(scale) {}

  protected:
    double CumulativeAt(double diameter) const override {
        return scale_ * std::exp(-diameter / 10.0);
    }

  private:
    double scale_;
};

class Constant final : public SizeFrequencyDistribution {
  public:
    Constant() : SizeFrequencyDistribution(SfdKind::Density, DiameterRange{}) {}

  protected:
    double CumulativeAt(double /*diameter*/) const override {
        return 1.0;
    }
};

}  // namespace

TEST(PowerLaw, EvaluatesItsClosedForm) {
    const PowerLaw law(2.0, 2.5, SfdKind::ProductionRate, DiameterRange{1.0, 100.0});
    EXPECT_EQ(law.Coefficient(), 2.0);
    EXPECT_EQ(law.Exponent(), 2.5);
    EXPECT_EQ(law.Kind(), SfdKind::ProductionRate);
    EXPECT_DOUBLE_EQ(law.Cumulative(4.0), 2.0 * std::pow(4.0, -2.5));
    EXPECT_DOUBLE_EQ(law.Incremental(4.0), 2.0 * 2.5 * std::pow(4.0, -3.5));
}

TEST(PowerLaw, RejectsInvalidParameters) {
    EXPECT_THROW(PowerLaw(0.0, 2.0), std::invalid_argument);
    EXPECT_THROW(PowerLaw(1.0, -1.0), std::invalid_argument);
    EXPECT_THROW(PowerLaw(kInf, 2.0), std::invalid_argument);
    EXPECT_THROW(PowerLaw(1.0, 2.0, SfdKind::Density, DiameterRange{-1.0, 10.0}),
                 std::invalid_argument);
    EXPECT_THROW(PowerLaw(1.0, 2.0, SfdKind::Density, DiameterRange{5.0, 5.0}),
                 std::invalid_argument);
    EXPECT_THROW(PowerLaw(1.0, 2.0, SfdKind::Density, DiameterRange{1.0, kNaN}),
                 std::invalid_argument);
}

TEST(SizeFrequencyDistribution, RefusesDiametersOutsideTheValidRangeUnlessExtrapolating) {
    PowerLaw law(1.0, 2.0, SfdKind::Density, DiameterRange{10.0, 300.0});
    EXPECT_NO_THROW(law.Cumulative(10.0));
    EXPECT_NO_THROW(law.Cumulative(300.0));
    EXPECT_THROW(law.Cumulative(9.0), std::domain_error);
    EXPECT_THROW(law.Incremental(301.0), std::domain_error);
    EXPECT_FALSE(law.AllowsExtrapolation());
    law.SetAllowExtrapolation(true);
    EXPECT_TRUE(law.AllowsExtrapolation());
    EXPECT_DOUBLE_EQ(law.Cumulative(1.0), 1.0);
    for (const double bad : {0.0, -1.0, kNaN, kInf}) {
        EXPECT_THROW(law.Cumulative(bad), std::domain_error) << bad;
    }
}

TEST(SizeFrequencyDistribution, ExpectedCountScalesWithAreaAndAddsAcrossBands) {
    const PowerLaw law(3.0, 2.0);
    EXPECT_DOUBLE_EQ(law.ExpectedCount(100.0, 1.0, 2.0), 100.0 * (3.0 - 0.75));
    EXPECT_DOUBLE_EQ(law.ExpectedCount(5.0, 1.0, 2.0) + law.ExpectedCount(5.0, 2.0, 8.0),
                     law.ExpectedCount(5.0, 1.0, 8.0));
    EXPECT_DOUBLE_EQ(law.ExpectedCount(2.0, 1.0, kInf), 6.0);
    EXPECT_EQ(law.ExpectedCount(0.0, 1.0, 2.0), 0.0);
}

TEST(SizeFrequencyDistribution, ExpectedCountRejectsInvalidArguments) {
    PowerLaw bounded(1.0, 2.0, SfdKind::Density, DiameterRange{1.0, 100.0});
    EXPECT_THROW(bounded.ExpectedCount(1.0, 0.0, 2.0), std::invalid_argument);
    EXPECT_THROW(bounded.ExpectedCount(1.0, 3.0, 2.0), std::invalid_argument);
    EXPECT_THROW(bounded.ExpectedCount(1.0, 1.0, kNaN), std::invalid_argument);
    EXPECT_THROW(bounded.ExpectedCount(-1.0, 1.0, 2.0), std::invalid_argument);
    EXPECT_THROW(bounded.ExpectedCount(kInf, 1.0, 2.0), std::invalid_argument);
    EXPECT_THROW(bounded.ExpectedCount(1.0, 1.0, kInf), std::domain_error);
    bounded.SetAllowExtrapolation(true);
    EXPECT_DOUBLE_EQ(bounded.ExpectedCount(1.0, 1.0, kInf), 1.0);
}

TEST(SizeFrequencyDistribution, SamplesByInvertingTheCumulativeDistribution) {
    const PowerLaw law(1.0, 2.0);
    RandomStream copy(5u, 6u);
    RandomStream stream(5u, 6u);
    for (int i = 0; i < 100; ++i) {
        const double u = copy.NextDouble();
        const double target = 1.0 / 4.0 - u * (1.0 / 4.0 - 1.0 / 400.0);
        const double d = law.SampleDiameter(stream, 2.0, 20.0);
        EXPECT_NEAR(d, std::pow(target, -0.5), 1e-12 * d);
        EXPECT_GE(d, 2.0);
        EXPECT_LE(d, 20.0);
    }
}

TEST(SizeFrequencyDistribution, SamplingRejectsIntervalsWithoutMass) {
    RandomStream stream(1u, 1u);
    EXPECT_THROW(Constant().SampleDiameter(stream, 1.0, 2.0), std::invalid_argument);
    EXPECT_THROW(PowerLaw(1.0, 2.0).SampleDiameter(stream, 2.0, 1.0), std::invalid_argument);
}

TEST(SizeFrequencyDistribution, DefaultIncrementalAndInversionWorkForAnyCurve) {
    const ExponentialDecay decay(7.0);
    EXPECT_NEAR(decay.Incremental(5.0), 0.7 * std::exp(-0.5), 1e-8);
    RandomStream copy(9u, 9u);
    RandomStream stream(9u, 9u);
    for (const double dMax : {30.0, kInf}) {
        for (int i = 0; i < 20; ++i) {
            const double top = 7.0 * std::exp(-0.2);
            const double bottom = std::isinf(dMax) ? 0.0 : 7.0 * std::exp(-dMax / 10.0);
            const double target = top - copy.NextDouble() * (top - bottom);
            const double d = decay.SampleDiameter(stream, 2.0, dMax);
            EXPECT_NEAR(d, -10.0 * std::log(target / 7.0), 1e-9 * d) << dMax;
        }
    }
    const std::unique_ptr<SizeFrequencyDistribution> base = std::make_unique<PowerLaw>(1.0, 2.0);
    EXPECT_DOUBLE_EQ(base->Cumulative(2.0), 0.25);
}

TEST(PiecewisePowerLaw, UsesTheLowerSegmentAtAJoinAndAnalyticSlopes) {
    const PiecewisePowerLaw law({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
    EXPECT_EQ(law.Segments().size(), 2u);
    EXPECT_DOUBLE_EQ(law.Cumulative(100.0), 0.1 * std::pow(100.0, -2.0));
    EXPECT_DOUBLE_EQ(law.Cumulative(200.0), 10.0 * std::pow(200.0, -3.0));
    EXPECT_DOUBLE_EQ(law.Incremental(50.0), 0.1 * 2.0 * std::pow(50.0, -3.0));
    EXPECT_DOUBLE_EQ(law.Incremental(500.0), 10.0 * 3.0 * std::pow(500.0, -4.0));
}

TEST(PiecewisePowerLaw, SamplesWithinLaterSegmentsAnalytically) {
    const PiecewisePowerLaw law({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
    RandomStream copy(2u, 2u);
    RandomStream stream(2u, 2u);
    const double top = law.Cumulative(150.0);
    const double bottom = law.Cumulative(900.0);
    for (int i = 0; i < 50; ++i) {
        const double target = top - copy.NextDouble() * (top - bottom);
        const double d = law.SampleDiameter(stream, 150.0, 900.0);
        EXPECT_NEAR(d, std::pow(target / 10.0, -1.0 / 3.0), 1e-12 * d);
    }
}

TEST(PiecewisePowerLaw, SamplesAnUnboundedLastSegment) {
    const PiecewisePowerLaw law({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
    RandomStream copy(4u, 4u);
    RandomStream stream(4u, 4u);
    const double top = law.Cumulative(50.0);
    for (int i = 0; i < 50; ++i) {
        const double target = top - copy.NextDouble() * top;
        const double d = law.SampleDiameter(stream, 50.0, kInf);
        const double expected = target > law.Cumulative(100.0)
                                    ? std::pow(target / 0.1, -0.5)
                                    : std::pow(target / 10.0, -1.0 / 3.0);
        EXPECT_NEAR(d, expected, 1e-12 * d);
    }
}

TEST(DiameterRange, ContainsOnlyPositiveDiametersWithinItsBounds) {
    const DiameterRange range{10.0, 100.0};
    EXPECT_TRUE(range.Contains(10.0));
    EXPECT_TRUE(range.Contains(100.0));
    EXPECT_FALSE(range.Contains(9.9));
    EXPECT_FALSE(range.Contains(100.1));
    EXPECT_FALSE(DiameterRange{}.Contains(0.0));
    EXPECT_TRUE(DiameterRange{}.Contains(1e9));
}

TEST(PiecewisePowerLaw, RejectsMalformedSegments) {
    const auto rejects = [](std::vector<PowerLawSegment> segments,
                            DiameterRange range = DiameterRange{}) {
        EXPECT_THROW(PiecewisePowerLaw(segments, SfdKind::Density, range), std::invalid_argument);
    };
    rejects({});
    rejects({{1.0, 0.1, 2.0}});
    rejects({{0.0, 0.0, 2.0}});
    rejects({{0.0, 0.1, -2.0}});
    rejects({{0.0, 0.1, 2.0}, {0.0, 0.1, 2.0}});
    rejects({{0.0, 0.1, 2.0}, {kInf, 0.1, 2.0}});
    rejects({{0.0, 0.1, 2.0}, {50.0, 0.1, 2.0}}, DiameterRange{0.0, 40.0});
    rejects({{0.0, 0.1, 2.0}, {100.0, 100.0, 3.0}});
    rejects({{0.0, 0.1, 2.0}, {100.0, 1.0, 3.0}});
}

TEST(PiecewisePowerLaw, AllowsDownwardDiscontinuitiesOnlyByOptIn) {
    const std::vector<PowerLawSegment> segments{{0.0, 0.1, 2.0}, {100.0, 1.0, 3.0}};
    EXPECT_NO_THROW(PiecewisePowerLaw(segments, SfdKind::Density, DiameterRange{}, true));
    const std::vector<PowerLawSegment> rising{{0.0, 0.1, 2.0}, {100.0, 100.0, 3.0}};
    EXPECT_THROW(PiecewisePowerLaw(rising, SfdKind::Density, DiameterRange{}, true),
                 std::invalid_argument);
}
