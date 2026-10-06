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
