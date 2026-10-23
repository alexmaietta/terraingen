#include "terraingen/distribution/golombek_rapp.h"

#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

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

TEST(GolombekRappSfd, FollowsGolombekAndRapp) {
    const GolombekRappSfd boulders(0.02);
    const double q = 1.79 + 0.152 / 0.02;
    EXPECT_DOUBLE_EQ(boulders.FractionalArea(0.0), 0.02);
    EXPECT_DOUBLE_EQ(boulders.FractionalArea(0.5), 0.02 * std::exp(-q * 0.5));
    EXPECT_DOUBLE_EQ(boulders.Incremental(0.3),
                     0.02 * q * std::exp(-q * 0.3) / (terraingen::kPi * 0.09 / 4.0));
    for (const double d : {0.05, 0.2, 1.0, 3.0}) {
        const double integral =
            IntegrateLog([&](double x) { return boulders.Incremental(x); }, d, 60.0);
        EXPECT_NEAR(boulders.Cumulative(d), integral, 1e-6 * boulders.Cumulative(d)) << d;
        const double area = IntegrateLog(
            [&](double x) { return boulders.Incremental(x) * terraingen::kPi * x * x / 4.0; }, d,
            60.0);
        EXPECT_NEAR(area, boulders.FractionalArea(d), 1e-6 * boulders.FractionalArea(d)) << d;
    }
    EXPECT_GT(GolombekRappSfd(0.05).Cumulative(1.0), boulders.Cumulative(1.0))
        << "rockier ground has more boulders of every size";
    EXPECT_GT(GolombekRappSfd(0.05).Incremental(0.1), boulders.Incremental(0.1));
}

TEST(GolombekRappSfd, RejectsInvalidParameters) {
    EXPECT_THROW(GolombekRappSfd(0.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(1.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(0.02, -1.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(0.02, 0.0, 0.0), std::invalid_argument);
    EXPECT_THROW(GolombekRappSfd(0.02, std::nan("")), std::invalid_argument);
}
