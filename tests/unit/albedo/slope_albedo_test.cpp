// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/slope_albedo.h"

#include "terraingen/albedo/synthetic.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::Albedo;
using terraingen::GeoTransform;
using terraingen::RasterGrid;
using terraingen::SlopeAlbedo;
using terraingen::SlopeAlbedoOptions;

namespace {

/// An evaluation at `spacing` meters over `ground`.
terraingen::LayerContext OnGround(double spacing, const terraingen::Ground* ground) {
    terraingen::LayerContext context;
    context.spacing = spacing;
    context.ground = ground;
    return context;
}

/// Ground tilted toward +x by `degrees`.
class Tilted final : public terraingen::Ground {
  public:
    explicit Tilted(double degrees) : gradient_(std::tan(degrees * terraingen::kPi / 180.0)) {}

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = static_cast<float>(gradient_ * samples.Position(i).x);
        }
    }

  private:
    double gradient_;
};

/// A bowl whose walls steepen away from the origin.
class Bowl final : public terraingen::Ground {
  protected:
    void FillSamples(terraingen::SampleBlock& samples, double,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const terraingen::Vec2d p = samples.Position(i);
            samples.Height(i) = static_cast<float>(0.02 * (p.x * p.x + p.y * p.y));
        }
    }
};

Albedo Gray(double value) {
    terraingen::SyntheticAlbedoOptions params;
    params.value = value;
    return {std::make_shared<terraingen::SyntheticAlbedo>(params),
            {std::make_shared<SlopeAlbedo>()},
            {}};
}

}  // namespace

TEST(SlopeAlbedo, MaturesFromTheOnsetAtTheMeasuredRate) {
    const SlopeAlbedo slopes;
    EXPECT_EQ(slopes.MaturityAt(0.0), 0.10);
    EXPECT_EQ(slopes.MaturityAt(20.0), 0.10);
    EXPECT_DOUBLE_EQ(slopes.MaturityAt(30.0), 0.10 + 0.065);
    EXPECT_EQ(slopes.MaturityAt(89.0), 0.42) << "capped";
}

TEST(SlopeAlbedo, ReadsTheSlopeOfTheGround) {
    const SlopeAlbedo slopes;
    for (const double degrees : {0.0, 12.0, 35.0}) {
        const Tilted ground(degrees);
        EXPECT_NEAR(slopes.SlopeAt(3.7, -41.2, ground), degrees, 1e-3);
    }
}

TEST(SlopeAlbedo, BrightensSteepGroundAsFreshMaterial) {
    const Tilted steep(30.0);
    const Tilted gentle(10.0);
    const Albedo albedo = Gray(0.12);
    const auto value = [&](const terraingen::Ground& ground) {
        return terraingen::EvaluateAlbedoAt(albedo, 1.0, 2.0, OnGround(1.0, &ground))[0];
    };
    EXPECT_FLOAT_EQ(value(gentle), 0.12f);
    EXPECT_FLOAT_EQ(value(steep), static_cast<float>(terraingen::ReflectanceAtMaturity(
                                      static_cast<double>(0.12f), 0.10, 0.165)));
    EXPECT_NEAR(value(steep) - 0.12f, 0.026, 0.002)
        << "about the 0.02 Lucey et al. (2014) found beyond 20 degrees";
    EXPECT_FLOAT_EQ(terraingen::EvaluateAlbedoAt(albedo, 1.0, 2.0, {1.0})[0], 0.12f)
        << "no ground, no change";
}

TEST(SlopeAlbedo, GridsAndPointsAgree) {
    const Bowl bowl;
    const Albedo albedo = Gray(0.12);
    const RasterGrid grid{40, 30, GeoTransform::NorthUp(-60.0, 45.0, 3.0)};
    const auto values = terraingen::EvaluateAlbedo(albedo, grid, OnGround(3.0, &bowl));
    EXPECT_FLOAT_EQ(values[0](20, 15), 0.12f) << "the gentle floor";
    EXPECT_GT(values[0](0, 0), 0.12f) << "the steep wall";
    for (std::size_t row = 0; row < grid.height; row += 3) {
        for (std::size_t col = 0; col < grid.width; col += 7) {
            const auto center = values[0].CellCenter(col, row);
            ASSERT_EQ(
                terraingen::EvaluateAlbedoAt(albedo, center.x, center.y, OnGround(3.0, &bowl))[0],
                values[0](col, row));
        }
    }
}

TEST(SlopeAlbedo, RejectsBadOptions) {
    using Change = std::function<void(SlopeAlbedoOptions&)>;
    const std::vector<Change> changes{
        [](auto& o) { o.onsetDegrees = 90.0; },
        [](auto& o) { o.maturityPerDegree = -1.0; },
        [](auto& o) { o.backgroundMaturity = 0.0; },
        [](auto& o) { o.maxMaturity = 0.05; },
        [](auto& o) { o.baseline = 0.0; },
        [](auto& o) { o.originReflectance = std::numeric_limits<double>::quiet_NaN(); }};
    for (const Change& change : changes) {
        SlopeAlbedoOptions options;
        change(options);
        EXPECT_THROW(terraingen::ValidateSlopeAlbedoOptions(options), std::invalid_argument);
    }
}

TEST(SlopeAlbedo, ReadsSlopesInMetersOnTheGivenSurface) {
    const terraingen::Surface sphere = terraingen::Surface::Sphere(1737400.0);
    class EastSlope final : public terraingen::Ground {
      public:
        explicit EastSlope(const terraingen::Surface& surface) : surface_(surface) {}

      protected:
        void FillSamples(terraingen::SampleBlock& samples, double,
                         unsigned /*threads*/) const override {
            for (std::size_t i = 0; i < samples.Size(); ++i) {
                samples.Height(i) =
                    static_cast<float>(0.5 * surface_.Offset({30.0, 60.0}, samples.Position(i)).x);
            }
        }

      private:
        const terraingen::Surface& surface_;
    } ground(sphere);
    const SlopeAlbedo slopes;
    const double expected = std::atan(0.5) * 180.0 / terraingen::kPi;
    EXPECT_NEAR(slopes.SlopeAt(30.0, 60.0, ground, sphere), expected, 0.5);
    EXPECT_GT(slopes.SlopeAt(30.0, 60.0, ground), 80.0) << "degrees read as meters on a plane";
}
