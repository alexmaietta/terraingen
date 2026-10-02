#include "ernie/raster/raster_delegate.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using ernie::BrdfRegistry;
using ernie::DType;
using ernie::ImageEncoding;
using ernie::Path;
using ernie::RasterPrims;
using ernie::Scene;
using ernie::Tensor;
using ernie::TerrainColorMode;
using ernie::TerrainDrawParams;
namespace tokens = ernie::tokens;

namespace {

Scene MinimalScene() {
    Scene scene;
    scene.Define("/World/Camera", tokens::kCamera)
        .Set(tokens::kTransform,
             ernie::LookAtTransform({0.0, -100.0, 100.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}));
    scene.Define("/World/Terrain", tokens::kTerrain);
    return scene;
}

}  // namespace

TEST(RasterPrims, ResolveFollowsSettingsBindingsAndVisibility) {
    Scene scene = MinimalScene();
    scene.Define("/World/Other", tokens::kCamera);
    scene.Define("/World/Sun", tokens::kDistantLight);
    scene.Define("/Looks/Rock", tokens::kMaterial);
    scene.Define("/Looks/NotAMaterial", tokens::kXform);
    scene.Set("/World/Terrain", tokens::kMaterialBinding, Path("/Looks/Rock"));

    RasterPrims prims = ernie::ResolveRasterPrims(scene, {});
    EXPECT_TRUE(prims.settings.IsEmpty());
    EXPECT_EQ(prims.camera, Path("/World/Camera"));
    EXPECT_EQ(prims.terrain, Path("/World/Terrain"));
    EXPECT_EQ(prims.material, Path("/Looks/Rock"));
    EXPECT_EQ(prims.light, Path("/World/Sun"));

    scene.Define("/Render/Settings", tokens::kRenderSettings)
        .Set(tokens::kCameraRel, Path("/World/Other"));
    scene.Set("/World/Sun", tokens::kVisibility, tokens::kInvisible);
    scene.Set("/World/Terrain", tokens::kMaterialBinding, Path("/Looks/NotAMaterial"));
    prims = ernie::ResolveRasterPrims(scene, {});
    EXPECT_EQ(prims.settings, Path("/Render/Settings"));
    EXPECT_EQ(prims.camera, Path("/World/Other"));
    EXPECT_TRUE(prims.light.IsEmpty());
    EXPECT_TRUE(prims.material.IsEmpty());

    scene.Set("/World", tokens::kVisibility, tokens::kInvisible);
    EXPECT_TRUE(ernie::ResolveRasterPrims(scene, "/Render/Settings").terrain.IsEmpty());
    EXPECT_THROW(ernie::ResolveRasterPrims(scene, "/World/Camera"), std::invalid_argument);
    EXPECT_THROW(ernie::ResolveRasterPrims(scene, "/Missing"), std::invalid_argument);
}

TEST(DrawParamsOf, DefaultsMatchTheRendererWithADefaultSun) {
    const Scene scene = MinimalScene();
    const TerrainDrawParams params = ernie::DrawParamsOf(
        scene, ernie::ResolveRasterPrims(scene, {}), BrdfRegistry::WithBuiltins(), 2.0f);
    const TerrainDrawParams defaults;
    EXPECT_EQ(params.sunDirection, defaults.sunDirection);
    EXPECT_EQ(params.ambient, defaults.ambient);
    EXPECT_EQ(params.colorMode, defaults.colorMode);
    EXPECT_EQ(params.exaggeration, 1.0f);
    EXPECT_EQ(params.surface.exposure, 1.0f);
    EXPECT_EQ(params.surface.brdf, "lambert");
    EXPECT_EQ(params.surface.albedo, glm::vec3(1.0f));
    EXPECT_TRUE(params.shadows.enabled);
    EXPECT_EQ(params.camera.aspect, 2.0f);

    Scene empty;
    EXPECT_THROW(ernie::DrawParamsOf(empty, ernie::ResolveRasterPrims(empty, {}),
                                     BrdfRegistry::WithBuiltins(), 1.0f),
                 std::invalid_argument);
}

TEST(DrawParamsOf, ReadsLightMaterialTerrainAndSettings) {
    Scene scene = MinimalScene();
    scene.Define("/World/Sun", tokens::kDistantLight)
        .Set(tokens::kTransform, ernie::DistantLightTransform({1.0, 0.0, 1.0}))
        .Set(tokens::kIntensity, 2.0)
        .Set(tokens::kExposure, 1.0f)
        .Set(tokens::kShadowEnable, false);
    scene.Set("/World/Camera", tokens::kCameraExposure, 1.0f);
    scene.Define("/Looks/Rock", tokens::kMaterial)
        .Set(tokens::kBrdf, "hapke")
        .Set("inputs:b", 0.33)
        .Set(tokens::kAlbedo, glm::vec3(0.5f, 0.4f, 0.3f))
        .Set(tokens::kAlbedoMapEnable, false)
        .Set(tokens::kDetailTileSize, 8.0f)
        .Set(tokens::kDetailHexTiling, false);
    scene.Set("/World/Terrain", tokens::kMaterialBinding, Path("/Looks/Rock"));
    scene.Set("/World/Terrain", tokens::kExaggeration, 3.0f);
    scene.Define("/Render/Settings", tokens::kRenderSettings)
        .Set(tokens::kAmbient, 0.3f)
        .Set(tokens::kColorMode, tokens::kHillshade)
        .Set(tokens::kWireframe, true)
        .Set(tokens::kShadowCascades, 2)
        .Set(tokens::kShadowResolution, 1024)
        .Set(tokens::kLodEnable, false)
        .Set(tokens::kLodDetail, 6.0f);

    const BrdfRegistry brdfs = BrdfRegistry::WithBuiltins();
    const TerrainDrawParams params =
        ernie::DrawParamsOf(scene, ernie::ResolveRasterPrims(scene, {}), brdfs, 1.0f);
    const float diagonal = std::sqrt(0.5f);
    EXPECT_NEAR(params.sunDirection.x, diagonal, 1e-6f);
    EXPECT_NEAR(params.sunDirection.z, diagonal, 1e-6f);
    EXPECT_FLOAT_EQ(params.surface.exposure, 2.0f) << "2 x 2^1 / 2^1";
    EXPECT_FALSE(params.shadows.enabled);
    EXPECT_EQ(params.surface.brdf, "hapke");
    const ernie::BrdfModel* hapke = brdfs.Find("hapke");
    ASSERT_NE(hapke, nullptr);
    ASSERT_EQ(params.surface.brdfParams.size(), hapke->params.size());
    for (std::size_t i = 0; i < hapke->params.size(); ++i) {
        const float expected = hapke->params[i].name == "b" ? 0.33f : hapke->params[i].defaultValue;
        EXPECT_FLOAT_EQ(params.surface.brdfParams[i], expected) << hapke->params[i].name;
    }
    EXPECT_EQ(params.surface.albedo, glm::vec3(0.5f, 0.4f, 0.3f));
    EXPECT_FALSE(params.surface.useAlbedoMap);
    EXPECT_EQ(params.surface.detail.tileSize, 8.0f);
    EXPECT_FALSE(params.surface.detail.hexTiling);
    EXPECT_EQ(params.exaggeration, 3.0f);
    EXPECT_EQ(params.ambient, 0.3f);
    EXPECT_EQ(params.colorMode, TerrainColorMode::Hillshade);
    EXPECT_TRUE(params.wireframe);
    EXPECT_EQ(params.shadows.cascades, 2);
    EXPECT_EQ(params.shadows.resolution, 1024u);
    EXPECT_FALSE(params.lod.enabled);
    EXPECT_EQ(params.lod.detail, 6.0f);

    scene.Set("/Render/Settings", tokens::kColorMode, tokens::kColormap);
    EXPECT_EQ(
        ernie::DrawParamsOf(scene, ernie::ResolveRasterPrims(scene, {}), brdfs, 1.0f).colorMode,
        TerrainColorMode::Colormap);
    scene.Set("/Looks/Rock", tokens::kBrdf, "nope");
    EXPECT_THROW(ernie::DrawParamsOf(scene, ernie::ResolveRasterPrims(scene, {}), brdfs, 1.0f),
                 std::invalid_argument);
}

TEST(ImageOfTensor, ConvertsFloatAndByteTextures) {
    const std::array<float, 6> rgb{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
    const ernie::Image floats =
        ernie::ImageOfTensor(Tensor::Copy<float>(rgb, {1, 2, 3}), ImageEncoding::Srgb);
    EXPECT_EQ(floats.width, 2u);
    EXPECT_EQ(floats.height, 1u);
    EXPECT_EQ(floats.At(1, 0), glm::vec4(0.4f, 0.5f, 0.6f, 1.0f));

    const std::array<std::uint8_t, 4> rgba{255, 128, 0, 51};
    const Tensor bytes = Tensor::Copy<std::uint8_t>(rgba, {1, 1, 4});
    const ernie::Image srgb = ernie::ImageOfTensor(bytes, ImageEncoding::Srgb);
    EXPECT_NEAR(srgb.At(0, 0).g, ernie::SrgbToLinear(128.0f / 255.0f), 1e-6f);
    EXPECT_NEAR(srgb.At(0, 0).a, 0.2f, 1e-6f) << "alpha stays linear";
    const ernie::Image linear = ernie::ImageOfTensor(bytes, ImageEncoding::Linear);
    EXPECT_NEAR(linear.At(0, 0).g, 128.0f / 255.0f, 1e-6f);

    const std::array<float, 2> grey{0.25f, 0.75f};
    const ernie::Image g =
        ernie::ImageOfTensor(Tensor::Copy<float>(grey, {2, 1}), ImageEncoding::Linear);
    EXPECT_EQ(g.At(0, 1), glm::vec4(0.75f, 0.75f, 0.75f, 1.0f));

    EXPECT_THROW(
        ernie::ImageOfTensor(Tensor::Zeros(DType::Float32, {2, 2, 2}), ImageEncoding::Srgb),
        std::invalid_argument);
    EXPECT_THROW(ernie::ImageOfTensor(Tensor::Zeros(DType::Int32, {2, 2}), ImageEncoding::Srgb),
                 std::invalid_argument);
    EXPECT_THROW(ernie::ImageOfTensor(Tensor::Zeros(DType::Float32, {0, 2}), ImageEncoding::Srgb),
                 std::invalid_argument);
    EXPECT_THROW(ernie::ImageOfTensor(Tensor::Zeros(DType::Float32, {4}), ImageEncoding::Srgb),
                 std::invalid_argument);
}
