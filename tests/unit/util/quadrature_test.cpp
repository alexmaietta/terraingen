// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/quadrature.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(ForEachLogSimpsonNode, IntegratesPowerLawsAcrossDecades) {
    double sum = 0.0;
    int nodes = 0;
    terraingen::ForEachLogSimpsonNode(0.1, 100.0, 256, [&](double x, double weight) {
        sum += weight * x * x;
        ++nodes;
    });
    EXPECT_EQ(nodes, 257);
    EXPECT_NEAR(sum, (1e6 - 1e-3) / 3.0, 1e-6 * 1e6 / 3.0);
}

TEST(ForEachLogSimpsonNode, StartsAndEndsExactlyAtTheBounds) {
    const double low = 0.1;
    const double high = 5.861381645140288;
    double first = 0.0;
    double last = 0.0;
    terraingen::ForEachLogSimpsonNode(low, high, 16, [&](double x, double) {
        if (first == 0.0) {
            first = x;
        }
        last = x;
        EXPECT_GE(x, low);
        EXPECT_LE(x, high);
    });
    EXPECT_EQ(first, low);
    EXPECT_EQ(last, high);
}
