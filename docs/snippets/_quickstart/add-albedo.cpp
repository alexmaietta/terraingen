// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief An Albedo With Bright Fresh Craters for the Quickstart
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, QuickstartAddAlbedo) {
    const auto source = std::make_shared<FlatSource>(257, 257, 0.5);
    const auto craters = std::make_shared<CraterLayer>();
    // --8<-- [start:add_albedo]
    SyntheticAlbedoOptions base;
    base.value = 0.11;  // mature mare soil at 750 nm
    Albedo albedo;
    albedo.source = std::make_shared<SyntheticAlbedo>(base);
    albedo.layers = {std::make_shared<CraterAlbedo>(craters)};
    const std::vector<Raster<float>> bands = EvaluateAlbedo(albedo, source->Grid(), {});
    // --8<-- [end:add_albedo]
    ASSERT_EQ(bands.size(), 1u);
}

}  // namespace
}  // namespace terraingen
