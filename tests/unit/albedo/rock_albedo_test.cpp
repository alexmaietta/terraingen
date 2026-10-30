// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/rock_albedo.h"

#include "support/golombek_rapp.h"

#include "terraingen/albedo/synthetic.h"
#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/random/samplers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::Albedo;
using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;
using terraingen::GeoTransform;
using terraingen::RasterGrid;
using terraingen::RockAlbedo;
using terraingen::RockAlbedoOptions;
using terraingen::RockShare;
using terraingen::UnresolvedCover;

namespace {

/// Level ground at height 0.
class Level final : public terraingen::Ground {
  protected:
    void FillSamples(terraingen::SampleBlock& samples, double) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = 0.0f;
        }
    }
};

std::shared_ptr<const BoulderLayer> Boulders(double coverage = 0.02) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.seed = 3;
    options.sizes.coverage = coverage;
    options.sizes.minDiameter = 0.1;
    options.sizes.maxDiameter = 2.0;
    return std::make_shared<BoulderLayer>(options);
}

Albedo Gray(const std::shared_ptr<const RockAlbedo>& rocks) {
    terraingen::SyntheticAlbedoOptions params;
    params.value = 0.1;
    return {std::make_shared<terraingen::SyntheticAlbedo>(params), {rocks}, {}};
}

}  // namespace

TEST(RockShare, AddsTheUnresolvedShareOfWhatTheResolvedLeaves) {
    EXPECT_EQ(RockShare(0.25, {}, 1.0), 0.25);
    EXPECT_EQ(RockShare(1.0, {0.1, 0.01}, 2.0), 1.0);
    EXPECT_DOUBLE_EQ(RockShare(0.0, {0.1, 0.0}, 3.0), 0.1) << "no scatter, the mean";
    EXPECT_LE(RockShare(0.0, {0.5, 10.0}, 8.0), 1.0);
    double sum = 0.0;
    double squares = 0.0;
    constexpr int kDraws = 200000;
    for (int n = 0; n < kDraws; ++n) {
        const double u1 = (n + 0.5) / kDraws;
        const double u2 = std::fmod(n * 0.6180339887, 1.0);
        const double share = RockShare(0.0, {0.01, 2e-4}, terraingen::StandardNormal(u1, u2));
        sum += share;
        squares += share * share;
    }
    const double mean = sum / kDraws;
    EXPECT_NEAR(mean, 0.01, 2e-4);
    EXPECT_NEAR(squares / kDraws - mean * mean, 2e-4, 2e-5) << "log-normal of the same variance";
}

TEST(BoulderLayer, CoversWithBouldersTooSmallToResolveTheAreaTheirSizesGive) {
    const auto layer = Boulders();
    const terraingen::GolombekRappSfd sizes(0.02);
    const double area = sizes.FractionalArea(0.1) - sizes.FractionalArea(2.0);
    const UnresolvedCover coarse = layer->UnresolvedCoverAt(5.0, 7.0, 100.0);
    EXPECT_NEAR(coarse.mean, area, 1e-4 * area) << "every boulder unresolved";
    EXPECT_GT(coarse.variance, 0.0);
    const UnresolvedCover finer = layer->UnresolvedCoverAt(5.0, 7.0, 10.0);
    EXPECT_NEAR(finer.mean, area, 1e-4 * area);
    EXPECT_NEAR(finer.variance, 100.0 * coarse.variance, 1e-6 * finer.variance)
        << "a hundredth of the area scatters a hundred times as much";
    const UnresolvedCover partial = layer->UnresolvedCoverAt(5.0, 7.0, 0.1);
    EXPECT_GT(partial.mean, 0.0);
    EXPECT_LT(partial.mean, area) << "the largest are resolved";
    EXPECT_EQ(layer->UnresolvedCoverAt(5.0, 7.0, 0.01), UnresolvedCover{}) << "all resolved";
    EXPECT_THROW(layer->UnresolvedCoverAt(5.0, 7.0, 0.0), std::invalid_argument);
    EXPECT_EQ(layer->UnresolvedCoverOver(RasterGrid{2, 2, GeoTransform::NorthUp(0.0, 0.02, 0.01)},
                                         0.01, nullptr, 1),
              std::vector<UnresolvedCover>(4))
        << "all resolved";

    const RasterGrid grid{6, 5, GeoTransform::NorthUp(-3.0, 2.5, 0.2)};
    const auto over = layer->UnresolvedCoverOver(grid, 0.2, nullptr, 2);
    ASSERT_EQ(over.size(), 30u);
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = grid.transform.PixelToWorld(static_cast<double>(col) + 0.5,
                                                            static_cast<double>(row) + 0.5);
            EXPECT_EQ(over[row * grid.width + col],
                      layer->UnresolvedCoverAt(center.x, center.y, 0.2));
        }
    }
}

TEST(RockAlbedo, BrightensSamplesByTheRockThatCoversThem) {
    const auto ground = std::make_shared<Level>();
    const auto layer = Boulders(0.1);
    RockAlbedoOptions options;
    options.unresolved = false;
    const auto resolvedOnly =
        std::make_shared<RockAlbedo>(options, std::vector<terraingen::RockSource>{{layer, ground}});
    const RasterGrid grid{64, 64, GeoTransform::NorthUp(-6.4, 6.4, 0.2)};
    const auto values = terraingen::EvaluateAlbedo(Gray(resolvedOnly), grid, {0.2});
    const auto cover = layer->MaterialCoverOver(grid, {0.2, 1, ground.get()});
    bool covered = false;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const double expected = static_cast<double>(0.1f) * (1.0 + 1.5 * cover(col, row));
            EXPECT_FLOAT_EQ(values[0](col, row), static_cast<float>(expected));
            covered = covered || cover(col, row) > 0.0;
        }
    }
    EXPECT_TRUE(covered);
    const auto center = values[0].CellCenter(13, 41);
    EXPECT_EQ(terraingen::EvaluateAlbedoAt(Gray(resolvedOnly), center.x, center.y, {0.2})[0],
              values[0](13, 41));

    const auto speckled = std::make_shared<RockAlbedo>(
        RockAlbedoOptions{}, std::vector<terraingen::RockSource>{{layer, ground}});
    const Albedo albedo = Gray(speckled);
    const auto coarse = terraingen::EvaluateAlbedo(albedo, grid, {0.2});
    terraingen::AlbedoContext onGround;
    onGround.spacing = 0.2;
    onGround.ground = ground.get();
    double brighter = 0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            brighter += coarse[0](col, row) > values[0](col, row) ? 1 : 0;
            if (row % 7 == 0 && col % 5 == 0) {
                const auto cell = coarse[0].CellCenter(col, row);
                ASSERT_EQ(terraingen::EvaluateAlbedoAt(albedo, cell.x, cell.y, onGround)[0],
                          coarse[0](col, row));
            }
        }
    }
    EXPECT_GT(brighter, 0.5 * 64 * 64) << "unresolved rock brightens nearly every sample a little";
}

TEST(RockAlbedo, RejectsBadOptionsAndSources) {
    RockAlbedoOptions options;
    options.reflectanceRatio = 0.0;
    EXPECT_THROW(terraingen::ValidateRockAlbedoOptions(options), std::invalid_argument);
    EXPECT_THROW(RockAlbedo({}, {{Boulders(), nullptr}}), std::invalid_argument);
    EXPECT_THROW(RockAlbedo({}, {{nullptr, std::make_shared<Level>()}}), std::invalid_argument);
    const RasterGrid grid{4, 4, GeoTransform::NorthUp(0.0, 4.0, 1.0)};
    const auto bare =
        std::make_shared<RockAlbedo>(RockAlbedoOptions{}, std::vector<terraingen::RockSource>{});
    EXPECT_FLOAT_EQ(terraingen::EvaluateAlbedo(Gray(bare), grid, {1.0})[0](1, 2), 0.1f)
        << "no boulder layers, no rock";
}
