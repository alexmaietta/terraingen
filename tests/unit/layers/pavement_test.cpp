// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/pavement.h"

#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using terraingen::PavementLayer;
using terraingen::PavementOptions;

namespace {

/// Plates without tilt, each stepped, with deep, narrow cracks.
PavementOptions Stepped() {
    PavementOptions options;
    options.plateSize = 1.0;
    options.crackWidth = 0.04;
    options.crackDepth = 0.1;
    options.edgeRounding = 0.01;
    options.maxTiltDegrees = 0.0 * terraingen::kRadian;
    options.maxStep = 0.05;
    options.warp = 0.0;
    return options;
}

/// Ground tilted along x by `slope`.
class Tilted final : public terraingen::Ground {
  public:
    explicit Tilted(double slope) : slope_(slope) {}
    float HeightAt(double x, double, double) const override {
        return static_cast<float>(slope_ * x);
    }

  private:
    double slope_;
};

/// The pavement's relief at (x, y) with every octave and crack resolved.
double Relief(const PavementLayer& layer, double x, double y) {
    return static_cast<double>(layer.ApplyAt(0.0f, x, y, {1e-4}));
}

}  // namespace

TEST(PavementLayer, PlatesAreFlatAndCracksCutBetweenThem) {
    const PavementOptions options = Stepped();
    const PavementLayer layer(options);
    int interiors = 0;
    double deepest = 0.0;
    for (double x = 0.0; x < 6.0; x += 0.005) {
        const double relief = Relief(layer, x, 0.37);
        if (relief == Relief(layer, x + 0.01, 0.37 + 0.01)) {
            EXPECT_LE(std::fabs(relief), options.maxStep + 1e-7) << "a flat, untilted plate";
            ++interiors;
        }
        deepest = std::min(deepest, relief);
    }
    EXPECT_GT(interiors, 100);
    EXPECT_LT(deepest, -options.crackDepth + options.maxStep);
    EXPECT_GE(deepest, -options.crackDepth - options.maxStep);
}

TEST(PavementLayer, PlatesMeetWithoutStepsAndRidgesRise) {
    PavementOptions options = Stepped();
    options.maxTiltDegrees = 0.1 * terraingen::kRadian;
    options.warp = 0.15;
    options.octaves = 3;
    const PavementLayer layer(options);
    double previous = Relief(layer, 0.0, 1.23);
    double jump = 0.0;
    for (double x = 0.0005; x < 8.0; x += 0.0005) {
        const double relief = Relief(layer, x, 1.23);
        jump = std::max(jump, std::fabs(relief - previous));
        previous = relief;
    }
    EXPECT_LT(jump, 0.01) << "no steps at plate edges, at 0.5 mm";

    options = Stepped();
    options.maxStep = 0.0;
    options.raised = true;
    const PavementLayer ridges(options);
    double highest = 0.0;
    double lowest = 0.0;
    for (double x = 0.0; x < 6.0; x += 0.002) {
        highest = std::max(highest, Relief(ridges, x, 0.5));
        lowest = std::min(lowest, Relief(ridges, x, 0.5));
    }
    EXPECT_NEAR(highest, options.crackDepth, 0.01);
    EXPECT_EQ(lowest, 0.0);
}

TEST(PavementLayer, FinerOctavesSplitPlatesAndFadeWithSpacing) {
    PavementOptions options = Stepped();
    options.maxStep = 0.0;
    const PavementLayer one(options);
    options.octaves = 4;
    const PavementLayer four(options);
    int split = 0;
    for (double x = 0.0; x < 6.0; x += 0.01) {
        if (Relief(one, x, 2.1) == 0.0 && Relief(four, x, 2.1) < -0.01) {
            ++split;
        }
    }
    EXPECT_GT(split, 5) << "finer cracks inside the coarse plates";
    for (double x = 0.0; x < 6.0; x += 0.37) {
        const float coarse = four.ApplyAt(0.0f, x, 2.1, {0.3});
        const float onlyOne = one.ApplyAt(0.0f, x, 2.1, {0.3});
        EXPECT_FLOAT_EQ(coarse, onlyOne) << "octaves finer than the spacing fade out";
    }
    EXPECT_EQ(four.ApplyAt(1.5f, 0.3, 0.2, {10.0}), 1.5f) << "all faded";
}

TEST(PavementLayer, ShowsOnExposedBedrockAndLaysIt) {
    PavementOptions options = Stepped();
    options.exposure = terraingen::ExposureParams{};
    const PavementLayer layer(options);
    const Tilted level(0.0);
    const Tilted steep(0.8);
    EXPECT_EQ(layer.Material(), terraingen::MaterialClass::Bedrock);
    EXPECT_EQ(layer.MaterialCoverAt(0.3, 0.3, 0.01, &level), 0.0);
    EXPECT_EQ(layer.MaterialCoverAt(0.3, 0.3, 0.01, &steep), 1.0);
    EXPECT_THROW(layer.MaterialCoverAt(0.3, 0.3, 0.01, nullptr), std::invalid_argument);
    double change = 0.0;
    for (double x = 0.0; x < 3.0; x += 0.013) {
        change = std::max(
            change, std::fabs(static_cast<double>(layer.ApplyAt(0.0f, x, 0.4, {0.01, 1, &level}))));
    }
    EXPECT_EQ(change, 0.0) << "no plates on soil";
    const float on = layer.ApplyAt(0.0f, 0.3, 0.3, {0.01, 1, &steep});
    EXPECT_EQ(on, PavementLayer(Stepped()).ApplyAt(0.0f, 0.3, 0.3, {0.01}));
    EXPECT_THROW(layer.ApplyAt(0.0f, 0.3, 0.3, {0.01}), std::invalid_argument);
    EXPECT_EQ(PavementLayer(Stepped()).MaterialCoverAt(0.3, 0.3, 0.01, nullptr), 1.0);
}

TEST(PavementLayer, GridsAndPointsAgree) {
    PavementOptions options = Stepped();
    options.octaves = 3;
    options.maxTiltDegrees = 0.05 * terraingen::kRadian;
    const PavementLayer layer(options);
    const terraingen::RasterGrid grid{40, 30, terraingen::GeoTransform::NorthUp(-1.0, 2.0, 0.02)};
    terraingen::Raster<float> heights = grid.MakeRaster();
    layer.Apply(heights, {0.02, 2});
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(0.0f, center.x, center.y, {0.02}), heights(col, row));
        }
    }
}

TEST(PavementLayer, LaysPlatesOutInTheAnchorsFrameOnABody) {
    const terraingen::Surface mars = terraingen::Surface::Sphere(3389500.0);
    PavementOptions options = Stepped();
    options.anchor = {137.4, -4.6};
    const PavementLayer body(options, mars);
    const PavementLayer plane(options);
    const terraingen::Vec2d point{137.40001, -4.59998};
    const terraingen::Vec2d offset = mars.Offset(options.anchor, point);
    EXPECT_NEAR(Relief(body, point.x, point.y), Relief(plane, offset.x, offset.y), 1e-7);
    EXPECT_EQ(Relief(body, -42.6, 4.6), 0.0);
    EXPECT_EQ(body.ApplyAt(2.0f, -42.6, 4.6, {0.01}), 2.0f);
}

TEST(PavementLayer, OldSoakerRidgesAndRejectsBadOptions) {
    const PavementOptions ridges = terraingen::MarsDesiccationRidges();
    EXPECT_EQ(ridges.plateSize, 0.02);
    EXPECT_TRUE(ridges.raised);
    EXPECT_EQ(ridges.crackDepth, 0.005);
    EXPECT_NO_THROW(PavementLayer{ridges});
    const auto rejects = [](auto change) {
        PavementOptions options;
        change(options);
        EXPECT_THROW(PavementLayer{options}, std::invalid_argument);
    };
    rejects([](PavementOptions& o) { o.plateSize = 0.0; });
    rejects([](PavementOptions& o) { o.crackWidth = -1.0; });
    rejects([](PavementOptions& o) { o.crackDepth = -0.1; });
    rejects([](PavementOptions& o) { o.maxTiltDegrees = 2.0 * terraingen::kRadian; });
    rejects([](PavementOptions& o) { o.jitter = 1.5; });
    rejects([](PavementOptions& o) { o.octaves = 0; });
    rejects([](PavementOptions& o) { o.octaves = 9; });
    rejects([](PavementOptions& o) { o.lacunarity = 1.0; });
    rejects([](PavementOptions& o) { o.gain = 0.0; });
    rejects([](PavementOptions& o) { o.anchor.y = std::numeric_limits<double>::quiet_NaN(); });
    rejects([](PavementOptions& o) { o.exposureSpacing = 0.0; });
    rejects([](PavementOptions& o) { o.exposure = terraingen::ExposureParams{-1.0}; });
}
