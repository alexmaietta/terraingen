// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/analysis/variogram.h"

#include <gtest/gtest.h>

using terraingen::DeviogramFit;
using terraingen::Raster;

TEST(FitDeviogram, FitsALinearRampWithSlopeOne) {
    Raster<float> ramp(16, 16);
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            ramp(col, row) = static_cast<float>(col + row);
        }
    }
    const DeviogramFit fit = terraingen::FitDeviogram(ramp);
    EXPECT_EQ(fit.lags, 4);
    EXPECT_NEAR(fit.slope, 1.0, 1e-12);
    EXPECT_NEAR(fit.intercept, 0.0, 1e-12);
    EXPECT_DOUBLE_EQ(fit.cellRms, 1.0);
}

TEST(FitDeviogram, GivesNothingForAFlatRaster) {
    const DeviogramFit fit = terraingen::FitDeviogram(Raster<float>(8, 8, 3.0f));
    EXPECT_EQ(fit.lags, 0);
    EXPECT_EQ(fit.cellRms, 0.0);
}
