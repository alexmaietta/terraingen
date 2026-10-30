// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Evaluate on the GPU
///
/// Layers whose relief is a GPU kernel, such as noise and craters, run on a Vulkan, CUDA or Metal
/// device when evaluation asks for one; the other layers and every point query run on the CPU.
/// `Backend::Auto` picks a GPU when one is available and the CPU otherwise. Heights on a GPU match
/// the CPU to within float rounding.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, EvaluateOnTheGpu) {
    // --8<-- [start:evaluate_on_the_gpu]
    const Terrain terrain(Surface{}, std::make_shared<FlatSource>(257, 257, 1.0),
                          {std::make_shared<NoiseLayer>(Fractal::Fbm(3, 40.0, 1.0)),
                           std::make_shared<CraterLayer>()});
    const Backend where = ResolveBackend(Backend::Auto);  // what Auto runs on here
    const Raster<float> heights = terrain.Evaluate(*terrain.Grid(), EvaluationOptions{}.On(where));
    // --8<-- [end:evaluate_on_the_gpu]
    const Raster<float> cpu =
        terrain.Evaluate(*terrain.Grid(), EvaluationOptions{}.On(Backend::Cpu));
    for (std::size_t i = 0; i < cpu.Values().size(); ++i) {
        EXPECT_NEAR(heights.Values()[i], cpu.Values()[i], 1e-3);
    }
}

}  // namespace
}  // namespace terraingen
