// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/layer.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
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
  protected:
    void FillSamples(terraingen::SampleBlock& samples, double /*spacing*/,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            samples.Height(i) = 2.0f;
        }
    }
};

/// The surface's radius where a formula layer reads it, 0 on a plane.
class Radius final : public terraingen::FormulaLayer {
  protected:
    float HeightAt(float height, double /*x*/, double /*y*/,
                   const terraingen::LayerContext& context) const override {
        const terraingen::Surface& surface = context.SurfaceOrPlane();
        return height + static_cast<float>(surface.IsBody() ? surface.Radius() : 0.0);
    }
};

}  // namespace

TEST(FormulaLayer, ReadsTheSurfaceFromItsContext) {
    const Radius layer;
    EXPECT_EQ(layer.ApplyAt(1.0f, 0.0, 0.0, {1.0}), 1.0f) << "a plane without a surface";
    const terraingen::Surface sphere = terraingen::Surface::Sphere(1000.0);
    terraingen::LayerContext context{1.0};
    context.surface = &sphere;
    EXPECT_EQ(layer.ApplyAt(1.0f, 0.0, 0.0, context), 1001.0f);
}

TEST(Layer, HasNoSeedsOrDependenciesByDefault) {
    const Plus plus;
    EXPECT_EQ(plus.Reseeded(7), nullptr);
    EXPECT_EQ(plus.Rebound({}), nullptr);
    EXPECT_FALSE(plus.LayerBase());
    EXPECT_EQ(plus.Rebased({}), nullptr);
}

TEST(Replacement, GivesTheReplacementOfTheSameTypeOrTheLayer) {
    const auto old = std::make_shared<const Plus>();
    const auto fresh = std::make_shared<const Plus>();
    const auto other = std::make_shared<const Radius>();
    const terraingen::LayerReplacements replaced{{old.get(), fresh}};
    EXPECT_EQ(terraingen::Replacement(old, replaced), fresh);
    EXPECT_EQ(terraingen::Replacement(fresh, replaced), fresh);
    const terraingen::LayerReplacements mistyped{{old.get(), other}};
    EXPECT_EQ(terraingen::Replacement(old, mistyped), old);
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
}

TEST(Layer, AppliesToOneCellForPointQueriesByDefault) {
    const Plus plus;
    EXPECT_EQ(plus.ApplyAt(1.5f, 10.0, 20.0, {2.0}), 2.5f);
    EXPECT_THROW(plus.ApplyAt(1.5f, 10.0, 20.0, {}), std::invalid_argument);
    EXPECT_THROW(plus.ApplyAt(1.5f, 10.0, 20.0, {std::numeric_limits<double>::infinity()}),
                 std::invalid_argument);
}
