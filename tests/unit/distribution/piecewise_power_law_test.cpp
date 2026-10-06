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

}  // namespace

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
