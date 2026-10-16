#include "terraingen/layers/boulders/boulder_fragments.h"

#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

using terraingen::Boulder;
using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;
using terraingen::FragmentBoulderShape;
using terraingen::GeoTransform;
using terraingen::Raster;

namespace {

/// Options whose boulders between craters all break: 0.3 to 2 m across, split from wear 0.
BoulderLayerOptions Breaking(double rubbleWear) {
    BoulderLayerOptions options;
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.3;
    options.sizes.maxDiameter = 2.0;
    options.fragmentation.minDiameter = 0.3;
    options.fragmentation.splitWear = 0.0;
    options.fragmentation.rubbleWear = rubbleWear;
    options.rendering.relief = terraingen::BoulderRelief::Shape;
    return options;
}

}  // namespace

TEST(FragmentBoulderShape, SplitsAShapeIntoPiecesThatFillIt) {
    const auto parent = terraingen::GenerateBoulderShape(7, {});
    const double whole = terraingen::Volume(parent.mesh);
    for (const int pieces : {2, 3, 5, 12}) {
        const auto fragments = FragmentBoulderShape(parent, pieces, 7);
        ASSERT_EQ(fragments.size(), static_cast<std::size_t>(pieces));
        const auto box = terraingen::Bounds(parent.mesh);
        double total = 0.0;
        for (const auto& fragment : fragments) {
            total += terraingen::Volume(fragment.shape.mesh) * std::pow(fragment.scale, 3.0);
            for (const auto& p : fragment.shape.mesh.positions) {
                const auto q = fragment.centre + p * fragment.scale;
                ASSERT_GE(q.x, box.min.x - 1e-9);
                ASSERT_LE(q.x, box.max.x + 1e-9);
                ASSERT_GE(q.z, box.min.z - 1e-9);
                ASSERT_LE(q.z, box.max.z + 1e-9);
            }
            const auto extents = terraingen::Bounds(fragment.shape.mesh).Extents();
            EXPECT_NEAR(std::fmax(extents.x, extents.y), 1.0, 1e-9) << "scaled to its extent";
            EXPECT_LT(fragment.scale, 1.0 + 1e-9);
        }
        EXPECT_NEAR(total / whole, 1.0, 0.1) << pieces << " pieces fill the boulder";
    }
    EXPECT_EQ(FragmentBoulderShape(parent, 3, 7)[1].shape.mesh.positions,
              FragmentBoulderShape(parent, 3, 7)[1].shape.mesh.positions);
    EXPECT_THROW(FragmentBoulderShape(parent, 1, 7), std::invalid_argument);
    EXPECT_THROW(FragmentBoulderShape(parent, 13, 7), std::invalid_argument);
}

TEST(BoulderLayer, BreaksWornBouldersIntoPiecesThatReplaceThem) {
    const BoulderLayer whole([] {
        BoulderLayerOptions options = Breaking(1.0);
        options.fragmentation.splitWear = 1.0;
        return options;
    }());
    const BoulderLayer broken(Breaking(1.0));
    const terraingen::Bounds2d region{-30.0, -30.0, 30.0, 30.0};
    const auto parents = whole.Boulders(region);
    ASSERT_GT(parents.size(), 10u);
    std::map<std::uint64_t, std::vector<Boulder>> byParent;
    for (const Boulder& piece : broken.Boulders(region)) {
        ASSERT_NE(piece.parent, 0u) << "every boulder here breaks";
        byParent[piece.parent].push_back(piece);
    }
    std::size_t matched = 0;
    for (const Boulder& parent : parents) {
        const auto found = byParent.find(parent.id);
        if (found == byParent.end() || std::fabs(parent.x) > 25.0 || std::fabs(parent.y) > 25.0) {
            continue;
        }
        ++matched;
        const auto& pieces = found->second;
        EXPECT_GE(pieces.size(), 2u);
        EXPECT_LE(pieces.size(), 5u);
        for (const Boulder& piece : pieces) {
            EXPECT_LT(piece.diameter, parent.diameter + 1e-9);
            EXPECT_LT(std::hypot(piece.x - parent.x, piece.y - parent.y),
                      (0.5 + 0.04) * parent.diameter + 1e-9)
                << "split pieces stay where their boulder was";
            EXPECT_EQ(piece.wear, parent.wear);
        }
    }
    EXPECT_GT(matched, parents.size() / 2);

    const terraingen::Bounds2d inner{-10.0, -10.0, 10.0, 10.0};
    std::set<std::uint64_t> fromInner;
    for (const Boulder& piece : broken.Boulders(inner)) {
        fromInner.insert(piece.id);
    }
    for (const Boulder& piece : broken.Boulders(region)) {
        if (std::fabs(piece.x) < 9.0 && std::fabs(piece.y) < 9.0) {
            EXPECT_TRUE(fromInner.count(piece.id)) << "the same pieces in every region";
        }
    }
}

TEST(BoulderLayer, ScattersRubbleFurtherAndSinksIt) {
    const BoulderLayer split(Breaking(1.0));
    const BoulderLayer rubble(Breaking(0.0));
    const terraingen::Bounds2d region{-30.0, -30.0, 30.0, 30.0};
    double splitSpread = 0.0;
    double rubbleSpread = 0.0;
    std::map<std::uint64_t, Boulder> splitPieces;
    for (const Boulder& piece : split.Boulders(region)) {
        splitPieces[piece.id] = piece;
    }
    std::size_t compared = 0;
    for (const Boulder& piece : rubble.Boulders(region)) {
        const auto found = splitPieces.find(piece.id);
        if (found == splitPieces.end()) {
            continue;
        }
        ++compared;
        EXPECT_GE(piece.burial, found->second.burial - 1e-12);
        splitSpread += std::hypot(found->second.x, found->second.y);
        rubbleSpread += std::hypot(piece.x, piece.y);
    }
    EXPECT_GT(compared, 10u);
    EXPECT_NE(splitSpread, rubbleSpread);
}

TEST(BoulderLayer, DrawsPiecesThatPointQueriesMatch) {
    const BoulderLayer layer(Breaking(1.0));
    Raster<float> heights(60, 60, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(-7.5, 7.5, 0.25));
    layer.Apply(heights, {0.25, 4});
    bool raised = false;
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto centre = heights.CellCenter(col, row);
            ASSERT_EQ(layer.ApplyAt(1.0f, centre.x, centre.y, {0.25}), heights(col, row));
            raised = raised || heights(col, row) > 1.0f;
        }
    }
    EXPECT_TRUE(raised);
}

TEST(BoulderLayer, GathersBouldersInClustersKeepingTheirMeanDensity) {
    BoulderLayerOptions options;
    options.clusters = {.share = 0.8, .spacing = 40.0, .radius = 4.0};
    const BoulderLayer layer(options);
    double sum = 0.0;
    double most = 0.0;
    double least = 1e9;
    int samples = 0;
    for (double y = 0.5; y < 400.0; y += 1.0) {
        for (double x = 0.5; x < 400.0; x += 1.0) {
            const double background = layer.AbundanceAt(x, y).background;
            sum += background;
            most = std::fmax(most, background);
            least = std::fmin(least, background);
            ++samples;
        }
    }
    EXPECT_NEAR(sum / samples, 1.0, 0.05) << "the mean density is kept";
    EXPECT_GT(most, 5.0);
    EXPECT_LT(least, 0.25);
    EXPECT_LE(most, terraingen::MaxClusterAbundance(options.clusters) + 1e-9);
    options.clusters.radius = 50.0;
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
    options.clusters = {};
    options.fragmentation = {.splitWear = 0.5, .rubbleWear = 0.2};
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
    options.fragmentation = {.minPieces = 1};
    EXPECT_THROW(terraingen::ValidateBoulderFragmentation(options.fragmentation),
                 std::invalid_argument);
}
