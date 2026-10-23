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
