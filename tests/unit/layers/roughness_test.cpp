#include "terraingen/layers/roughness.h"

#include "terraingen/noise/perlin.h"
#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

using terraingen::PerlinStructureFunction;
using terraingen::RoughnessLayer;
using terraingen::RoughnessOptions;

TEST(RoughnessLayer, RejectsInvalidOptions) {
    EXPECT_NO_THROW(terraingen::ValidateRoughnessOptions({}));
    const std::vector<std::function<void(RoughnessOptions&)>> changes{
        [](RoughnessOptions& o) { o.rmsSlope = 0.0; },
        [](RoughnessOptions& o) { o.baseline = -1.0; },
        [](RoughnessOptions& o) { o.hurst = 0.0; },
        [](RoughnessOptions& o) { o.hurst = 1.5; },
        [](RoughnessOptions& o) { o.minWavelength = 0.0; },
        [](RoughnessOptions& o) { o.minWavelength = 8.0; },
        [](RoughnessOptions& o) { o.maxWavelength = std::numeric_limits<double>::infinity(); },
        [](RoughnessOptions& o) { o.minWavelength = 1e-12; },
        [](RoughnessOptions& o) { o.fade = {2.0, 1.0}; }};
    for (const auto& bad : changes) {
        RoughnessOptions invalid;
        bad(invalid);
        EXPECT_THROW(terraingen::ValidateRoughnessOptions(invalid), std::invalid_argument);
        EXPECT_THROW(RoughnessLayer{invalid}, std::invalid_argument);
    }
}

TEST(RoughnessLayer, HalvesOctavesDownToTheShortestWavelength) {
    EXPECT_EQ(RoughnessLayer({}).Wavelengths(),
              (std::vector<double>{4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125, 0.015625}));
    EXPECT_EQ(RoughnessLayer({.maxWavelength = 4.0, .minWavelength = 0.25}).Wavelengths(),
              (std::vector<double>{4.0, 2.0, 1.0, 0.5, 0.25}));
    EXPECT_EQ(RoughnessLayer({.maxWavelength = 0.3, .minWavelength = 0.3}).Wavelengths(),
              std::vector<double>{0.3});
}

TEST(RoughnessLayer, HasTheRmsSlopeAskedForOverItsBaseline) {
    for (const double hurst : {0.5, 0.8, 1.0}) {
        const RoughnessLayer layer({.rmsSlope = 0.2, .baseline = 0.1, .hurst = hurst});
        EXPECT_NEAR(layer.RmsSlope(0.1), 0.2, 1e-12);
        EXPECT_GT(layer.RmsSlope(0.05), layer.RmsSlope(0.1)) << "steeper over shorter baselines";
        EXPECT_GT(layer.RmsSlope(0.1), layer.RmsSlope(0.5));
    }
    const RoughnessLayer rough({.hurst = 0.5});
    const RoughnessLayer smooth({.hurst = 0.9});
    EXPECT_GT(rough.RmsSlope(0.03) / rough.RmsSlope(0.25),
              smooth.RmsSlope(0.03) / smooth.RmsSlope(0.25))
        << "a smaller Hurst exponent is rougher at fine scales";
}

TEST(PerlinStructureFunction, FollowsItsLimitsAndIsContinuous) {
    EXPECT_EQ(PerlinStructureFunction(0.0), 0.0);
    EXPECT_EQ(PerlinStructureFunction(-1.0), 0.0);
    EXPECT_NEAR(PerlinStructureFunction(1e-4) / 1e-8, 0.7724, 1e-3);
    EXPECT_EQ(PerlinStructureFunction(100.0), 0.18312);
    double previous = 0.0;
    for (double r = 1e-3; r < 10.0; r *= 1.01) {
        const double value = PerlinStructureFunction(r);
        EXPECT_GE(value, previous * 0.99) << r;
        EXPECT_LT(std::fabs(PerlinStructureFunction(r * 1.0001) - value), 1e-3 * value + 1e-12)
            << r;
        previous = value;
    }
}

TEST(PerlinStructureFunction, MatchesSampledDifferencesOfThePerlinBasis) {
    for (const double r : {0.1, 0.5, 1.0, 3.0}) {
        double sum = 0.0;
        constexpr int kSamples = 1 << 16;
        for (int n = 0; n < kSamples; ++n) {
            const auto seed = static_cast<std::uint32_t>(n);
            const float x = 0.37f * static_cast<float>(n % 256) + 0.013f * static_cast<float>(n);
            const float y = 0.29f * static_cast<float>(n / 256);
            const double angle = 2.399963 * n;
            const double a = terraingen::PerlinNoise2(x, y, seed);
            const double b = terraingen::PerlinNoise2(
                static_cast<float>(static_cast<double>(x) + r * std::cos(angle)),
                static_cast<float>(static_cast<double>(y) + r * std::sin(angle)), seed);
            sum += (a - b) * (a - b);
        }
        EXPECT_NEAR(sum / kSamples, PerlinStructureFunction(r), 0.03 * PerlinStructureFunction(r))
            << r;
    }
}

TEST(RoughnessLayer, GivesPointsTheSameBitsAsGridsOnAnyThreads) {
    const RoughnessLayer layer({.seed = 7});
    const terraingen::RasterGrid grid{
        64, 48, terraingen::GeoTransform::NorthUp(350000.25, 4100000.5, 0.02)};
    auto one = grid.MakeRaster();
    auto many = grid.MakeRaster();
    layer.Apply(one, {0.0, 1});
    layer.Apply(many, {0.0, 4});
    double sumSquares = 0.0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            ASSERT_EQ(one(col, row), many(col, row));
            const auto centre = one.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {0.02}), one(col, row));
            sumSquares += std::pow(static_cast<double>(one(col, row)), 2.0);
        }
    }
    EXPECT_GT(sumSquares, 0.0);
}

TEST(RoughnessLayer, FadesOctavesTooFineForTheSpacing) {
    const RoughnessLayer layer({});
    EXPECT_EQ(layer.ApplyAt(1.5f, 0.3, 0.7, {8.0}), 1.5f) << "4 m octaves need 2 samples";
    double fine = 0.0;
    double coarse = 0.0;
    for (int n = 0; n < 400; ++n) {
        const double x = 0.731 * n;
        const double y = 0.377 * n;
        fine += std::pow(static_cast<double>(layer.ApplyAt(0.0f, x, y, {0.002})), 2.0);
        coarse += std::pow(static_cast<double>(layer.ApplyAt(0.0f, x, y, {0.5})), 2.0);
    }
    EXPECT_GT(fine, coarse);
    EXPECT_GT(coarse, 0.0);
    const RoughnessLayer exact({.minWavelength = 0.25});
    for (int n = 0; n < 50; ++n) {
        EXPECT_EQ(exact.ApplyAt(0.0f, 0.37 * n, 0.11 * n, {0.0625}),
                  exact.ApplyAt(0.0f, 0.37 * n, 0.11 * n, {0.001}))
            << "every octave whole from 4 samples per wavelength";
    }
}

TEST(RoughnessLayer, LeavesMissingCellsAndRejectsBadSpacings) {
    const RoughnessLayer layer({});
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {0.1})));
    EXPECT_THROW(layer.ApplyAt(0.0f, 0.0, 0.0, {0.0}), std::invalid_argument);
    const terraingen::RasterGrid grid{4, 4, terraingen::GeoTransform::NorthUp(0.0, 4.0, 0.01)};
    auto heights = grid.MakeRaster();
    heights.SetNoData(-9999.0f);
    heights(1, 2) = -9999.0f;
    layer.Apply(heights, {});
    EXPECT_EQ(heights(1, 2), -9999.0f);
    EXPECT_NE(heights(0, 0), 0.0f);
}
