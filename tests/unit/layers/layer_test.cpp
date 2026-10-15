#include "terraingen/layers/layer.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace {

/// Adds 1 to every cell and records one crater.
class Plus final : public terraingen::Layer {
  public:
    void Apply(terraingen::Raster<float>& heights, const terraingen::LayerContext& /*context*/,
               terraingen::TerrainFeatures& features) const override {
        for (float& value : heights.Values()) {
            value += 1.0f;
        }
        features.craters.Append({0.0, 0.0, 1.0, 0.0, 1u});
    }
};

/// Ground at height 2 everywhere.
class Level final : public terraingen::Ground {
  public:
    float HeightAt(double /*x*/, double /*y*/, double /*spacing*/) const override {
        return 2.0f;
    }
};

}  // namespace

TEST(ValidatePointContext, NeedsAPositiveFiniteSpacing) {
    EXPECT_NO_THROW(terraingen::ValidatePointContext({.spacing = 0.5}));
    EXPECT_THROW(terraingen::ValidatePointContext({}), std::invalid_argument);
    EXPECT_THROW(
        terraingen::ValidatePointContext({.spacing = std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
}

TEST(RequireGround, NamesWhatNeedsIt) {
    const Level level;
    EXPECT_NO_THROW(terraingen::RequireGround(&level, "Rocks"));
    try {
        terraingen::RequireGround(nullptr, "Rocks");
        ADD_FAILURE() << "accepted no ground";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()), "Rocks need the ground");
    }
    EXPECT_EQ(level.Sampler(1.0)(3.0, 4.0), 2.0f);
}

TEST(Layer, AppliesToOneCellForPointQueriesByDefault) {
    const Plus plus;
    EXPECT_EQ(plus.ApplyAt(1.5f, 10.0, 20.0, {.spacing = 2.0}), 2.5f);
    EXPECT_EQ(plus.MaxFeatureDiameter(), 0.0);
    EXPECT_THROW(plus.ApplyAt(1.5f, 10.0, 20.0, {}), std::invalid_argument);
}

TEST(TerrainFeatures, AddsCratersWithTheirAttributesInStep) {
    terraingen::CraterList craters;
    craters.Append({0.0, 0.0, 10.0, 0.0, 1u});
    craters.Append({5.0, 0.0, 20.0, 0.0, 2u});
    terraingen::TerrainFeatures features;
    features.AddCraters(craters, [](const terraingen::Crater& crater) {
        return terraingen::CraterAttributes{.floorDepth = crater.diameter};
    });
    ASSERT_EQ(features.craters.Size(), 2u);
    ASSERT_EQ(features.craterAttributes.size(), 2u);
    EXPECT_EQ(features.craterAttributes[1].floorDepth, 20.0);
}
