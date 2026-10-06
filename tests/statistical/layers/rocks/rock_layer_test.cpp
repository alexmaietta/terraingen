#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <memory>

using terraingen::RockLayer;
using terraingen::RockLayerOptions;

namespace {

/// Expected rocks over `region` from the layer's coverage, summed over 1 m cells.
double ExpectedRocks(const RockLayer& layer, const terraingen::Bounds2d& region) {
    const RockLayerOptions& options = layer.Options();
    double expected = 0.0;
    for (double y = region.minY + 0.5; y < region.maxY; y += 1.0) {
        for (double x = region.minX + 0.5; x < region.maxX; x += 1.0) {
            const terraingen::GolombekRappSfd local(
                options.coverage * layer.AbundanceAt(x, y).background, options.qa, options.qb);
            expected += local.ExpectedCount(1.0, options.minDiameter, options.maxDiameter);
        }
    }
    return expected;
}

/// Rocks centred in `region`.
double CountRocks(const RockLayer& layer, const terraingen::Bounds2d& region) {
    double count = 0.0;
    for (const auto& rock : layer.Rocks(region)) {
        count += region.Contains(rock.x, rock.y) ? 1.0 : 0.0;
    }
    return count;
}

}  // namespace

TEST(RockLayerStatistics, CountsFollowGolombekAndRapp) {
    RockLayerOptions options;
    options.coverage = 0.03;
    options.minDiameter = 0.2;
    const RockLayer layer(options);
    const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
    const double expected =
        terraingen::GolombekRappSfd(0.03).ExpectedCount(300.0 * 300.0, 0.2, 4.0);
    EXPECT_NEAR(CountRocks(layer, region), expected, 4.0 * std::sqrt(expected));
}

TEST(RockLayerStatistics, ThinningFollowsTheLocalCoverageInPatches) {
    RockLayerOptions options;
    options.coverage = 0.03;
    options.minDiameter = 0.2;
    options.patches = {3.0, 40.0};
    const RockLayer layer(options);
    const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
    const double expected = ExpectedRocks(layer, region);
    EXPECT_NEAR(CountRocks(layer, region), expected, 4.0 * std::sqrt(expected));
    const double uniform = terraingen::GolombekRappSfd(0.03).ExpectedCount(300.0 * 300.0, 0.2, 4.0);
    EXPECT_GT(std::fabs(expected - uniform), 0.0);
}

TEST(RockLayerStatistics, CountsFollowAnyDistribution) {
    for (const auto& distribution :
         std::initializer_list<std::shared_ptr<terraingen::SizeFrequencyDistribution>>{
             terraingen::Sp8023IntercraterBlocks(terraingen::RockModelCurve::Nominal),
             terraingen::SynthterrainRockDistribution(terraingen::SynthterrainRocks::Haworth)}) {
        RockLayerOptions options;
        options.distribution = distribution;
        options.minDiameter = 0.2;
        const RockLayer layer(options);
        const terraingen::Bounds2d region{-150.0, -150.0, 150.0, 150.0};
        const double expected = distribution->ExpectedCount(300.0 * 300.0, 0.2, 4.0);
        ASSERT_GT(expected, 50.0);
        EXPECT_NEAR(CountRocks(layer, region), expected, 4.0 * std::sqrt(expected));
    }
}
