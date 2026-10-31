// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/bedforms.h"

#include "terraingen/terraingen.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

using terraingen::BedformLayer;
using terraingen::BedformOptions;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;

namespace {

BedformOptions Field() {
    BedformOptions options;
    options.wavelength = 2.0;
    options.height = 0.3;
    options.cover = 0.5;
    options.fieldSize = 30.0;
    options.leeFraction = 0.3;
    return options;
}

class Level final : public terraingen::Ground {
  public:
    explicit Level(double slope) : slope_(slope) {}

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = static_cast<float>(slope_ * samples.Position(i).x);
        }
    }

  private:
    double slope_;
};

}  // namespace

TEST(BedformLayer, GivesPointsTheSameBitsAsGridsOnAnyThreads) {
    const BedformLayer layer(Field());
    Raster<float> one(RasterGrid{120, 100, GeoTransform::NorthUp(-30.0, 25.0, 0.5)}, 1.0f);
    Raster<float> many = one;
    layer.Apply(one, {0.5, 1});
    layer.Apply(many, {0.5, 4});
    double highest = 0.0;
    for (std::size_t row = 0; row < one.Height(); ++row) {
        for (std::size_t col = 0; col < one.Width(); ++col) {
            ASSERT_EQ(one(col, row), many(col, row));
            const auto center = one.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, center.x, center.y, {0.5}), one(col, row));
            ASSERT_GE(one(col, row), 1.0f);
            highest = std::fmax(highest, static_cast<double>(one(col, row)) - 1.0);
        }
    }
    EXPECT_GT(highest, 0.1);
    EXPECT_LE(highest, 0.3 * 1.2 + 1e-6) << "the tallest field is 1.2 times the height";
    EXPECT_TRUE(std::isnan(layer.ApplyAt(std::nanf(""), 0.0, 0.0, {0.5})));
    EXPECT_THROW(layer.ApplyAt(1.0f, 0.0, 0.0, {0.0}), std::invalid_argument);
}

TEST(BedformLayer, CoversAboutTheShareOfGroundAskedFor) {
    for (const double cover : {0.0, 0.2, 0.6}) {
        BedformOptions options = Field();
        options.cover = cover;
        const BedformLayer layer(options);
        double inside = 0.0;
        constexpr int kSide = 200;
        for (int j = 0; j < kSide; ++j) {
            for (int i = 0; i < kSide; ++i) {
                inside += layer.MaterialCoverAt(7.3 * i, 6.1 * j, {}) > 0.5 ? 1.0 : 0.0;
            }
        }
        EXPECT_NEAR(inside / (kSide * kSide), cover, 0.12) << cover;
    }
}

TEST(BedformLayer, FadesBelowTwoSamplesPerWavelengthAndSkipsBareGround) {
    const BedformLayer layer(Field());
    for (int n = 0; n < 50; ++n) {
        EXPECT_EQ(layer.ApplyAt(1.0f, 3.1 * n, 1.7 * n, {1.0}), 1.0f) << "2 m at 1 m spacing";
    }
    BedformOptions bare = Field();
    bare.cover = 0.0;
    EXPECT_EQ(BedformLayer(bare).ApplyAt(1.0f, 5.0, 5.0, {0.1}), 1.0f);
}

TEST(BedformLayer, HasSteeperLeeFacesDownwind) {
    BedformOptions options = Field();
    options.cover = 0.9;
    options.fieldSize = 400.0;
    options.sinuosity = 0.0;
    const BedformLayer layer(options);
    double up = 0.0;
    double down = 0.0;
    for (int i = 0; i < 4000; ++i) {
        const double x = 0.01 * i;
        const double slope = (static_cast<double>(layer.ApplyAt(0.0f, x + 0.01, 3.0, {0.01})) -
                              static_cast<double>(layer.ApplyAt(0.0f, x, 3.0, {0.01}))) /
                             0.01;
        up = std::fmax(up, slope);
        down = std::fmax(down, -slope);
    }
    EXPECT_GT(down, 1.5 * up) << "wind toward +x: the lee faces east and is the steeper";
}

TEST(BedformLayer, KeepsSandOffBedrock) {
    BedformOptions options = Field();
    options.cover = 0.6;
    options.exposure = terraingen::ExposureParams{};
    const BedformLayer layer(options);
    const Level flat(0.0);
    const Level steep(0.8);
    double soil = 0.0;
    double boulder = 0.0;
    for (int n = 0; n < 400; ++n) {
        soil += layer.MaterialCoverAt(5.3 * n, 2.9 * n, {1.0, 1, &flat});
        boulder += layer.MaterialCoverAt(5.3 * n, 2.9 * n, {1.0, 1, &steep});
    }
    EXPECT_GT(soil, 0.0);
    EXPECT_EQ(boulder, 0.0) << "steep ground is all bedrock";
    EXPECT_THROW(layer.MaterialCoverAt(0.0, 0.0, {}), std::invalid_argument);
    Raster<float> heights(4, 4, 0.0f);
    EXPECT_THROW(layer.Apply(heights, {}), std::invalid_argument);
}

TEST(BedformLayer, ReseedsItsFields) {
    const BedformLayer layer(Field());
    const terraingen::LayerPtr a = layer.Reseeded(3);
    const terraingen::LayerPtr b = layer.Reseeded(3);
    const terraingen::LayerPtr c = layer.Reseeded(4);
    double differ = 0.0;
    for (int n = 0; n < 200; ++n) {
        const float ha = a->ApplyAt(0.0f, 3.1 * n, 1.7 * n, {0.1});
        EXPECT_EQ(ha, b->ApplyAt(0.0f, 3.1 * n, 1.7 * n, {0.1}));
        differ += std::fabs(static_cast<double>(ha - c->ApplyAt(0.0f, 3.1 * n, 1.7 * n, {0.1})));
    }
    EXPECT_GT(differ, 0.0);
}

TEST(BedformLayer, RejectsInvalidOptions) {
    const std::vector<std::function<void(BedformOptions&)>> changes{
        [](BedformOptions& o) { o.wavelength = 0.0; },
        [](BedformOptions& o) { o.height = -1.0; },
        [](BedformOptions& o) { o.leeFraction = 1.0; },
        [](BedformOptions& o) { o.cover = 0.95; },
        [](BedformOptions& o) { o.sinuosity = -1.0; },
        [](BedformOptions& o) { o.exposureSpacing = 0.0; },
        [](BedformOptions& o) { o.exposure = terraingen::ExposureParams{-1.0}; },
        [](BedformOptions& o) { o.fade = {2.0, 1.0}; }};
    for (const auto& change : changes) {
        BedformOptions options;
        change(options);
        EXPECT_THROW(BedformLayer{options}, std::invalid_argument);
    }
    EXPECT_EQ(BedformLayer(Field()).Options(), Field());
}

TEST(MarsBedforms, FollowLapotreAndHugenholtz) {
    const auto ripples = terraingen::MarsLargeRipples();
    const double leeSlope =
        terraingen::kPi * ripples.height / (2.0 * ripples.wavelength * ripples.leeFraction);
    EXPECT_NEAR(std::atan(leeSlope) * 180.0 / terraingen::kPi, 30.0, 1e-9);
    EXPECT_NO_THROW(terraingen::ValidateBedformOptions(ripples));
    const auto ridges = terraingen::MarsTransverseAeolianRidges();
    EXPECT_EQ(ridges.wavelength, 25.8);
    EXPECT_EQ(ridges.height, 1.3);
    EXPECT_EQ(ridges.leeFraction, 0.5);
}
