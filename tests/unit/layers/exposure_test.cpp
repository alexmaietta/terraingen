// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/exposure.h"

#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

using terraingen::ExposureField;
using terraingen::ExposureParams;

namespace {

/// A ground given by a function of position.
class Analytic final : public terraingen::Ground {
  public:
    explicit Analytic(std::function<float(double, double)> height) : height_(std::move(height)) {}

    float HeightAt(double x, double y, double /*spacing*/) const override {
        return height_(x, y);
    }

  private:
    std::function<float(double, double)> height_;
};

}  // namespace

TEST(ExposureField, CallsLevelGroundSoilAndSteepGroundBedrock) {
    const ExposureField field;
    EXPECT_EQ(field.Exposure(Analytic([](double, double) { return 3.0f; }), 0.0, 0.0, 1.0), 0.0);
    const auto plane = [](double slope) {
        return Analytic([slope](double x, double) { return static_cast<float>(slope * x); });
    };
    EXPECT_EQ(field.Exposure(plane(0.1), 5.0, 5.0, 1.0), 0.0);
    EXPECT_NEAR(field.Exposure(plane(0.3), 5.0, 5.0, 1.0), 0.5, 1e-5) << "halfway: smoothstep 0.5";
    EXPECT_EQ(field.Exposure(plane(0.6), 5.0, 5.0, 1.0), 1.0);
}

TEST(ExposureField, CallsRisesBedrockAndLowsSoil) {
    const ExposureField field;
    const Analytic mound([](double x, double y) {
        return static_cast<float>(2.0 * std::exp(-(x * x + y * y) / 200.0));
    });
    EXPECT_GT(field.Exposure(mound, 0.0, 0.0, 1.0), 0.99) << "a 2 m mound is bedrock on top";
    const Analytic pit([](double x, double y) {
        return static_cast<float>(-2.0 * std::exp(-(x * x + y * y) / 200.0));
    });
    EXPECT_EQ(field.Exposure(pit, 0.0, 0.0, 1.0), 0.0) << "soil settles in the low";
}

TEST(ExposureField, ReadsMissingSurroundingsAsLevelAndMissingPointsAsSoil) {
    const ExposureField field;
    const Analytic holes(
        [](double x, double) { return x > 5.0 ? std::nanf("") : static_cast<float>(0.6 * x); });
    EXPECT_EQ(field.Exposure(holes, 10.0, 0.0, 1.0), 0.0);
    EXPECT_GT(field.Exposure(holes, -20.0, 0.0, 1.0), 0.99);
}

TEST(ExposureField, CoversLevelGroundInPatchesWithFloorCover) {
    const Analytic flat([](double, double) { return 0.0f; });
    ExposureParams params;
    params.floorCover = 0.3;
    params.floorPatch = 20.0;
    params.seed = 4;
    const ExposureField field(params);
    double bedrock = 0.0;
    int samples = 0;
    for (int j = 0; j < 100; ++j) {
        for (int i = 0; i < 100; ++i) {
            bedrock += field.Exposure(flat, 3.1 * i, 2.9 * j, 1.0);
            ++samples;
        }
    }
    EXPECT_NEAR(bedrock / samples, 0.3, 0.12);
    EXPECT_EQ(ExposureField().Exposure(flat, 7.0, 3.0, 1.0), 0.0);
}

TEST(ExposureField, FillsGridsWithThePointValues) {
    const ExposureField field;
    const Analytic ridge(
        [](double x, double) { return static_cast<float>(3.0 * std::cos(x / 15.0)); });
    terraingen::Raster<float> raster(20, 8);
    raster.SetTransform(terraingen::GeoTransform::NorthUp(-50.0, 10.0, 5.0));
    field.Fill(raster, ridge, 5.0, 3);
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const auto center = raster.CellCenter(col, row);
            ASSERT_EQ(raster(col, row),
                      static_cast<float>(field.Exposure(ridge, center.x, center.y, 5.0)));
        }
    }
}

TEST(ExposureField, RejectsInvalidParameters) {
    const std::vector<std::function<void(ExposureParams&)>> changes{
        [](ExposureParams& p) { p.reach = 0.0; }, [](ExposureParams& p) { p.riseSoil = 0.05; },
        [](ExposureParams& p) { p.slopeBoulder = 0.1; },
        [](ExposureParams& p) { p.floorCover = 1.0; },
        [](ExposureParams& p) { p.floorPatch = -1.0; }};
    for (const auto& change : changes) {
        ExposureParams params;
        change(params);
        EXPECT_THROW(ExposureField{params}, std::invalid_argument);
    }
    EXPECT_EQ(ExposureField().Params(), ExposureParams{});
}

TEST(PatchNoise, StaysInRangeAndFollowsTwoOctavesOfPerlinNoise) {
    const terraingen::Surface plane;
    double lowest = 1.0;
    double highest = -1.0;
    for (int i = 0; i < 400; ++i) {
        const double x = 7.3 * i;
        const double y = -3.1 * i;
        const double value = terraingen::PatchNoise(plane, x, y, 50.0, 9u);
        lowest = std::min(lowest, value);
        highest = std::max(highest, value);
        const auto px = static_cast<float>(x / 50.0);
        const auto py = static_cast<float>(y / 50.0);
        const auto coarse = static_cast<double>(terraingen::PerlinNoise2(px, py, 9u));
        const auto fine = static_cast<double>(terraingen::PerlinNoise2(
            4.0f * px, 4.0f * py, terraingen::SubstreamSeed(9u, terraingen::kPatchFineStream)));
        const double expected = 2.5 * (0.7 * coarse + 0.3 * fine);
        EXPECT_EQ(value, std::clamp(expected, -1.0, 1.0));
    }
    EXPECT_GE(lowest, -1.0);
    EXPECT_LE(highest, 1.0);
    EXPECT_LT(lowest, -0.5) << "patches reach both ends";
    EXPECT_GT(highest, 0.5);
}

TEST(PatchNoise, IsSmoothNoiseOnASphere) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const double here = terraingen::PatchNoise(moon, 10.0, 20.0, 1000.0, 3u);
    const double near = terraingen::PatchNoise(moon, 10.0 + 1e-6, 20.0, 1000.0, 3u);
    EXPECT_GE(here, -1.0);
    EXPECT_LE(here, 1.0);
    EXPECT_NEAR(here, near, 1e-3);
    EXPECT_NE(terraingen::PatchNoise(moon, 10.0, 20.0, 1000.0, 4u), here);
}

TEST(LayerExposure, ReadsTheFieldAtItsSpacingAndNeedsTheGround) {
    const terraingen::LayerExposure none;
    EXPECT_FALSE(none);
    EXPECT_NO_THROW(none.Require(nullptr));
    const terraingen::LayerExposure exposure(ExposureParams{}, 2.0, {}, "Rocks");
    ASSERT_TRUE(exposure);
    const Analytic steep([](double x, double) { return static_cast<float>(0.8 * x); });
    EXPECT_EQ(exposure.At(3.0, 4.0, &steep), ExposureField().Exposure(steep, 3.0, 4.0, 2.0));
    EXPECT_THROW(exposure.Require(nullptr), std::invalid_argument);
    EXPECT_THROW(exposure.At(3.0, 4.0, nullptr), std::invalid_argument);
}
