#include "terraingen/distribution/golombek_rapp.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

using terraingen::ExponentialIntegral;
using terraingen::GolombekRappSfd;

namespace {

/// Integral of `f` over [a, b] by Simpson's rule in log x with `steps` intervals.
template <typename F> double IntegrateLog(F f, double a, double b, int steps = 4000) {
    const double la = std::log(a);
    const double h = (std::log(b) - la) / steps;
    double sum = 0.0;
    for (int i = 0; i <= steps; ++i) {
        const double x = std::exp(la + i * h);
        const double weight = (i == 0 || i == steps) ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
        sum += weight * f(x) * x;
    }
    return sum * h / 3.0;
}

}  // namespace

TEST(ExponentialIntegral, MatchesTabulatedValues) {
    EXPECT_NEAR(ExponentialIntegral(1, 1.0), 0.21938393439552029, 1e-15);
    EXPECT_NEAR(ExponentialIntegral(1, 0.1), 1.8229239584193906, 1e-14);
    EXPECT_NEAR(ExponentialIntegral(2, 1.0), 0.14849550677592205, 1e-15);
    EXPECT_NEAR(ExponentialIntegral(2, 0.01), 0.94967053798378715, 1e-14);
    EXPECT_NEAR(ExponentialIntegral(2, 20.0), 9.4048564308581544e-11, 1e-24);
    EXPECT_EQ(ExponentialIntegral(2, 0.0), 1.0);
    EXPECT_DOUBLE_EQ(ExponentialIntegral(0, 2.0), std::exp(-2.0) / 2.0);
    EXPECT_NEAR(ExponentialIntegral(3, 0.5), 0.22160436427517818, 1e-15);
    EXPECT_THROW(ExponentialIntegral(1, 0.0), std::domain_error);
    EXPECT_THROW(ExponentialIntegral(-1, 1.0), std::domain_error);
    EXPECT_THROW(ExponentialIntegral(2, -1.0), std::domain_error);
}

TEST(GolombekRappSfd, FollowsGolombekAndRapp) {
    const GolombekRappSfd rocks(0.02);
    EXPECT_DOUBLE_EQ(rocks.Q(), 1.79 + 0.152 / 0.02);
    EXPECT_DOUBLE_EQ(rocks.FractionalArea(0.0), 0.02);
    EXPECT_DOUBLE_EQ(rocks.FractionalArea(0.5), 0.02 * std::exp(-rocks.Q() * 0.5));
    EXPECT_DOUBLE_EQ(rocks.Incremental(0.3), 0.02 * rocks.Q() * std::exp(-rocks.Q() * 0.3) /
                                                 (std::numbers::pi * 0.09 / 4.0));
    for (const double d : {0.05, 0.2, 1.0, 3.0}) {
        const double integral =
            IntegrateLog([&](double x) { return rocks.Incremental(x); }, d, 60.0);
        EXPECT_NEAR(rocks.Cumulative(d), integral, 1e-6 * rocks.Cumulative(d)) << d;
        const double area = IntegrateLog(
            [&](double x) { return rocks.Incremental(x) * std::numbers::pi * x * x / 4.0; }, d,
            60.0);
        EXPECT_NEAR(area, rocks.FractionalArea(d), 1e-6 * rocks.FractionalArea(d)) << d;
    }
    EXPECT_GT(GolombekRappSfd(0.05).Cumulative(1.0), rocks.Cumulative(1.0))
        << "rockier ground has more rocks of every size";
    EXPECT_GT(GolombekRappSfd(0.05).Incremental(0.1), rocks.Incremental(0.1));
}

TEST(GolombekRappSfd, RejectsInvalidParameters) {
    EXPECT_THROW(GolombekRappSfd(0.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(1.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(0.02, -1.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(0.02, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(0.02, std::nan("")), std::invalid_argument);
}
