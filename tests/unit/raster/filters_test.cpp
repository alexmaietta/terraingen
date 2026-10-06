#include "terraingen/raster/filters.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::GaussianBlur;
using terraingen::Raster;

TEST(GaussianBlur, KeepsConstantsIncludingAtTheEdges) {
    const Raster<float> flat(20, 15, 3.0f);
    const Raster<float> blurred = GaussianBlur(flat, 2.0);
    for (const float value : blurred.Values()) {
        EXPECT_NEAR(value, 3.0f, 1e-5f);
    }
}

TEST(GaussianBlur, SpreadsAnImpulseAsANormalizedGaussian) {
    Raster<float> impulse(41, 41, 0.0f);
    impulse(20, 20) = 1.0f;
    const double sigma = 2.0;
    const Raster<float> blurred = GaussianBlur(impulse, sigma);
    double total = 0.0;
    for (const float value : blurred.Values()) {
        total += static_cast<double>(value);
    }
    EXPECT_NEAR(total, 1.0, 1e-4);
    EXPECT_NEAR(blurred(22, 20), blurred(18, 20), 1e-7);
    EXPECT_NEAR(blurred(20, 22), blurred(22, 20), 1e-7);
    EXPECT_NEAR(blurred(22, 20) / blurred(20, 20), std::exp(-4.0 / (2.0 * sigma * sigma)), 1e-4);
}

TEST(GaussianBlur, PreservesLinearRampsAwayFromTheEdges) {
    Raster<float> ramp(40, 10);
    for (std::size_t row = 0; row < ramp.Height(); ++row) {
        for (std::size_t col = 0; col < ramp.Width(); ++col) {
            ramp(col, row) = static_cast<float>(col) * 0.5f;
        }
    }
    const Raster<float> blurred = GaussianBlur(ramp, 1.5);
    for (std::size_t col = 6; col < 34; ++col) {
        EXPECT_NEAR(blurred(col, 5), ramp(col, 5), 1e-4f) << col;
    }
}

TEST(GaussianBlur, ExcludesAndKeepsNoDataCells) {
    Raster<float> raster(15, 15, 2.0f);
    raster.SetNoData(-9999.0f);
    raster(7, 7) = -9999.0f;
    raster(8, 7) = -9999.0f;
    const Raster<float> blurred = GaussianBlur(raster, 1.0);
    EXPECT_EQ(blurred(7, 7), -9999.0f);
    EXPECT_EQ(blurred(8, 7), -9999.0f);
    EXPECT_NEAR(blurred(6, 7), 2.0f, 1e-5f);
    EXPECT_NEAR(blurred(9, 7), 2.0f, 1e-5f);
}

TEST(GaussianBlur, ZeroSigmaCopiesAndInvalidSigmaThrows) {
    Raster<float> raster(4, 4, 1.0f);
    raster(1, 1) = 5.0f;
    const Raster<float> copy = GaussianBlur(raster, 0.0);
    EXPECT_EQ(copy(1, 1), 5.0f);
    EXPECT_THROW(GaussianBlur(raster, -1.0), std::invalid_argument);
    EXPECT_THROW(GaussianBlur(raster, std::numeric_limits<double>::quiet_NaN()),
                 std::invalid_argument);
    EXPECT_TRUE(GaussianBlur(Raster<float>(), 1.0).Empty());
}
