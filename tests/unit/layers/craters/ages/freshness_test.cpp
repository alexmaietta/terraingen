#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using terraingen::FreshnessAges;
using terraingen::RandomStream;
using terraingen::ValueDistribution;

namespace {

constexpr int kDraws = 40000;

/// The freshness of `kDraws` craters, read back from their ages.
std::vector<double> DrawFreshness(const FreshnessAges& ages) {
    RandomStream stream(7u, terraingen::kCraterAgeStream, 3u, 4u, 5u);
    std::vector<double> freshness;
    for (int n = 0; n < kDraws; ++n) {
        const double diameter = 5.0 + n % 300;
        freshness.push_back(ages.RemainingLifetime(diameter, ages.SampleAgeGyr(stream, diameter)));
    }
    return freshness;
}

double Mean(const std::vector<double>& values) {
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double StandardDeviation(const std::vector<double>& values) {
    const double mean = Mean(values);
    double sum = 0.0;
    for (const double value : values) {
        sum += (value - mean) * (value - mean);
    }
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

}  // namespace

TEST(FreshnessAges, NormalFreshnessHasTheRequestedMeanAndSpread) {
    const FreshnessAges ages({ValueDistribution::Kind::Normal, 0.35, 0.1}, 2.0);
    EXPECT_EQ(ages.Freshness().min, 0.0);
    EXPECT_EQ(ages.Freshness().max, 1.0);
    const std::vector<double> freshness = DrawFreshness(ages);
    EXPECT_NEAR(Mean(freshness), 0.35, 0.003);
    EXPECT_NEAR(StandardDeviation(freshness), 0.1, 0.003);
    EXPECT_GE(*std::min_element(freshness.begin(), freshness.end()), 0.0);
    EXPECT_LE(*std::max_element(freshness.begin(), freshness.end()), 1.0);
}

TEST(FreshnessAges, UniformFreshnessFillsItsBounds) {
    const FreshnessAges ages({ValueDistribution::Kind::Uniform, 0.2, 0.6}, 3.0);
    const std::vector<double> freshness = DrawFreshness(ages);
    const auto [low, high] = std::minmax_element(freshness.begin(), freshness.end());
    EXPECT_GE(*low, 0.2 - 1e-12);
    EXPECT_LT(*low, 0.201);
    EXPECT_LE(*high, 0.6 + 1e-12);
    EXPECT_GT(*high, 0.599);
    EXPECT_NEAR(Mean(freshness), 0.4, 0.003);
}

TEST(FreshnessAges, PiecewiseFreshnessGivesEachBinItsShare) {
    ValueDistribution shape{ValueDistribution::Kind::Piecewise};
    shape.edges = {0.0, 0.25, 0.5, 1.0};
    shape.weights = {1.0, 0.0, 3.0};
    const FreshnessAges ages(shape, 1.0);
    std::array<int, 3> counts{};
    for (const double f : DrawFreshness(ages)) {
        counts[f < 0.25 ? 0 : (f < 0.5 ? 1 : 2)] += 1;
    }
    EXPECT_NEAR(counts[0] / static_cast<double>(kDraws), 0.25, 0.007);
    EXPECT_LE(counts[1], 2) << "only rounding at the empty bin's edges";
    EXPECT_NEAR(counts[2] / static_cast<double>(kDraws), 0.75, 0.007);
}

TEST(FreshnessAges, AgesFallLinearlyWithFreshnessOverOneLifetime) {
    const FreshnessAges ages({ValueDistribution::Kind::Normal, 0.5, 0.3}, 2.5);
    RandomStream draws(9u, 2u);
    RandomStream copy(9u, 2u);
    for (int n = 0; n < 200; ++n) {
        const double diameter = 1.0 + 37.0 * n;
        const double expected = (1.0 - ages.SampleFreshness(copy)) * 2.5;
        EXPECT_DOUBLE_EQ(ages.SampleAgeGyr(draws, diameter), expected);
        EXPECT_EQ(ages.LifetimeGyr(diameter), 2.5);
    }
    const FreshnessAges fresh({ValueDistribution::Kind::Constant, 1.5}, 1.0);
    const FreshnessAges worn({ValueDistribution::Kind::Constant, -0.5}, 1.0);
    EXPECT_EQ(fresh.SampleAgeGyr(draws, 10.0), 0.0);
    EXPECT_EQ(worn.SampleAgeGyr(draws, 10.0), 1.0);
    EXPECT_EQ(worn.RemainingLifetime(10.0, 1.0), 0.0);
}

TEST(FreshnessAges, RejectsBadLifetimesAndDistributions) {
    const ValueDistribution uniform{ValueDistribution::Kind::Uniform, 0.0, 1.0};
    constexpr double kInf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(FreshnessAges(uniform, 0.0), std::invalid_argument);
    EXPECT_THROW(FreshnessAges(uniform, kInf), std::invalid_argument);
    EXPECT_THROW(FreshnessAges({ValueDistribution::Kind::Uniform, 0.6, 0.2}, 1.0),
                 std::invalid_argument);
    EXPECT_THROW(FreshnessAges({ValueDistribution::Kind::Normal, 0.5, 0.1, 2.0}, 1.0),
                 std::invalid_argument);
    EXPECT_THROW(FreshnessAges({ValueDistribution::Kind::Piecewise}, 1.0), std::invalid_argument);
    EXPECT_THROW(FreshnessAges(uniform, 1.0).LifetimeGyr(0.0), std::domain_error);
}

TEST(FreshnessAges, CraterLayersAgreeOnGridsTilesAndPoints) {
    const auto ages = std::make_shared<FreshnessAges>(
        ValueDistribution{ValueDistribution::Kind::Normal, 0.35, 0.15}, 2.0);
    const auto profile = std::make_shared<terraingen::FassettThomsonProfile>();
    terraingen::CraterLayerOptions options;
    options.seed = 11;
    options.generation.dMin = 6.0;
    options.generation.dMax = 60.0;
    const terraingen::CraterLayer layer({std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0), ages,
                                         std::make_shared<terraingen::FreshnessDegradation>(
                                             ages, terraingen::LunarFreshCraterMorphometry),
                                         profile},
                                        options);
    terraingen::Raster<float> whole(96, 96, 0.0f);
    whole.SetTransform(terraingen::GeoTransform::NorthUp(-96.0, 96.0, 2.0));
    layer.Apply(whole, {2.0, 0});
    terraingen::Raster<float> tile(40, 40, 0.0f);
    tile.SetTransform(
        terraingen::GeoTransform::NorthUp(-96.0 + 48.0 * 2.0, 96.0 - 48.0 * 2.0, 2.0));
    layer.Apply(tile, {2.0, 1});
    for (std::size_t row = 0; row < 40; ++row) {
        for (std::size_t col = 0; col < 40; ++col) {
            EXPECT_NEAR(tile(col, row), whole(col + 48, row + 48), 1e-5f);
        }
    }
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{10, 10}, std::pair<std::size_t, std::size_t>{50, 70},
          std::pair<std::size_t, std::size_t>{95, 30}}) {
        const auto centre = whole.CellCenter(col, row);
        EXPECT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {2.0}), whole(col, row));
    }
    const auto all = layer.Craters(terraingen::RasterGrid::FromRaster(whole).Bounds(), 2.0);
    const auto part = layer.Craters(terraingen::RasterGrid::FromRaster(tile).Bounds(), 2.0);
    ASSERT_GT(part.Size(), 3u);
    std::size_t matched = 0;
    for (std::size_t i = 0; i < part.Size(); ++i) {
        for (std::size_t j = 0; j < all.Size(); ++j) {
            if (all.At(j).id == part.At(i).id) {
                EXPECT_EQ(all.At(j).ageGyr, part.At(i).ageGyr);
                matched += 1;
            }
        }
        const double f = layer.Attributes(part.At(i)).freshness;
        EXPECT_GE(f, 0.0);
        EXPECT_LE(f, 1.0);
    }
    EXPECT_EQ(matched, part.Size());
}
