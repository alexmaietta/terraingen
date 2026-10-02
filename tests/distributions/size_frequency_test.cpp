#include "terraingen/distributions.h"

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
using terraingen::PowerLawCoefficientFromKilometres;
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

TEST(PowerLawSfd, EvaluatesItsClosedForm) {
    const PowerLawSfd law(2.0, 2.5, SfdKind::ProductionRate, DiameterRange{1.0, 100.0});
    EXPECT_EQ(law.Coefficient(), 2.0);
    EXPECT_EQ(law.Exponent(), 2.5);
    EXPECT_EQ(law.Kind(), SfdKind::ProductionRate);
    EXPECT_DOUBLE_EQ(law.Cumulative(4.0), 2.0 * std::pow(4.0, -2.5));
    EXPECT_DOUBLE_EQ(law.Incremental(4.0), 2.0 * 2.5 * std::pow(4.0, -3.5));
}

TEST(PowerLawSfd, RejectsInvalidParameters) {
    EXPECT_THROW(PowerLawSfd(0.0, 2.0), std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, -1.0), std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(kInf, 2.0), std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, 2.0, SfdKind::Density, DiameterRange{-1.0, 10.0}),
                 std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, 2.0, SfdKind::Density, DiameterRange{5.0, 5.0}),
                 std::invalid_argument);
    EXPECT_THROW(PowerLawSfd(1.0, 2.0, SfdKind::Density, DiameterRange{1.0, kNaN}),
                 std::invalid_argument);
}

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

TEST(PiecewisePowerLawSfd, UsesTheLowerSegmentAtAJoinAndAnalyticSlopes) {
    const PiecewisePowerLawSfd law({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
    EXPECT_EQ(law.Segments().size(), 2u);
    EXPECT_DOUBLE_EQ(law.Cumulative(100.0), 0.1 * std::pow(100.0, -2.0));
    EXPECT_DOUBLE_EQ(law.Cumulative(200.0), 10.0 * std::pow(200.0, -3.0));
    EXPECT_DOUBLE_EQ(law.Incremental(50.0), 0.1 * 2.0 * std::pow(50.0, -3.0));
    EXPECT_DOUBLE_EQ(law.Incremental(500.0), 10.0 * 3.0 * std::pow(500.0, -4.0));
}

TEST(PiecewisePowerLawSfd, SamplesWithinLaterSegmentsAnalytically) {
    const PiecewisePowerLawSfd law({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
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

TEST(PiecewisePowerLawSfd, SamplesAnUnboundedLastSegment) {
    const PiecewisePowerLawSfd law({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
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

TEST(PiecewisePowerLawSfd, RejectsMalformedSegments) {
    const auto rejects = [](std::vector<PowerLawSegment> segments,
                            DiameterRange range = DiameterRange{}) {
        EXPECT_THROW(PiecewisePowerLawSfd(segments, SfdKind::Density, range),
                     std::invalid_argument);
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

TEST(PiecewisePowerLawSfd, AllowsDownwardDiscontinuitiesOnlyByOptIn) {
    const std::vector<PowerLawSegment> segments{{0.0, 0.1, 2.0}, {100.0, 1.0, 3.0}};
    EXPECT_NO_THROW(PiecewisePowerLawSfd(segments, SfdKind::Density, DiameterRange{}, true));
    const std::vector<PowerLawSegment> rising{{0.0, 0.1, 2.0}, {100.0, 100.0, 3.0}};
    EXPECT_THROW(PiecewisePowerLawSfd(rising, SfdKind::Density, DiameterRange{}, true),
                 std::invalid_argument);
}

TEST(LogPolynomialSfd, ConvertsUnitsAndMatchesTheEquivalentPowerLaw) {
    const LogPolynomialSfd poly({std::log10(2.0), -2.0}, DiameterRange{1.0, 1e5}, SfdKind::Density,
                                1000.0, 1e6);
    const PowerLawSfd law(2.0, 2.0);
    for (const double d : {1.0, 37.0, 1000.0, 1e5}) {
        EXPECT_NEAR(poly.Cumulative(d), law.Cumulative(d), 1e-12 * law.Cumulative(d)) << d;
        EXPECT_NEAR(poly.Incremental(d), law.Incremental(d), 1e-12 * law.Incremental(d)) << d;
    }
    RandomStream polyStream(4u, 4u);
    RandomStream lawStream(4u, 4u);
    for (int i = 0; i < 50; ++i) {
        const double expected = law.SampleDiameter(lawStream, 3.0, 3000.0);
        EXPECT_NEAR(poly.SampleDiameter(polyStream, 3.0, 3000.0), expected, 1e-9 * expected);
    }
    EXPECT_EQ(poly.Coefficients().size(), 2u);
    EXPECT_THROW(poly.Cumulative(2e5), std::domain_error);
}

TEST(LogPolynomialSfd, RejectsMalformedArgumentsAndIncreasingCurves) {
    const DiameterRange range{1.0, 100.0};
    EXPECT_THROW(LogPolynomialSfd({}, range), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, kNaN}, range), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, range, SfdKind::Density, 0.0),
                 std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, range, SfdKind::Density, 1.0, kInf),
                 std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, DiameterRange{0.0, 100.0}), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, -1.0}, DiameterRange{1.0, kInf}), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, 1.0}, range), std::invalid_argument);
    EXPECT_THROW(LogPolynomialSfd({0.0, 0.0, -1.0}, DiameterRange{0.1, 10.0}),
                 std::invalid_argument);
    EXPECT_NO_THROW(LogPolynomialSfd({0.0, 0.0, -1.0}, DiameterRange{1.0, 10.0}));
    EXPECT_DOUBLE_EQ(LogPolynomialSfd({0.5}, range).Cumulative(10.0), std::pow(10.0, 0.5));
}

TEST(TabulatedSfd, InterpolatesPowerLawsBetweenPoints) {
    const PowerLawSfd law(2.0, 2.0);
    const TabulatedSfd table({1.0, 10.0, 100.0}, {2.0, 0.02, 0.0002});
    for (const double d : {1.0, 3.0, 10.0, 42.0, 100.0}) {
        EXPECT_NEAR(table.Cumulative(d), law.Cumulative(d), 1e-12 * law.Cumulative(d)) << d;
        EXPECT_NEAR(table.Incremental(d), law.Incremental(d), 1e-12 * law.Incremental(d)) << d;
    }
    RandomStream tableStream(2u, 3u);
    RandomStream lawStream(2u, 3u);
    for (int i = 0; i < 50; ++i) {
        const double expected = law.SampleDiameter(lawStream, 1.5, 80.0);
        EXPECT_NEAR(table.SampleDiameter(tableStream, 1.5, 80.0), expected, 1e-9 * expected);
    }
    EXPECT_EQ(table.ValidRange().min, 1.0);
    EXPECT_EQ(table.ValidRange().max, 100.0);
    EXPECT_THROW(table.Cumulative(0.5), std::domain_error);
}

TEST(TabulatedSfd, UsesEachIntervalsSlopeAndConvertsUnits) {
    const TabulatedSfd table({0.001, 0.01, 0.1}, {1e6, 1e5, 1e2}, SfdKind::ProductionRate, 1000.0,
                             1e6);
    EXPECT_EQ(table.Kind(), SfdKind::ProductionRate);
    EXPECT_DOUBLE_EQ(table.Diameters().back(), 100.0);
    EXPECT_DOUBLE_EQ(table.CumulativeValues().front(), 1.0);
    EXPECT_NEAR(table.Cumulative(std::sqrt(10.0)), std::pow(10.0, -0.5), 1e-12);
    EXPECT_NEAR(table.Cumulative(std::sqrt(1000.0)), 1e-1 * std::pow(10.0, -1.5), 1e-12);
    EXPECT_NEAR(table.Incremental(5.0), table.Cumulative(5.0) / 5.0, 1e-12);
    EXPECT_NEAR(table.Incremental(50.0), 3.0 * table.Cumulative(50.0) / 50.0, 1e-12);
}

TEST(TabulatedSfd, FlatIntervalsHoldNoMass) {
    const TabulatedSfd table({1.0, 2.0, 4.0}, {1.0, 1.0, 0.25});
    EXPECT_EQ(table.Incremental(1.5), 0.0);
    RandomStream stream(6u, 6u);
    for (int i = 0; i < 200; ++i) {
        EXPECT_GE(table.SampleDiameter(stream, 1.0, 4.0), 2.0);
    }
}

TEST(TabulatedSfd, ExtrapolatesTheEndSlopesByOptIn) {
    TabulatedSfd table({1.0, 10.0, 100.0}, {1.0, 0.1, 0.0001});
    table.SetAllowExtrapolation(true);
    EXPECT_NEAR(table.Cumulative(0.1), 10.0, 1e-12);
    EXPECT_NEAR(table.Cumulative(1000.0), 1e-7, 1e-18);
    RandomStream stream(3u, 9u);
    for (int i = 0; i < 50; ++i) {
        const double d = table.SampleDiameter(stream, 0.1, 1000.0);
        EXPECT_GE(d, 0.1);
        EXPECT_LE(d, 1000.0);
    }
    const PowerLawSfd steep(1e-1 * 1000.0, 3.0);
    RandomStream tableStream(4u, 4u);
    RandomStream lawStream(4u, 4u);
    for (int i = 0; i < 20; ++i) {
        const double expected = steep.SampleDiameter(lawStream, 200.0, 2000.0);
        EXPECT_NEAR(table.SampleDiameter(tableStream, 200.0, 2000.0), expected, 1e-9 * expected);
    }
}

TEST(TabulatedSfd, RejectsMalformedTables) {
    EXPECT_THROW(TabulatedSfd({1.0}, {1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({2.0, 1.0}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 1.0}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({0.0, 1.0}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, kInf}, {1.0, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0, 0.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {kNaN, 0.5}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {0.5, 1.0}), std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0, 0.5}, SfdKind::Density, 0.0),
                 std::invalid_argument);
    EXPECT_THROW(TabulatedSfd({1.0, 2.0}, {1.0, 0.5}, SfdKind::Density, 1.0, kNaN),
                 std::invalid_argument);
}

namespace {

SplicedSfd TwoPowerLaws() {
    return SplicedSfd({{0.0, std::make_shared<PowerLawSfd>(0.1, 2.0)},
                       {100.0, std::make_shared<PowerLawSfd>(10.0, 3.0)}},
                      DiameterRange{});
}

}  // namespace

TEST(SplicedSfd, MatchesTheEquivalentPiecewisePowerLaw) {
    const SplicedSfd spliced = TwoPowerLaws();
    const PiecewisePowerLawSfd piecewise({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
    EXPECT_EQ(spliced.Pieces().size(), 2u);
    EXPECT_EQ(spliced.Kind(), SfdKind::Density);
    for (const double d : {1.0, 99.0, 100.0, 101.0, 1e4}) {
        EXPECT_NEAR(spliced.Cumulative(d), piecewise.Cumulative(d),
                    1e-12 * piecewise.Cumulative(d));
        EXPECT_NEAR(spliced.Incremental(d), piecewise.Incremental(d),
                    1e-12 * piecewise.Incremental(d));
    }
    for (const auto& [dMin, dMax] : {std::pair{1.0, 1000.0}, std::pair{150.0, kInf},
                                     std::pair{2.0, 50.0}, std::pair{50.0, 100.0}}) {
        RandomStream splicedStream(7u, 7u);
        RandomStream piecewiseStream(7u, 7u);
        for (int i = 0; i < 50; ++i) {
            const double expected = piecewise.SampleDiameter(piecewiseStream, dMin, dMax);
            EXPECT_NEAR(spliced.SampleDiameter(splicedStream, dMin, dMax), expected,
                        1e-9 * expected)
                << dMin << " " << dMax;
        }
    }
}

TEST(SplicedSfd, DiscontinuitiesWithinToleranceBecomeAnAtomAtTheJoin) {
    const SplicedSfd spliced({{0.0, std::make_shared<PowerLawSfd>(1.0, 2.0)},
                              {10.0, std::make_shared<PowerLawSfd>(0.9 * 10.0, 3.0)}},
                             DiameterRange{}, 0.2);
    RandomStream stream(1u, 5u);
    constexpr int kDraws = 20000;
    int atJoin = 0;
    for (int i = 0; i < kDraws; ++i) {
        atJoin += spliced.SampleDiameter(stream, 5.0, 100.0) == 10.0 ? 1 : 0;
    }
    const double expected = kDraws * 0.1 * 0.01 / (0.04 - 0.9 * 10.0 * 1e-6);
    EXPECT_NEAR(atJoin, expected, 5.0 * std::sqrt(expected));
}

TEST(SplicedSfd, RejectsMalformedPieces) {
    const auto low = std::make_shared<PowerLawSfd>(0.1, 2.0);
    const auto high = std::make_shared<PowerLawSfd>(10.0, 3.0);
    const DiameterRange all{};
    EXPECT_THROW(SplicedSfd({}, all), std::invalid_argument);
    EXPECT_THROW(SplicedSfd({{0.0, nullptr}}, all), std::invalid_argument);
    EXPECT_THROW(SplicedSfd({{1.0, low}}, all), std::invalid_argument);
    EXPECT_THROW(SplicedSfd({{0.0, low}, {0.0, high}}, all), std::invalid_argument);
    EXPECT_THROW(SplicedSfd({{0.0, low}, {100.0, high}}, DiameterRange{0.0, 50.0}),
                 std::invalid_argument);
    EXPECT_THROW(SplicedSfd({{0.0, low}, {100.0, nullptr}}, all), std::invalid_argument);
    const auto rate = std::make_shared<PowerLawSfd>(10.0, 3.0, SfdKind::ProductionRate);
    EXPECT_THROW(SplicedSfd({{0.0, low}, {100.0, rate}}, all), std::invalid_argument);
    EXPECT_THROW(SplicedSfd({{0.0, low}, {50.0, high}}, all), std::invalid_argument);
    EXPECT_THROW(SplicedSfd({{0.0, low}, {200.0, high}}, all), std::invalid_argument);
    const auto bounded =
        std::make_shared<PowerLawSfd>(10.0, 3.0, SfdKind::Density, DiameterRange{100.0, 1000.0});
    EXPECT_THROW(SplicedSfd({{0.0, low}, {100.0, bounded}}, all), std::invalid_argument);
    EXPECT_NO_THROW(SplicedSfd({{0.0, low}, {100.0, bounded}}, DiameterRange{0.0, 1000.0}));
    auto extrapolating =
        std::make_shared<PowerLawSfd>(10.0, 3.0, SfdKind::Density, DiameterRange{100.0, 1000.0});
    extrapolating->SetAllowExtrapolation(true);
    EXPECT_NO_THROW(SplicedSfd({{0.0, low}, {100.0, extrapolating}}, all));
}

TEST(MinimumSfd, FollowsTheLowerCurveOnEachSideOfTheCrossover) {
    const MinimumSfd minimum(
        {std::make_shared<PowerLawSfd>(0.1, 2.0), std::make_shared<PowerLawSfd>(10.0, 3.0)});
    const PiecewisePowerLawSfd piecewise({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}});
    EXPECT_EQ(minimum.Distributions().size(), 2u);
    EXPECT_EQ(minimum.Kind(), SfdKind::Density);
    for (const double d : {1.0, 50.0, 100.0, 150.0, 1e4}) {
        EXPECT_NEAR(minimum.Cumulative(d), piecewise.Cumulative(d),
                    1e-12 * piecewise.Cumulative(d));
    }
    EXPECT_NEAR(minimum.Incremental(50.0), piecewise.Incremental(50.0), 1e-15);
    EXPECT_NEAR(minimum.Incremental(500.0), piecewise.Incremental(500.0), 1e-18);
    RandomStream minimumStream(3u, 3u);
    RandomStream piecewiseStream(3u, 3u);
    for (int i = 0; i < 50; ++i) {
        const double expected = piecewise.SampleDiameter(piecewiseStream, 10.0, 1000.0);
        EXPECT_NEAR(minimum.SampleDiameter(minimumStream, 10.0, 1000.0), expected, 1e-9 * expected);
    }
}

TEST(MinimumSfd, IntersectsRangesAndRejectsInvalidInputs) {
    const auto low =
        std::make_shared<PowerLawSfd>(1.0, 2.0, SfdKind::Density, DiameterRange{0.0, 100.0});
    const auto high =
        std::make_shared<PowerLawSfd>(1.0, 3.0, SfdKind::Density, DiameterRange{10.0, kInf});
    const MinimumSfd minimum({low, high});
    EXPECT_EQ(minimum.ValidRange().min, 10.0);
    EXPECT_EQ(minimum.ValidRange().max, 100.0);
    EXPECT_THROW(minimum.Cumulative(5.0), std::domain_error);
    EXPECT_THROW(MinimumSfd({}), std::invalid_argument);
    EXPECT_THROW(MinimumSfd({low, nullptr}), std::invalid_argument);
    EXPECT_THROW(MinimumSfd({nullptr, low}), std::invalid_argument);
    EXPECT_THROW(
        MinimumSfd({low, std::make_shared<PowerLawSfd>(1.0, 2.0, SfdKind::ProductionRate)}),
        std::invalid_argument);
    const auto far =
        std::make_shared<PowerLawSfd>(1.0, 2.0, SfdKind::Density, DiameterRange{200.0, kInf});
    EXPECT_THROW(MinimumSfd({low, far}), std::invalid_argument);
}

TEST(PowerLawSfd, ConvertsKilometreCoefficientsToMetres) {
    EXPECT_DOUBLE_EQ(PowerLawCoefficientFromKilometres(0.1, 2.0), 0.1);
    EXPECT_DOUBLE_EQ(PowerLawCoefficientFromKilometres(1.0, 3.0), 1000.0);
    const PowerLawSfd metres(PowerLawCoefficientFromKilometres(5.0, 1.8), 1.8);
    EXPECT_NEAR(metres.Cumulative(2000.0) * 1e6, 5.0 * std::pow(2.0, -1.8), 1e-12);
}
