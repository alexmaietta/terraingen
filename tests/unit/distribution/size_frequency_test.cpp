#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::DiameterRange;
using terraingen::LogPolynomialSfd;
using terraingen::MinimumSfd;
using terraingen::PiecewisePowerLawSfd;
using terraingen::PowerLawCoefficientFromKilometers;
using terraingen::PowerLawSegment;
using terraingen::PowerLawSfd;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SizeFrequencyDistribution;
using terraingen::SplicedSfd;
using terraingen::TabulatedSfd;

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

TEST(SizeFrequencyDistribution, RefusesDiametersOutsideTheValidRangeUnlessExtrapolating) {
    PowerLawSfd law(1.0, 2.0, SfdKind::Density, DiameterRange{10.0, 300.0});
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
    const PowerLawSfd law(3.0, 2.0);
    EXPECT_DOUBLE_EQ(law.ExpectedCount(100.0, 1.0, 2.0), 100.0 * (3.0 - 0.75));
    EXPECT_DOUBLE_EQ(law.ExpectedCount(5.0, 1.0, 2.0) + law.ExpectedCount(5.0, 2.0, 8.0),
                     law.ExpectedCount(5.0, 1.0, 8.0));
    EXPECT_DOUBLE_EQ(law.ExpectedCount(2.0, 1.0, kInf), 6.0);
    EXPECT_EQ(law.ExpectedCount(0.0, 1.0, 2.0), 0.0);
}

TEST(SizeFrequencyDistribution, ExpectedCountRejectsInvalidArguments) {
    PowerLawSfd bounded(1.0, 2.0, SfdKind::Density, DiameterRange{1.0, 100.0});
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
    const PowerLawSfd law(1.0, 2.0);
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
    EXPECT_THROW(PowerLawSfd(1.0, 2.0).SampleDiameter(stream, 2.0, 1.0), std::invalid_argument);
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
    const std::unique_ptr<SizeFrequencyDistribution> base = std::make_unique<PowerLawSfd>(1.0, 2.0);
    EXPECT_DOUBLE_EQ(base->Cumulative(2.0), 0.25);
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
