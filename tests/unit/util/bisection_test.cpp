// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/bisection.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(Bisect, NarrowsAroundWhereTheConditionTurnsTrue) {
    const terraingen::BisectionInterval found =
        terraingen::Bisect(0.0, 4.0, 100, 1e-12, [](double x) { return x * x >= 2.0; });
    EXPECT_LE(found.high - found.low, 1e-12);
    EXPECT_LT(found.low * found.low, 2.0);
    EXPECT_GE(found.high * found.high, 2.0);
    const terraingen::BisectionInterval once =
        terraingen::Bisect(0.0, 4.0, 1, 0.0, [](double x) { return x >= 1.0; });
    EXPECT_EQ(once.low, 0.0) << "one step halves once";
    EXPECT_EQ(once.high, 2.0);
    const terraingen::BisectionInterval wide =
        terraingen::Bisect(0.0, 4.0, 100, 10.0, [](double x) { return x >= 1.0; });
    EXPECT_EQ(wide.high, 4.0) << "already within the tolerance";
}

TEST(BisectLog, BisectsInLogSpace) {
    const double found =
        terraingen::BisectLog(1e-3, 1e6, 200, 1e-12, [](double x) { return x >= 37.0; });
    EXPECT_NEAR(found, 37.0, 1e-9);
    EXPECT_GE(found, 37.0);
    EXPECT_DOUBLE_EQ(terraingen::BisectLog(1.0, 100.0, 1, 0.0, [](double) { return true; }), 10.0)
        << "the first middle is the geometric mean";
}
