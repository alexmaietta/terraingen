// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Write an Upsampler of Your Own and Name It in Configurations
///
/// An upsampler decides a source's heights between its posts and below their spacing, as cubic
/// interpolation or diamond-square refinement do. Registered under a name, configurations use it
/// like the built-in ones, as a source's `"upsampling"`. Here it holds the nearest post's height,
/// rounded down to 2 m terraces.
#include "snippet_test.h"
#include "terraingen/terraingen.h"

#include <cmath>
#include <memory>

namespace terraingen {
namespace {

TEST_F(SnippetTest, WriteAnUpsampler) {
    // --8<-- [start:write_an_upsampler]
    class Terraces final : public Upsampler {
      public:
        int Reach() const override {
            return 1;
        }

      protected:
        void FillSamples(const PostLattice& posts, SampleBlock& samples, double /*spacing*/,
                         unsigned /*threads*/) const override {
            const GeoTransform transform = posts.Transform();
            for (std::size_t i = 0; i < samples.Size(); ++i) {
                const Vec2d p = samples.Position(i);
                const Vec2d pixel = transform.WorldToPixel(p.x, p.y);
                float height = 0.0f;
                posts.Read(static_cast<std::int64_t>(std::floor(pixel.x)),
                           static_cast<std::int64_t>(std::floor(pixel.y)), 1, 1, {&height, 1});
                samples.Height(i) = 2.0f * std::floor(height / 2.0f);
            }
        }
    };

    RegisterUpsampler("terraces",
                      [](const std::string& /*settings*/) { return std::make_shared<Terraces>(); });
    const TerrainGenConfig config = ParseConfig(R"({
        "schema_version": 1,
        "source": {"type": "synthetic", "pixel_size_m": 4, "upsampling": "terraces",
                   "stack": [{"wavelength_m": 120, "amplitude_m": 8, "octaves": 3}]}
    })");
    const RasterGrid grid = RasterGrid::Centered({0.0, 0.0}, {128.0, 128.0}, 1.0);
    const Raster<float> heights = BuildTerrain(config).Evaluate(grid);
    // --8<-- [end:write_an_upsampler]
    for (const float height : heights.Values()) {
        EXPECT_EQ(std::fmod(height, 2.0f), 0.0f);
    }
}

}  // namespace
}  // namespace terraingen
