// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/equilibrium.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(TraskEquilibrium, IsTenToTheMinusOnePointOneOverDiameterSquared) {
    const terraingen::PowerLawSfd trask = terraingen::TraskEquilibrium();
    EXPECT_EQ(trask.Kind(), terraingen::SfdKind::Density);
    EXPECT_DOUBLE_EQ(trask.Cumulative(1.0), std::pow(10.0, -1.1));
    EXPECT_DOUBLE_EQ(trask.Cumulative(10.0), std::pow(10.0, -1.1) / 100.0);
}

TEST(HartmannEquilibrium, ConvertsItsKilometerFitToMeters) {
    const terraingen::PowerLawSfd hartmann = terraingen::HartmannEquilibrium();
    EXPECT_EQ(hartmann.Kind(), terraingen::SfdKind::Density);
    EXPECT_NEAR(hartmann.Cumulative(1000.0) * 1e6, std::pow(10.0, -1.14), 1e-12);
    EXPECT_NEAR(hartmann.Cumulative(2000.0) * 1e6, std::pow(10.0, -1.14) * std::pow(2.0, -1.83),
                1e-12);
}
