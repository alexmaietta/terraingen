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

TEST(FrameParamsOf, DefaultsMatchTheRendererWithADefaultSun) {
    const Scene scene = MinimalScene();
    const RasterPrims prims = ernie::ResolveRasterPrims(scene, {});
    const ernie::FrameParams params = ernie::FrameParamsOf(scene, prims, 64, 32);
    const ernie::FrameParams defaults;
    EXPECT_EQ(params.sunDirection, defaults.sunDirection);
    EXPECT_EQ(params.ambient, defaults.ambient);
    EXPECT_EQ(params.exposure, 1.0f);
    EXPECT_TRUE(params.shadows.enabled);
    EXPECT_EQ(params.camera.aspect, 2.0f);
    EXPECT_TRUE(params.sunIrradiance.empty()) << "the renderer's default white sun";
    EXPECT_EQ(params.physical.width, 64u);
    EXPECT_DOUBLE_EQ(params.overscan, 1.0);
    EXPECT_DOUBLE_EQ(params.physical.LinearExposureScale(), 1.0);
    const ernie::TerrainParams terrain = ernie::TerrainParamsOf(scene, prims);
    EXPECT_EQ(terrain.colorMode, ernie::TerrainParams().colorMode);
    EXPECT_EQ(terrain.exaggeration, 1.0f);
    const ernie::MaterialSettings material =
        ernie::MaterialSettingsOf(scene, prims.material, BrdfRegistry::WithBuiltins());
    EXPECT_EQ(material.brdf, "lambert");
    EXPECT_EQ(material.albedo, glm::vec3(1.0f));

    Scene empty;
    EXPECT_THROW(ernie::FrameParamsOf(empty, ernie::ResolveRasterPrims(empty, {}), 8, 8),
                 std::invalid_argument);
}

TEST(FrameParamsOf, ReadsLightMaterialTerrainAndSettings) {
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
    const RasterPrims prims = ernie::ResolveRasterPrims(scene, {});
    const ernie::FrameParams frame = ernie::FrameParamsOf(scene, prims, 32, 32);
    const float diagonal = std::sqrt(0.5f);
    EXPECT_NEAR(frame.sunDirection.x, diagonal, 1e-6f);
    EXPECT_NEAR(frame.sunDirection.z, diagonal, 1e-6f);
    EXPECT_DOUBLE_EQ(frame.physical.LinearExposureScale(), 2.0) << "camera exposure 1 stop";
    ASSERT_EQ(frame.sunIrradiance.size(), 16u);
    double luminanceIntegral = 0.0;
    for (double w = 360.0; w <= 830.0; w += 1.0) {
        luminanceIntegral += ernie::CieXyz(w).y;
    }
    EXPECT_NEAR(frame.sunIrradiance[5], M_PI * 4.0 / (683.002 * luminanceIntegral), 1e-6)
        << "a flat sun whose white Lambertian luminance is 2 x 2^1 cd/m^2";
    EXPECT_FALSE(frame.shadows.enabled);
    EXPECT_EQ(frame.ambient, 0.3f);
    EXPECT_TRUE(frame.wireframe);
    EXPECT_EQ(frame.shadows.cascades, 2);
    EXPECT_EQ(frame.shadows.resolution, 1024u);

    const ernie::MaterialSettings material =
        ernie::MaterialSettingsOf(scene, prims.material, brdfs);
    EXPECT_EQ(material.brdf, "hapke");
    const ernie::BrdfModel* hapke = brdfs.Find("hapke");
    ASSERT_NE(hapke, nullptr);
    ASSERT_EQ(material.brdfParams.size(), hapke->params.size());
    for (std::size_t i = 0; i < hapke->params.size(); ++i) {
        const float expected = hapke->params[i].name == "b" ? 0.33f : hapke->params[i].defaultValue;
        EXPECT_FLOAT_EQ(material.brdfParams[i], expected) << hapke->params[i].name;
    }
    EXPECT_EQ(material.albedo, glm::vec3(0.5f, 0.4f, 0.3f));
    EXPECT_FALSE(material.useAlbedoMap);
    EXPECT_EQ(material.detail.tileSize, 8.0f);
    EXPECT_FALSE(material.detail.hexTiling);

    ernie::TerrainParams terrain = ernie::TerrainParamsOf(scene, prims);
    EXPECT_EQ(terrain.exaggeration, 3.0f);
    EXPECT_EQ(terrain.colorMode, TerrainColorMode::Hillshade);
    EXPECT_FALSE(terrain.lod.enabled);
    EXPECT_EQ(terrain.lod.detail, 6.0f);

    scene.Set("/Render/Settings", tokens::kColorMode, tokens::kColormap);
    EXPECT_EQ(ernie::TerrainParamsOf(scene, prims).colorMode, TerrainColorMode::Colormap);
    scene.Set("/Looks/Rock", tokens::kBrdf, "nope");
    EXPECT_THROW(ernie::MaterialSettingsOf(scene, prims.material, brdfs), std::invalid_argument);
}

TEST(MeshDataOf, ReadsPointsTopologyNormalsAndVertexColours) {
    Scene scene;
    const std::array<double, 12> points{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    const std::array<std::int64_t, 1> counts{4};
    const std::array<std::int32_t, 4> indices{0, 1, 2, 3};
    const std::array<float, 12> normals{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::array<float, 12> colors{1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1};
    scene.Define("/Quad", tokens::kMesh)
        .Write(tokens::kPoints, Tensor::Copy<double>(points, {4, 3}))
        .Write(tokens::kFaceVertexCounts, Tensor::Copy<std::int64_t>(counts, {1}))
        .Write(tokens::kFaceVertexIndices, Tensor::Copy<std::int32_t>(indices, {4}))
        .Write(tokens::kNormals, Tensor::Copy<float>(normals, {12}))
        .Write(tokens::kDisplayColor, Tensor::Copy<float>(colors, {4, 3}));
    const ernie::MeshData data = ernie::MeshDataOf(scene, "/Quad");
    EXPECT_EQ(data.points.size(), 12u);
    EXPECT_EQ(data.points[3], 1.0f);
    EXPECT_EQ(data.indices, (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
    EXPECT_EQ(data.normals.size(), 12u);
    EXPECT_EQ(data.colors.size(), 12u);

    const std::array<float, 6> faceVarying{0, 0, 1, 0, 0, 1};
    scene.Write("/Quad", tokens::kNormals, Tensor::Copy<float>(faceVarying, {2, 3}));
    scene.Set("/Quad", tokens::kDisplayColor, glm::vec3(0.2f));
    scene.Clear("/Quad", tokens::kDisplayColor);
    EXPECT_TRUE(ernie::MeshDataOf(scene, "/Quad").normals.empty()) << "not one per point";

    scene.Write("/Quad", tokens::kPoints, Tensor::Zeros(DType::Int32, {4, 3}));
    EXPECT_THROW(ernie::MeshDataOf(scene, "/Quad"), std::invalid_argument);
    scene.Write("/Quad", tokens::kPoints, Tensor::Zeros(DType::Float32, {4, 2}));
    EXPECT_THROW(ernie::MeshDataOf(scene, "/Quad"), std::invalid_argument);
    scene.Write("/Quad", tokens::kPoints, Tensor::Copy<double>(points, {4, 3}));
    scene.Write("/Quad", tokens::kFaceVertexIndices, Tensor::Zeros(DType::Float32, {4}));
    EXPECT_THROW(ernie::MeshDataOf(scene, "/Quad"), std::invalid_argument);
    scene.Clear("/Quad", tokens::kFaceVertexIndices);
    EXPECT_THROW(ernie::MeshDataOf(scene, "/Quad"), std::invalid_argument);
}

TEST(InstanceDataOf, ReadsArraysAndGroupsInstancesByPrototype) {
    Scene scene;
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 2, 0, 0};
    const std::array<std::int32_t, 3> protoIndices{1, 0, 7};
    scene.Define("/Rocks", tokens::kPointInstancer)
        .Write(tokens::kPositions, Tensor::Copy<float>(positions, {3, 3}))
        .Write(tokens::kScales, Tensor::Copy<float>(positions, {9}));
    const ernie::InstanceData data = ernie::InstanceDataOf(scene, "/Rocks");
    EXPECT_EQ(data.positions.size(), 9u);
    EXPECT_TRUE(data.orientations.empty());
    EXPECT_EQ(data.scales.size(), 9u);

    using Lists = std::vector<std::vector<std::uint32_t>>;
    EXPECT_EQ(ernie::InstancesByPrototype(scene, "/Rocks", 2), (Lists{{0, 1, 2}, {}}))
        << "without protoIndices every instance uses prototype 0";
    scene.Write("/Rocks", tokens::kProtoIndices, Tensor::Copy<std::int32_t>(protoIndices, {3}));
    EXPECT_EQ(ernie::InstancesByPrototype(scene, "/Rocks", 2), (Lists{{1}, {0}}))
        << "out-of-range indices are dropped";
    EXPECT_TRUE(ernie::InstancesByPrototype(scene, "/Rocks", 0).empty());
    scene.Write("/Rocks", tokens::kProtoIndices, Tensor::Zeros(DType::Float32, {3}));
    EXPECT_THROW(ernie::InstancesByPrototype(scene, "/Rocks", 2), std::invalid_argument);
    scene.Write("/Rocks", tokens::kOrientations, Tensor::Zeros(DType::Float32, {3, 3}));
    EXPECT_THROW(ernie::InstanceDataOf(scene, "/Rocks"), std::invalid_argument);
}

TEST(BoundMaterial, FollowsBindingsToMaterialsOnly) {
    Scene scene;
    scene.Define("/Looks/Rock", tokens::kMaterial);
    scene.Define("/Looks/Group", tokens::kScope);
    scene.Define("/Mesh", tokens::kMesh).Set(tokens::kMaterialBinding, Path("/Looks/Rock"));
    EXPECT_EQ(ernie::BoundMaterial(scene, "/Mesh"), Path("/Looks/Rock"));
    scene.Set("/Mesh", tokens::kMaterialBinding, Path("/Looks/Group"));
    EXPECT_TRUE(ernie::BoundMaterial(scene, "/Mesh").IsEmpty());
    EXPECT_TRUE(ernie::BoundMaterial(scene, "/Missing").IsEmpty());
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

TEST(SunIrradianceOf, PhysicalLightsCarryTheirIrradianceAndSpectrum) {
    Scene scene;
    scene.Define("/Sun", tokens::kDistantLight).Set(tokens::kLightIrradiance, 1361.0);
    const ernie::SpectralBands bands;
    const std::vector<float> sun = ernie::SunIrradianceOf(scene, "/Sun", bands);
    EXPECT_EQ(sun, ernie::BlackbodyIrradiance(bands, 5772.0, 1361.0)) << "the Sun's black body";

    scene.Set("/Sun", tokens::kLightSpectrum, std::vector<float>{400.0f, 1.0f, 800.0f, 1.0f});
    scene.Set("/Sun", tokens::kLightIrradiance, 800.0);
    for (const float value : ernie::SunIrradianceOf(scene, "/Sun", {400.0, 780.0, 4})) {
        EXPECT_NEAR(value, 2.0f, 1e-5f) << "a flat 400 W/m^2 spectrum scaled to 800 W/m^2";
    }

    Scene hot;
    hot.Define("/Sun", tokens::kDistantLight)
        .Set(tokens::kLightIrradiance, 1000.0)
        .Set(tokens::kEnableColorTemperature, true)
        .Set(tokens::kColorTemperature, 3000.0);
    EXPECT_EQ(ernie::SunIrradianceOf(hot, "/Sun", bands),
              ernie::BlackbodyIrradiance(bands, 3000.0, 1000.0));
    EXPECT_THROW(ernie::SunIrradianceOf(hot, "/Sun", {500.0, 400.0, 4}), std::invalid_argument);
}

TEST(SunIrradianceOf, RelativeLightsFollowTheirColourAndTemperature) {
    Scene scene;
    scene.Define("/Sun", tokens::kDistantLight).Set(tokens::kColor, glm::vec3(1.0f, 0.2f, 0.1f));
    const ernie::SpectralBands bands;
    const std::vector<float> red = ernie::SunIrradianceOf(scene, "/Sun", bands);
    EXPECT_GT(red[13], 3.0f * red[2]) << "650 nm against 430 nm";

    scene.Set("/Sun", tokens::kColor, glm::vec3(1.0f));
    scene.Set("/Sun", tokens::kEnableColorTemperature, true);
    scene.Set("/Sun", tokens::kColorTemperature, 2500.0);
    const std::vector<float> warm = ernie::SunIrradianceOf(scene, "/Sun", bands);
    EXPECT_GT(warm[13], warm[2]);
    scene.Set("/Sun", tokens::kColorTemperature, 20000.0);
    const std::vector<float> cool = ernie::SunIrradianceOf(scene, "/Sun", bands);
    EXPECT_LT(cool[13], cool[2]);
}

TEST(OverscanOf, WidensTheRasterViewForDistortion) {
    ernie::PhysicalCamera camera;
    camera.width = 64;
    camera.height = 48;
    EXPECT_EQ(ernie::OverscanOf(camera), 1.0);
    camera.lens.k1 = -0.2;
    const double barrel = ernie::OverscanOf(camera);
    EXPECT_GT(barrel, 1.02) << "barrel distortion pulls wider rays into the sensor";
    camera.lens.k1 = 0.2;
    EXPECT_LT(ernie::OverscanOf(camera), barrel);
    EXPECT_GE(ernie::OverscanOf(camera), 1.0);
}

TEST(RasterCameraOf, NarrowsByTheOverscanAndShiftsThePrincipalPoint) {
    Scene scene = MinimalScene();
    ernie::PhysicalCamera camera = ernie::PhysicalCameraOf(scene, "/World/Camera", 64, 32);
    const ernie::CameraView plain = ernie::RasterCameraOf(scene, "/World/Camera", camera, 1.0);
    EXPECT_NEAR(plain.verticalFov, camera.VerticalFov(), 1e-6);
    EXPECT_FLOAT_EQ(plain.aspect, 2.0f);
    EXPECT_FLOAT_EQ(plain.projection[2][0], 0.0f);
    const ernie::CameraView wide = ernie::RasterCameraOf(scene, "/World/Camera", camera, 1.5);
    EXPECT_NEAR(std::tan(wide.verticalFov / 2.0), 1.5 * std::tan(plain.verticalFov / 2.0), 1e-5);

    camera.horizontalApertureOffsetMm = 2.0;
    const ernie::CameraView shifted = ernie::RasterCameraOf(scene, "/World/Camera", camera, 1.0);
    EXPECT_NEAR(shifted.projection[2][0], 1.0 - 2.0 * camera.PrincipalPoint().x / 64.0, 1e-6);
    EXPECT_NE(shifted.projection[2][0], 0.0f);
}
