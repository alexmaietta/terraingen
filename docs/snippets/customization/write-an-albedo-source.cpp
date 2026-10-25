// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write an Albedo Source of Your Own
///
/// An albedo source gives the starting albedo, as its number of bands, 1 or 3, and the value at
/// any point. It can be a map in a format of your own, an analytic pattern or another model's
/// output. Albedo layers are laid over it as over a synthetic albedo. Here dark mare meets bright
/// highlands along a winding contact, and a fresh crater's rays reach across it.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>
#include <vector>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteAnAlbedoSource) {
    // --8<-- [start:write_an_albedo_source]
    // Dark mare at 0.09 in the west and bright highlands at 0.16 in the east, along a winding
    // contact about 40 m wide.
    class MareContact final : public AlbedoSource {
      public:
        std::size_t Bands() const override {
            return 1;
        }

        AlbedoSample ValueAt(double x, double y, const AlbedoContext& /*context*/) const override {
            const double share = 0.5 * (1.0 + std::tanh((x - 20.0 * std::sin(y / 60.0)) / 20.0));
            return {static_cast<float>(0.09 + 0.07 * share), 0.0f, 0.0f};
        }
    };

    CraterAlbedoOptions options;
    options.seed = 2;
    Albedo albedo;
    albedo.source = std::make_shared<MareContact>();
    albedo.layers = {std::make_shared<CraterAlbedo>(
        CraterLayer::Manual({ManualCrater(70.0, -60.0, 60.0)}), options)};
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {400.0, 400.0}, 2.0);  // meters
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, grid, {});
    // --8<-- [end:write_an_albedo_source]
    ASSERT_EQ(bands.size(), 1u);
    EXPECT_LT(bands[0](0, 100), 0.1f) << "mare in the west";
    EXPECT_GT(bands[0](199, 100), 0.15f) << "highlands in the east";
}

}  // namespace
}  // namespace terraingen
