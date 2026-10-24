// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_layer.h"

#include "support/boulder_records.h"
#include "support/golombek_rapp.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

using terraingen::Boulder;
using terraingen::BoulderLayer;
using terraingen::BoulderLayerOptions;
using terraingen::BoulderPlacement;
using terraingen::BoulderRest;
using terraingen::Bounds2d;
using terraingen::ManualBoulder;

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
const Bounds2d kRegion{-15.0, -15.0, 15.0, 15.0};

/// Ground rising `rise` meters per meter eastward from 3 m at x = 0.
class Ramp final : public terraingen::Ground {
  public:
    explicit Ramp(double rise = 0.0) : rise_(rise) {}
    float HeightAt(double x, double /*y*/, double /*spacing*/) const override {
        return static_cast<float>(3.0 + rise_ * x);
    }

  private:
    double rise_;
};

/// Boulders that rest on sides and ends and tilt, those from 1 m placed as models.
BoulderLayerOptions Options() {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.05;
    options.sizes.minDiameter = 0.3;
    options.pose.sideShare = 0.2;
    options.pose.endShare = 0.1;
    options.pose.maxTilt = 0.4;
    options.rendering.models.minDiameter = 1.0;
    return options;
}

/// A manual boulder fixing nothing.
ManualBoulder Plain(double x, double y, double diameter) {
    ManualBoulder boulder;
    boulder.x = x;
    boulder.y = y;
    boulder.diameter = diameter;
    return boulder;
}

/// A manual boulder fixing every field.
ManualBoulder Posed() {
    ManualBoulder boulder = Plain(-6.0, 5.0, 1.5);
    boulder.id = 99;
    boulder.height = 0.9;
    boulder.burial = 0.35;
    boulder.yaw = 0.5;
    boulder.lean = 0.2;
    boulder.tilt = 0.3;
    boulder.tiltAzimuth = -1.0;
    boulder.wear = 0.6;
    boulder.rest = BoulderRest::End;
    boulder.shapeSeed = 5u;
    return boulder;
}

/// The boulder of ID `id` in `boulders`, or null.
const Boulder* Find(const std::vector<Boulder>& boulders, std::uint64_t id) {
    for (const Boulder& boulder : boulders) {
        if (boulder.id == id) {
            return &boulder;
        }
    }
    return nullptr;
}

/// The boulders of `boulders` whose IDs are none of `ids`.
std::vector<Boulder> Without(const std::vector<Boulder>& boulders,
                             const std::vector<std::uint64_t>& ids) {
    std::vector<Boulder> kept;
    for (const Boulder& boulder : boulders) {
        bool listed = false;
        for (const std::uint64_t id : ids) {
            listed = listed || boulder.id == id;
        }
        if (!listed) {
            kept.push_back(boulder);
        }
    }
    return kept;
}

/// Whether two placements are bit for bit the same.
bool SamePlacement(const BoulderPlacement& a, const BoulderPlacement& b) {
    return a.scale == b.scale && a.pose.rotation == b.pose.rotation &&
           a.pose.translation == b.pose.translation;
}

/// The model of the boulder of ID `id`; fails the test when there is none.
terraingen::BoulderModel ModelWithId(const std::vector<terraingen::BoulderModel>& models,
                                     std::uint64_t id) {
    for (const auto& model : models) {
        if (model.boulder.id == id) {
            return model;
        }
    }
    ADD_FAILURE() << "no model of boulder " << id;
    return {};
}

}  // namespace

TEST(ManualBoulder, FixingWhatAnUnsetOneDrewGivesTheSameBoulderAndLeavesTheRestAlone) {
    BoulderLayerOptions options = Options();
    options.manual = {Plain(2.0, 3.0, 2.5), Plain(-4.0, 1.0, 0.8)};
    const Ramp level;
    const auto drawn =
        terraingen::testing::Records(BoulderLayer(options).Boulders(kRegion, 0.0, &level));
    const std::uint64_t id = terraingen::ManualCraterId(2.0, 3.0, 2.5);
    const Boulder* plain = Find(drawn, id);
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->x, 2.0);
    EXPECT_EQ(plain->diameter, 2.5);
    EXPECT_FALSE(plain->shapeSeed.has_value());

    ManualBoulder fixed = options.manual[0];
    fixed.height = plain->height;
    fixed.burial = plain->burial;
    fixed.yaw = plain->yaw;
    fixed.lean = plain->lean;
    fixed.tilt = plain->tilt;
    fixed.tiltAzimuth = plain->tiltAzimuth;
    fixed.wear = plain->wear;
    fixed.rest = plain->rest;
    const std::uint64_t seed = terraingen::ShapeSeed(*plain, options.rendering.shapePrototypes);
    fixed.shapeSeed = seed;
    BoulderLayerOptions fixing = options;
    fixing.manual[0] = fixed;
    const auto posed =
        terraingen::testing::Records(BoulderLayer(fixing).Boulders(kRegion, 0.0, &level));
    const Boulder* again = Find(posed, id);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->shapeSeed, std::optional<std::uint64_t>(seed));
    Boulder same = *again;
    same.shapeSeed.reset();
    EXPECT_EQ(same, *plain) << "the same ID and every field as drawn";
    const std::vector<std::uint64_t> manual{id, terraingen::ManualCraterId(-4.0, 1.0, 0.8)};
    EXPECT_GT(Without(drawn, manual).size(), 20u);
    EXPECT_EQ(Without(posed, manual), Without(drawn, manual))
        << "generated boulders bit for bit the same";
}

TEST(ManualBoulder, PlacesEveryFixedFieldExactlyInTheListsAndModels) {
    BoulderLayerOptions options = Options();
    options.manual = {Posed()};
    const BoulderLayer layer(options);
    const Ramp level;
    const auto boulders = terraingen::testing::Records(layer.Boulders(kRegion, 0.0, &level));
    const Boulder* b = Find(boulders, 99);
    ASSERT_NE(b, nullptr);
    const ManualBoulder posed = Posed();
    EXPECT_EQ(b->x, posed.x);
    EXPECT_EQ(b->y, posed.y);
    EXPECT_EQ(b->diameter, posed.diameter);
    EXPECT_EQ(b->height, posed.height);
    EXPECT_EQ(b->burial, posed.burial);
    EXPECT_EQ(b->yaw, posed.yaw);
    EXPECT_EQ(b->lean, posed.lean);
    EXPECT_EQ(b->tilt, posed.tilt);
    EXPECT_EQ(b->tiltAzimuth, posed.tiltAzimuth);
    EXPECT_EQ(b->wear, posed.wear);
    EXPECT_EQ(b->rest, BoulderRest::End);
    EXPECT_EQ(b->shapeSeed, posed.shapeSeed);
    EXPECT_TRUE(b->model);
    const terraingen::BoulderModel model = ModelWithId(layer.Models(boulders), 99);
    EXPECT_EQ(model.shapeSeed, 5u);
    const terraingen::BoulderShape shape =
        terraingen::GenerateBoulderShape(5, options.rendering.shapes);
    EXPECT_EQ(model.shape->mesh.positions, shape.mesh.positions);
    EXPECT_TRUE(
        SamePlacement(model.placement, terraingen::PlaceBoulderModel(*b, shape, 3.0, false)))
        << "placed as fixed, not tipped over";
}

TEST(ManualBoulder, FixedOrientationIsNotTippedOverButOtherManualBouldersAre) {
    BoulderLayerOptions options = Options();
    ManualBoulder leaning = Plain(4.0, -4.0, 1.5);
    leaning.lean = 1.2;
    ManualBoulder resting = Plain(-4.0, -4.0, 1.5);
    resting.rest = BoulderRest::End;
    ManualBoulder tall = Plain(0.0, -8.0, 1.5);
    tall.height = 1.4;
    options.manual = {leaning, resting, tall};
    ASSERT_TRUE(options.pose.settle);
    const BoulderLayer layer(options);
    const Ramp level;
    const auto boulders = terraingen::testing::Records(layer.Boulders(kRegion, 0.0, &level));
    const auto models = layer.Models(boulders);
    for (const ManualBoulder& manual : options.manual) {
        const std::uint64_t id = terraingen::ManualBoulderId(manual);
        const Boulder* boulder = Find(boulders, id);
        ASSERT_NE(boulder, nullptr);
        const terraingen::BoulderModel model = ModelWithId(models, id);
        const bool settles = !manual.FixesOrientation();
        EXPECT_TRUE(SamePlacement(
            model.placement,
            terraingen::PlaceBoulderModel(*boulder, *layer.Shape(*boulder), 3.0, settles)))
            << "settled: " << settles;
    }
    EXPECT_EQ(Find(boulders, terraingen::ManualBoulderId(leaning))->lean, 1.2);
}

TEST(ManualBoulder, ShapeSeedPicksTheShapeAGeneratedBoulderDerives) {
    BoulderLayerOptions options = Options();
    options.rendering.shapePrototypes = 0;
    const BoulderLayer plain(options);
    const auto generated = terraingen::testing::Records(plain.Boulders(kRegion));
    ASSERT_FALSE(generated.empty());
    const Boulder& g = generated.front();
    const std::uint64_t seed = terraingen::ShapeSeed(g, 0);
    EXPECT_EQ(seed, g.id);
    ManualBoulder manual = Plain(1.0, 1.0, 2.0);
    manual.shapeSeed = seed;
    options.manual = {manual};
    const BoulderLayer layer(options);
    const auto boulders = terraingen::testing::Records(layer.Boulders(kRegion));
    const Boulder* placed = Find(boulders, terraingen::ManualBoulderId(manual));
    ASSERT_NE(placed, nullptr);
    EXPECT_EQ(layer.Shape(*placed)->mesh.positions, plain.Shape(g)->mesh.positions);
    Boulder unseeded = *placed;
    unseeded.shapeSeed.reset();
    EXPECT_NE(layer.Shape(unseeded)->mesh.positions, plain.Shape(g)->mesh.positions);
}

TEST(ManualBoulder, BouldersFixingAnyFieldNeverBreak) {
    BoulderLayerOptions options = Options();
    options.fragmentation.minDiameter = 0.5;
    options.fragmentation.splitWear = 0.0;
    ManualBoulder worn = Plain(5.0, 5.0, 2.0);
    worn.wear = 0.9;
    const ManualBoulder free = Plain(-5.0, 5.0, 2.0);
    options.manual = {worn, free};
    const auto boulders = terraingen::testing::Records(BoulderLayer(options).Boulders(kRegion));
    const Boulder* whole = Find(boulders, terraingen::ManualBoulderId(worn));
    ASSERT_NE(whole, nullptr);
    EXPECT_EQ(whole->parent, 0u);
    EXPECT_EQ(whole->wear, 0.9);
    const std::uint64_t broken = terraingen::ManualBoulderId(free);
    EXPECT_EQ(Find(boulders, broken), nullptr);
    std::size_t pieces = 0;
    for (const Boulder& boulder : boulders) {
        pieces += boulder.parent == broken ? 1 : 0;
    }
    EXPECT_GE(pieces, 2u) << "a manual boulder fixing nothing breaks as generated ones do";
}

TEST(ManualBoulder, AFollowedSlopeTipsAFixedTilt) {
    BoulderLayerOptions options = Options();
    options.pose.followSlope = true;
    ManualBoulder upright = Plain(0.0, 0.0, 1.0);
    upright.tilt = 0.0;
    upright.tiltAzimuth = 0.0;
    options.manual = {upright};
    const Ramp ramp(0.2);
    const auto boulders =
        terraingen::testing::Records(BoulderLayer(options).Boulders(kRegion, 0.0, &ramp));
    const Boulder* b = Find(boulders, terraingen::ManualBoulderId(upright));
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->tilt, std::atan(0.2), 1e-6);
    EXPECT_NEAR(std::fabs(b->tiltAzimuth), terraingen::kPi, 1e-6) << "tipped downhill";
}

TEST(ManualBoulder, AFixedWearSetsTheBurialDrawnByWear) {
    BoulderLayerOptions options = Options();
    options.pose.burialByWear = 1.0;
    ManualBoulder worn = Plain(0.0, 0.0, 1.0);
    worn.wear = 0.5;
    options.manual = {worn};
    const auto boulders = terraingen::testing::Records(BoulderLayer(options).Boulders(kRegion));
    const Boulder* b = Find(boulders, terraingen::ManualBoulderId(worn));
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->wear, 0.5);
    EXPECT_NEAR(b->burial, 0.5 * (options.pose.minBurial + options.pose.maxBurial), 1e-12);
}

TEST(ManualBoulder, ComparesUnsetFieldsAsEqualAndSaysWhatItFixes) {
    const ManualBoulder plain = Plain(1.0, 2.0, 3.0);
    EXPECT_EQ(plain, Plain(1.0, 2.0, 3.0));
    EXPECT_FALSE(plain.FixesAny());
    EXPECT_NE(plain, Posed());
    EXPECT_TRUE(Posed().FixesOrientation());
    ManualBoulder sized = plain;
    sized.height = 1.0;
    EXPECT_TRUE(sized.FixesAny());
    EXPECT_FALSE(sized.FixesOrientation());
    EXPECT_NE(sized, plain);
    ManualBoulder seeded = plain;
    seeded.shapeSeed = 0u;
    EXPECT_TRUE(seeded.FixesAny());
    ManualBoulder azimuth = plain;
    azimuth.tiltAzimuth = 0.0;
    EXPECT_TRUE(azimuth.FixesOrientation());
    EXPECT_EQ(terraingen::ManualBoulderId(plain), terraingen::ManualCraterId(1.0, 2.0, 3.0));
    EXPECT_EQ(terraingen::ManualBoulderId(Posed()), 99u);
}

TEST(ManualOnlyBoulderLayer, PlacesTheManualBouldersAsAGeneratingLayerDoesAndNothingElse) {
    EXPECT_TRUE(BoulderLayerOptions{}.generateRandom);
    BoulderLayerOptions options = Options();
    options.manual = {Posed(), Plain(2.0, 3.0, 2.5), Plain(-4.0, 1.0, 0.8), Plain(40.0, 40.0, 1.0)};
    const Ramp ramp(0.1);
    const auto generating =
        terraingen::testing::Records(BoulderLayer(options).Boulders(kRegion, 0.0, &ramp));
    BoulderLayerOptions manualOnly = options;
    manualOnly.generateRandom = false;
    const BoulderLayer layer(manualOnly);
    const auto boulders = terraingen::testing::Records(layer.Boulders(kRegion, 0.0, &ramp));
    ASSERT_EQ(boulders.size(), 3u) << "the manual boulder far outside the region is left out";
    EXPECT_GT(generating.size(), 20u);
    for (const Boulder& boulder : boulders) {
        const Boulder* drawn = Find(generating, boulder.id);
        ASSERT_NE(drawn, nullptr);
        EXPECT_EQ(boulder, *drawn) << "posed and placed bit for bit as with random boulders";
    }
    const auto models = layer.Models(boulders);
    ASSERT_EQ(models.size(), 2u) << "the boulders from 1 m";
    for (const auto& model : models) {
        EXPECT_TRUE(SamePlacement(
            model.placement,
            ModelWithId(BoulderLayer(options).Models(generating), model.boulder.id).placement));
    }
    EXPECT_EQ(layer.MaxFeatureDiameter(), 2.5);
    EXPECT_TRUE(terraingen::testing::Records(layer.Boulders({-30.0, 20.0, -20.0, 30.0})).empty());
}

TEST(ManualOnlyBoulderLayer, NeedsNoSizesCraterLayerOrGroundAndHasNoAbundance) {
    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.generateRandom = false;
    options.sizes.coverage = 0.0;
    options.sizes.maxDiameter = 0.01;
    options.ejecta.minCraterDiameter = 5.0;
    options.bedrock.exposure = terraingen::ExposureParams{};
    options.manual = {Plain(0.0, 0.0, 1.0)};
    EXPECT_NO_THROW(terraingen::ValidateBoulderLayerOptions(options));
    EXPECT_THROW(terraingen::BoulderCandidates(options), std::invalid_argument);
    const BoulderLayer layer(options);
    EXPECT_EQ(terraingen::testing::Records(layer.Boulders(kRegion)).size(), 1u)
        << "no ground needed for the exposure field";
    EXPECT_EQ(layer.AbundanceAt(1.0, 1.0), (terraingen::BoulderAbundance{0.0, 0.0, 0.0, {}}));
    EXPECT_EQ(layer.UnresolvedCoverAt(1.0, 1.0, 0.5), terraingen::UnresolvedCover{});
    const terraingen::RasterGrid grid{8, 8, terraingen::GeoTransform::NorthUp(-2.0, 2.0, 0.5)};
    for (const auto& cover : layer.UnresolvedCoverOver(grid, 0.5, nullptr, 1)) {
        EXPECT_EQ(cover, terraingen::UnresolvedCover{});
    }
    EXPECT_GT(layer.MaterialCoverAt(0.0, 0.0, 0.1, nullptr), 0.0) << "the manual boulder's rock";
    EXPECT_EQ(layer.MaterialCoverAt(1.5, 1.5, 0.1, nullptr), 0.0);
    options.generateRandom = true;
    EXPECT_THROW(terraingen::ValidateBoulderLayerOptions(options), std::invalid_argument);
}

TEST(ManualOnlyBoulderLayer, DrawsTheManualBouldersIntoTheHeights) {
    terraingen::Raster<float> heights(41, 41, 0.0f);
    heights.SetTransform(terraingen::GeoTransform::NorthUp(-2.05, 2.05, 0.1));
    BoulderLayer::Manual({Plain(0.0, 0.0, 1.0)})->Apply(heights, {0.1});
    EXPECT_GT(heights(20, 20), 0.2f) << "the boulder at the center";
    EXPECT_EQ(heights(0, 0), 0.0f) << "and nothing else";
}

TEST(ManualOnlyBoulderLayer, ManualMakesALayerOfOnlyTheGivenBoulders) {
    BoulderLayerOptions options = Options();
    const std::vector<ManualBoulder> boulders{Posed(), Plain(2.0, 3.0, 2.5)};
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const auto layer = BoulderLayer::Manual(boulders, options, moon);
    BoulderLayerOptions listed = options;
    listed.manual = {Plain(9.0, 9.0, 1.0)};
    EXPECT_THROW(BoulderLayer::Manual(boulders, listed), std::invalid_argument)
        << "boulders in options.manual would be dropped";
    EXPECT_FALSE(layer->Options().generateRandom);
    EXPECT_EQ(layer->Options().manual, boulders);
    EXPECT_EQ(layer->Options().rendering, options.rendering);
    EXPECT_EQ(layer->Options().pose, options.pose);
    const auto plain = BoulderLayer::Manual(boulders);
    EXPECT_EQ(plain->Options().rendering, terraingen::BoulderRendering{});
    EXPECT_EQ(terraingen::testing::Records(plain->Boulders(kRegion)).size(), 2u);
    EXPECT_THROW(BoulderLayer::Manual({Plain(1.0, 2.0, 3.0), Plain(1.0, 2.0, 3.0)}),
                 std::invalid_argument);
}

TEST(ManualBoulder, RejectsInvalidFieldsAndSharedIds) {
    EXPECT_NO_THROW(terraingen::ValidateManualBoulder(Plain(1.0, 2.0, 3.0)));
    EXPECT_NO_THROW(terraingen::ValidateManualBoulder(Posed()));
    const auto invalid = [](auto change) {
        ManualBoulder boulder = Posed();
        change(boulder);
        EXPECT_THROW(terraingen::ValidateManualBoulder(boulder), std::invalid_argument);
        BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
        options.manual = {boulder};
        EXPECT_THROW(BoulderLayer{options}, std::invalid_argument);
    };
    invalid([](ManualBoulder& b) { b.x = kNan; });
    invalid([](ManualBoulder& b) { b.diameter = 0.0; });
    invalid([](ManualBoulder& b) { b.diameter = std::numeric_limits<double>::infinity(); });
    invalid([](ManualBoulder& b) { b.height = -1.0; });
    invalid([](ManualBoulder& b) { b.height = std::numeric_limits<double>::infinity(); });
    invalid([](ManualBoulder& b) { b.burial = 1.0; });
    invalid([](ManualBoulder& b) { b.burial = -0.1; });
    invalid([](ManualBoulder& b) { b.yaw = std::numeric_limits<double>::infinity(); });
    invalid([](ManualBoulder& b) { b.lean = -std::numeric_limits<double>::infinity(); });
    invalid([](ManualBoulder& b) { b.tilt = std::numeric_limits<double>::infinity(); });
    invalid([](ManualBoulder& b) { b.tiltAzimuth = std::numeric_limits<double>::infinity(); });
    invalid([](ManualBoulder& b) { b.wear = 1.5; });

    BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.manual = {Plain(1.0, 2.0, 3.0), Plain(1.0, 2.0, 3.0)};
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument) << "one derived ID";
    ManualBoulder other = Plain(5.0, 5.0, 1.0);
    other.id = 99;
    options.manual = {Posed(), other};
    EXPECT_THROW(BoulderLayer{options}, std::invalid_argument) << "one given ID";
    options.manual[1].id = 100;
    EXPECT_NO_THROW(BoulderLayer{options});
}
