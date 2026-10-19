#include "terraingen/layers/layer.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace {

/// Adds 1 to every sample.
class Plus final : public terraingen::Layer {
  protected:
    void ApplySamples(terraingen::SampleBlock& samples,
                      const terraingen::LayerContext& /*context*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) += 1.0f;
        }
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
    EXPECT_NO_THROW(terraingen::ValidatePointContext({0.5}));
    EXPECT_THROW(terraingen::ValidatePointContext({}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidatePointContext({std::numeric_limits<double>::infinity()}),
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
    EXPECT_EQ(plus.ApplyAt(1.5f, 10.0, 20.0, {2.0}), 2.5f);
    EXPECT_THROW(plus.ApplyAt(1.5f, 10.0, 20.0, {}), std::invalid_argument);
}
