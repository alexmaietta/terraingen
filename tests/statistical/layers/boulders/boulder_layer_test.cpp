// SPDX-License-Identifier: BSD-3-Clause

#include "support/boulder_records.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <memory>

using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;

namespace {

/// Expected boulders over `region` from the layer's coverage, summed over 1 m cells.
double ExpectedBoulders(const BoulderLayer& layer, const terraingen::Bounds2d& region) {
    const terraingen::BoulderSizes& sizes = layer.Options().sizes;
    double expected = 0.0;
    for (double y = region.minY + 0.5; y < region.maxY; y += 1.0) {
        for (double x = region.minX + 0.5; x < region.maxX; x += 1.0) {
            const terraingen::GolombekRappSfd local(sizes.coverage *
                                                        layer.AbundanceAt(x, y).background,
                                                    sizes.qConstant, sizes.qCoverageCoefficient);
            expected += local.ExpectedCount(1.0, sizes.minDiameter, sizes.maxDiameter);
        }
    }
    return expected;
}

/// Boulders centered in `region`.
double CountBoulders(const BoulderLayer& layer, const terraingen::Bounds2d& region) {
    double count = 0.0;
    for (const auto& boulder : terraingen::testing::Records(layer.Boulders(region))) {
        count += region.Contains(boulder.x, boulder.y) ? 1.0 : 0.0;
    }
    return count;
}

}  // namespace

TEST(BoulderLayerStatistics, CountsFollowGolombekAndRapp) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.03;
    options.sizes.minDiameter = 0.2;
    const BoulderLayer layer(options);
    const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
    const double expected =
        terraingen::GolombekRappSfd(0.03).ExpectedCount(300.0 * 300.0, 0.2, 4.0);
    EXPECT_NEAR(CountBoulders(layer, region), expected, 4.0 * std::sqrt(expected));
}

TEST(BoulderLayerStatistics, ThinningFollowsTheLocalCoverageInPatches) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.03;
    options.sizes.minDiameter = 0.2;
    options.patches = {3.0, 40.0};
    const BoulderLayer layer(options);
    const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
    const double expected = ExpectedBoulders(layer, region);
    EXPECT_NEAR(CountBoulders(layer, region), expected, 4.0 * std::sqrt(expected));
    const double uniform = terraingen::GolombekRappSfd(0.03).ExpectedCount(300.0 * 300.0, 0.2, 4.0);
    EXPECT_GT(std::fabs(expected - uniform), 0.0);
}

TEST(BoulderLayerStatistics, CountsFollowAnyDistribution) {
    for (const auto& distribution :
         std::initializer_list<std::shared_ptr<terraingen::SizeFrequencyDistribution>>{
             std::make_shared<terraingen::TabulatedSfd>(
                 terraingen::Sp8023IntercraterBlocks(terraingen::SpecificationCurve::Nominal)),
             std::make_shared<terraingen::PowerLawSfd>(terraingen::SynthterrainBoulderDistribution(
                 terraingen::SynthterrainBoulders::Haworth))}) {
        BoulderLayerOptions options;
        options.sizes.distribution = distribution;
        options.sizes.minDiameter = 0.2;
        const BoulderLayer layer(options);
        const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
        const double expected = distribution->ExpectedCount(300.0 * 300.0, 0.2, 4.0);
        ASSERT_GT(expected, 50.0);
        EXPECT_NEAR(CountBoulders(layer, region), expected, 4.0 * std::sqrt(expected));
    }
}

TEST(BoulderLayerStatistics, ClustersKeepTheCountOfADistribution) {
    const auto distribution = std::make_shared<terraingen::TabulatedSfd>(
        terraingen::Sp8023IntercraterBlocks(terraingen::SpecificationCurve::Nominal));
    BoulderLayerOptions options;
    options.sizes.distribution = distribution;
    options.sizes.minDiameter = 0.2;
    options.clusters.share = 0.7;
    options.clusters.spacing = 30.0;
    options.clusters.radius = 4.0;
    const BoulderLayer layer(options);
    const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
    const double expected = distribution->ExpectedCount(300.0 * 300.0, 0.2, 4.0);
    EXPECT_NEAR(CountBoulders(layer, region), expected,
                4.0 * std::sqrt(expected) + 0.03 * expected);
}
