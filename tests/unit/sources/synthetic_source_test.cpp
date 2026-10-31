// SPDX-License-Identifier: BSD-3-Clause

#include "support/source_agreement.h"
#include "terraingen/sources/synthetic_source.h"

#include "terraingen/random/stream.h"
#include "terraingen/sources/diamond_square.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::Fractal;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::SyntheticSource;
using terraingen::testing::ExpectPointsTilesAndGridAgree;

namespace {

std::vector<Fractal> Hills() {
    Fractal layer;
    layer.seed = 5;
    layer.wavelength = 80.0f;
    layer.amplitude = 6.0f;
    layer.octaves = 4;
    return std::vector<Fractal>{layer};
}

}  // namespace

TEST(SyntheticSource, AnEmptyStackIsFlatAtItsElevation) {
    const SyntheticSource flat;
    EXPECT_EQ(flat.HeightAt(1e6, -3.0, 1.0), 0.0f);
    const SyntheticSource raised(std::vector<Fractal>{}, 12.5f);
    Raster<float> heights(RasterGrid{4, 3, GeoTransform::NorthUp(-1e5, 2e5, 7.0)});
    raised.Fill(heights);
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, 12.5f);
    }
    EXPECT_FALSE(heights.NoData().has_value());
    EXPECT_FALSE(terraingen::BoundedGrid(raised).has_value());
    EXPECT_EQ(raised.ResolutionAt(0.0, 0.0), std::numeric_limits<double>::infinity());
    EXPECT_EQ(raised.PostSpacingAt(0.0, 0.0), std::numeric_limits<double>::infinity());
    EXPECT_EQ(raised.ReferencePoint().x, 0.0);
    EXPECT_TRUE(raised.Keys().Empty());
    EXPECT_EQ(raised.UpsamplerInUse(), nullptr);
}

TEST(SyntheticSource, EvaluatesTheNoiseAtEveryPointWithoutAPixelSize) {
    const SyntheticSource source(Hills(), 3.0f);
    for (const double spacing : {10.0, 0.1}) {
        EXPECT_EQ(source.HeightAt(17.3, -4.1, spacing),
                  3.0f + terraingen::FractalStackAt(Hills(), {}, {}, 17.3, -4.1));
    }
    Raster<float> filled(RasterGrid{64, 48, GeoTransform::NorthUp(-50.0, 40.0, 2.0)});
    source.Fill(filled);
    const std::vector<Fractal> layers = Hills();
    for (std::size_t row = 0; row < filled.Height(); ++row) {
        for (std::size_t col = 0; col < filled.Width(); ++col) {
            const terraingen::Vec2d c = filled.CellCenter(col, row);
            ASSERT_EQ(filled(col, row), 3.0f + terraingen::EvaluateFractals(
                                                   static_cast<float>(c.x), static_cast<float>(c.y),
                                                   layers.data(), layers.size()));
        }
    }
    ExpectPointsTilesAndGridAgree(source, {20, 15, GeoTransform::NorthUp(13.1, 7.7, 0.9)});
    const RasterGrid grid = source.Grid();
    EXPECT_EQ(grid.width, 0u);
    EXPECT_EQ(grid.transform, GeoTransform::NorthUp(0.0, 0.0, 1.0));
}

TEST(SyntheticSource, UpsamplesNoiseSampledAtPostsWithAPixelSize) {
    const SyntheticSource source(Hills(), 0.0f, 8.0);
    EXPECT_EQ(source.PostSpacingAt(1.0, 2.0), 8.0);
    EXPECT_EQ(source.ResolutionAt(1.0, 2.0), std::numeric_limits<double>::infinity())
        << "generated posts hold no measured features";
    EXPECT_EQ(source.PixelSize(), 8.0);
    EXPECT_EQ(source.Grid().transform, GeoTransform::NorthUp(-4.0, 4.0, 8.0));
    EXPECT_FALSE(terraingen::BoundedGrid(source).has_value());
    const float post = terraingen::FractalStackAt(Hills(), {}, {}, 16.0, -24.0);
    EXPECT_EQ(source.HeightAt(16.0, -24.0, 8.0), post);
    EXPECT_EQ(source.HeightAt(16.0, -24.0, 1.0), post) << "cubic passes through the posts";
    const float between = source.HeightAt(19.0, -21.0, 1.0);
    EXPECT_NE(between, terraingen::FractalStackAt(Hills(), {}, {}, 19.0, -21.0))
        << "noise finer than the posts is not evaluated";
    ExpectPointsTilesAndGridAgree(source, {24, 18, GeoTransform::NorthUp(-31.0, 17.0, 1.3)});
    ExpectPointsTilesAndGridAgree(source, {20, 16, GeoTransform::NorthUp(-4.0, 4.0, 8.0)});
}

TEST(SyntheticSource, RefinesItsPostsWithDiamondSquare) {
    terraingen::DiamondSquareOptions options;
    options.seed = 2;
    const SyntheticSource source(Hills(), 0.0f, 8.0,
                                 std::make_shared<terraingen::DiamondSquareUpsampler>(options));
    const auto fitted = std::dynamic_pointer_cast<const terraingen::DiamondSquareUpsampler>(
        source.UpsamplerInUse());
    ASSERT_NE(fitted, nullptr);
    EXPECT_TRUE(fitted->Options().fractalDimension.has_value()) << "estimated from the posts";
    EXPECT_FALSE(fitted->Options().scale.has_value()) << "fitted window by window as it refines";
    const float post = terraingen::FractalStackAt(Hills(), {}, {}, 16.0, -24.0);
    EXPECT_EQ(source.HeightAt(16.0, -24.0, 0.5), post);
    ExpectPointsTilesAndGridAgree(source, {24, 18, GeoTransform::NorthUp(-31.0, 17.0, 0.9)});
}

TEST(SyntheticSource, EvaluatesNoiseIn3DOnABody) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const SyntheticSource source(Hills(), 0.0f, std::nullopt, nullptr, moon);
    EXPECT_EQ(source.HeightAt(10.0, 20.0, 1.0),
              terraingen::FractalStackAt(Hills(), {}, moon, 10.0, 20.0));
    EXPECT_NEAR(source.HeightAt(-180.0, 5.0, 1.0), source.HeightAt(180.0, 5.0, 1.0), 1e-3)
        << "seamless across the antimeridian";
}

TEST(SyntheticSource, RejectsInvalidSettings) {
    std::vector<Fractal> many;
    many.assign(terraingen::kMaxFractals + 1, Fractal{});
    EXPECT_THROW(SyntheticSource{many}, std::invalid_argument);
    std::vector<Fractal> bad = Hills();
    bad[0].octaves = 0;
    EXPECT_THROW(SyntheticSource{bad}, std::invalid_argument);
    EXPECT_THROW(SyntheticSource(Hills(), std::numeric_limits<float>::infinity()),
                 std::invalid_argument);
    EXPECT_THROW(SyntheticSource(Hills(), 0.0f, 0.0), std::invalid_argument);
    EXPECT_THROW(SyntheticSource(Hills(), 0.0f, std::nullopt,
                                 std::make_shared<terraingen::KernelUpsampler>()),
                 std::invalid_argument)
        << "an upsampler needs posts";
}

TEST(SyntheticSource, ADefaultStackFillsZeros) {
    Raster<float> heights(RasterGrid{5, 4, GeoTransform::NorthUp(-30.0, 20.0, 3.0)});
    SyntheticSource().Fill(heights);
    for (const float value : heights.Values()) {
        EXPECT_EQ(value, 0.0f);
    }
}

TEST(SyntheticSource, TakesItsPixelSizeInMetersOnABody) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const SyntheticSource source(Hills(), 0.0f, 5.0, nullptr, moon);
    ASSERT_TRUE(source.PixelSize().has_value());
    EXPECT_DOUBLE_EQ(*source.PixelSize(), 5.0 / moon.UnitLength()) << "posts 5 m apart, in degrees";
    EXPECT_EQ(*SyntheticSource(Hills(), 0.0f, 5.0).PixelSize(), 5.0) << "meters on a plane";
}

namespace {

/// The heights of `source` over a 24 x 16 grid of 2.5 m cells.
Raster<float> Sampled(const terraingen::Source& source) {
    Raster<float> heights(RasterGrid{24, 16, GeoTransform::NorthUp(-30.0, 20.0, 2.5)});
    source.Fill(heights, 1.0);
    return heights;
}

/// Whether `a` and `b` hold the same bits in every cell.
bool Same(const Raster<float>& a, const Raster<float>& b) {
    return std::memcmp(a.Values().data(), b.Values().data(), a.Size() * sizeof(float)) == 0;
}

}  // namespace

TEST(SyntheticSource, ReseedsEachFractalFromItsOwnWord) {
    std::vector<Fractal> stack = Hills();
    stack.push_back(stack[0]);
    stack[1].wavelength = 20.0f;
    const SyntheticSource source(stack, 3.0f);
    const auto one = std::dynamic_pointer_cast<const SyntheticSource>(source.Reseeded(11));
    ASSERT_NE(one, nullptr);
    ASSERT_EQ(one->Fractals().size(), 2u);
    EXPECT_EQ(one->Fractals()[0].seed, terraingen::ReseedWord(11, 0));
    EXPECT_EQ(one->Fractals()[1].seed, terraingen::ReseedWord(11, 1));
    EXPECT_EQ(one->Fractals()[1].wavelength, 20.0f) << "only the seeds change";
    EXPECT_TRUE(Same(Sampled(*one), Sampled(*source.Reseeded(11)))) << "deterministic";
    EXPECT_FALSE(Same(Sampled(*one), Sampled(*source.Reseeded(12))));
    EXPECT_FALSE(Same(Sampled(*one), Sampled(source)));
    EXPECT_EQ(SyntheticSource().Reseeded(11), nullptr) << "an empty stack has no seeds";
}

TEST(SyntheticSource, ReseedsItsUpsamplerAndKeepsItsPosts) {
    terraingen::DiamondSquareOptions options;
    options.fractalDimension = 2.4;
    options.scale = 0.5;
    const auto refined = std::make_shared<terraingen::DiamondSquareUpsampler>(options);
    const SyntheticSource source(Hills(), 0.0f, 5.0, refined);
    const auto one = std::dynamic_pointer_cast<const SyntheticSource>(source.Reseeded(4));
    ASSERT_NE(one, nullptr);
    EXPECT_EQ(one->PixelSize(), source.PixelSize());
    const auto upsampler =
        std::dynamic_pointer_cast<const terraingen::DiamondSquareUpsampler>(one->UpsamplerInUse());
    ASSERT_NE(upsampler, nullptr);
    EXPECT_EQ(upsampler->Options().seed,
              terraingen::ReseedWord(
                  terraingen::JoinWords(terraingen::ReseedWord(4, terraingen::kMaxFractals),
                                        terraingen::ReseedWord(4, terraingen::kMaxFractals + 1)),
                  0));
    EXPECT_TRUE(Same(Sampled(*one), Sampled(*source.Reseeded(4))));

    const SyntheticSource flatPosts({}, 1.0f, 5.0, refined);
    const auto posts = std::dynamic_pointer_cast<const SyntheticSource>(flatPosts.Reseeded(4));
    ASSERT_NE(posts, nullptr) << "the upsampler has a seed though the stack is empty";
    EXPECT_TRUE(posts->Fractals().empty());
    EXPECT_EQ(SyntheticSource({}, 1.0f, 5.0).Reseeded(4), nullptr)
        << "cubic interpolation has no seed";
}
