// SPDX-License-Identifier: BSD-3-Clause

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

}  // namespace

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
