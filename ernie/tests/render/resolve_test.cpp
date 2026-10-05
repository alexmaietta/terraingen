#include "ernie/render/resolve.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using ernie::BrdfRegistry;
using ernie::DType;
using ernie::ImageEncoding;
using ernie::Path;
using ernie::RenderPrims;
using ernie::Scene;
using ernie::Tensor;
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

TEST(RenderPrims, ResolveFollowsSettingsBindingsAndVisibility) {
    Scene scene = MinimalScene();
    scene.Define("/World/Other", tokens::kCamera);
    scene.Define("/World/Sun", tokens::kDistantLight);
    scene.Define("/Looks/Rock", tokens::kMaterial);
    scene.Define("/Looks/NotAMaterial", tokens::kXform);
    scene.Set("/World/Terrain", tokens::kMaterialBinding, Path("/Looks/Rock"));

    RenderPrims prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_TRUE(prims.settings.IsEmpty());
    EXPECT_EQ(prims.camera, Path("/World/Camera"));
    EXPECT_EQ(prims.terrain, Path("/World/Terrain"));
    EXPECT_EQ(prims.material, Path("/Looks/Rock"));
    EXPECT_EQ(prims.light, Path("/World/Sun"));

    scene.Define("/Render/Settings", tokens::kRenderSettings)
        .Set(tokens::kCameraRel, Path("/World/Other"));
    scene.Set("/World/Sun", tokens::kVisibility, tokens::kInvisible);
    scene.Set("/World/Terrain", tokens::kMaterialBinding, Path("/Looks/NotAMaterial"));
    prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_EQ(prims.settings, Path("/Render/Settings"));
    EXPECT_EQ(prims.camera, Path("/World/Other"));
    EXPECT_TRUE(prims.light.IsEmpty());
    EXPECT_TRUE(prims.material.IsEmpty());

    scene.Set("/World", tokens::kVisibility, tokens::kInvisible);
    EXPECT_TRUE(ernie::ResolveRenderPrims(scene, "/Render/Settings").terrain.IsEmpty());
    EXPECT_THROW(ernie::ResolveRenderPrims(scene, "/World/Camera"), std::invalid_argument);
    EXPECT_THROW(ernie::ResolveRenderPrims(scene, "/Missing"), std::invalid_argument);
}

TEST(RenderPrims, FallbacksSkipTheSessionRoot) {
    Scene scene;
    scene.Define("/__session/Perspective", tokens::kCamera);
    scene.Define("/__session/RenderSettings", tokens::kRenderSettings);
    scene.Define("/__session/Lights/Default", tokens::kDistantLight);
    scene.Define("/__session/Lights/Dome", tokens::kDomeLight);
    RenderPrims prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_TRUE(prims.settings.IsEmpty());
    EXPECT_TRUE(prims.camera.IsEmpty());
    EXPECT_TRUE(prims.light.IsEmpty());
    EXPECT_TRUE(prims.dome.IsEmpty());

    scene.Define("/z/Camera", tokens::kCamera);
    scene.Define("/z/Settings", tokens::kRenderSettings);
    scene.Define("/z/Lamp", tokens::kSphereLight);
    prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_EQ(prims.settings, Path("/z/Settings"));
    EXPECT_EQ(prims.camera, Path("/z/Camera"));
    EXPECT_EQ(prims.lights, std::vector<Path>{Path("/z/Lamp")});

    prims = ernie::ResolveRenderPrims(scene, "/__session/RenderSettings");
    EXPECT_EQ(prims.settings, Path("/__session/RenderSettings"));
    EXPECT_EQ(prims.camera, Path("/z/Camera"));
}

TEST(RenderPrims, LightsRelationshipChoosesTheLights) {
    Scene scene = MinimalScene();
    scene.Define("/World/Sun", tokens::kDistantLight);
    scene.Define("/World/Lamp", tokens::kSphereLight);
    scene.Define("/World/Sky", tokens::kDomeLight);
    scene.Define("/__session/Lights/Head", tokens::kDistantLight);
    scene.Define("/__session/Lights/Fill", tokens::kDistantLight);
    scene.Define("/Render/Settings", tokens::kRenderSettings)
        .Set(tokens::kLightsRel,
             std::vector<Path>{Path("/__session/Lights/Head"), Path("/__session/Lights/Fill"),
                               Path("/Missing"), Path("/World/Terrain")});
    RenderPrims prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_EQ(prims.light, Path("/__session/Lights/Fill"));
    EXPECT_EQ(prims.lights, std::vector<Path>{Path("/__session/Lights/Head")});
    EXPECT_TRUE(prims.dome.IsEmpty());

    scene.Set("/Render/Settings", tokens::kLightsRel, Path("/World/Sky"));
    prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_TRUE(prims.light.IsEmpty());
    EXPECT_TRUE(prims.lights.empty());
    EXPECT_EQ(prims.dome, Path("/World/Sky"));

    scene.Set("/Render/Settings", tokens::kLightsRel, std::vector<Path>{});
    prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_TRUE(prims.light.IsEmpty());
    EXPECT_TRUE(prims.lights.empty());
    EXPECT_TRUE(prims.dome.IsEmpty());

    scene.Clear("/Render/Settings", tokens::kLightsRel);
    prims = ernie::ResolveRenderPrims(scene, {});
    EXPECT_EQ(prims.light, Path("/World/Sun"));
    EXPECT_EQ(prims.lights, std::vector<Path>{Path("/World/Lamp")});
    EXPECT_EQ(prims.dome, Path("/World/Sky"));

    scene.Set("/Render/Settings", tokens::kLightsRel, 3.0f);
    EXPECT_THROW(ernie::ResolveRenderPrims(scene, {}), std::invalid_argument);
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
    ernie::Camera camera;
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

TEST(PrimitiveMeshOf, ReadsUsdGeomAttributesAndDefaults) {
    Scene scene;
    scene.Define("/Cube", tokens::kCube);
    scene.Define("/Rod", tokens::kCylinder)
        .Set(tokens::kGeomRadius, 0.25)
        .Set(tokens::kGeomHeight, 3.0)
        .Set(tokens::kAxis, "X");
    scene.Define("/Floor", tokens::kPlane).Set(tokens::kGeomWidth, 10.0);
    scene.Define("/Bad", tokens::kCone).Set(tokens::kAxis, "W");

    const auto bounds = [](const ernie::MeshData& mesh) {
        glm::vec3 low(1e30f);
        glm::vec3 high(-1e30f);
        for (std::size_t i = 0; i < mesh.points.size(); i += 3) {
            const glm::vec3 p(mesh.points[i], mesh.points[i + 1], mesh.points[i + 2]);
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
        return std::pair(low, high);
    };
    EXPECT_EQ(bounds(ernie::MeshDataOf(scene, "/Cube")).second, glm::vec3(1.0f))
        << "UsdGeomCube's default size is 2";
    const auto [rodLow, rodHigh] = bounds(ernie::MeshDataOf(scene, "/Rod"));
    EXPECT_FLOAT_EQ(rodHigh.x, 1.5f) << "height along the X axis";
    EXPECT_NEAR(rodHigh.y, 0.25f, 1e-6f);
    EXPECT_FLOAT_EQ(bounds(ernie::MeshDataOf(scene, "/Floor")).second.x, 5.0f);
    EXPECT_FLOAT_EQ(bounds(ernie::MeshDataOf(scene, "/Floor")).second.y, 1.0f);
    EXPECT_THROW(ernie::MeshDataOf(scene, "/Bad"), std::invalid_argument);
    EXPECT_TRUE(ernie::IsMeshType(tokens::kCapsule));
    EXPECT_FALSE(ernie::IsMeshType(tokens::kTerrain));
    scene.Define("/Xform", tokens::kXform);
    EXPECT_THROW(ernie::PrimitiveMeshOf(scene, "/Xform"), std::invalid_argument);
}

TEST(FrameOf, ReadsTheAuthoredFrame) {
    Scene scene;
    scene.Define("/Render/Settings", tokens::kRenderSettings);
    EXPECT_FALSE(ernie::FrameOf(scene, Path("/Render/Settings")).has_value());
    EXPECT_FALSE(ernie::FrameOf(scene, Path()).has_value());
    scene.Set("/Render/Settings", tokens::kFrame, 12);
    EXPECT_EQ(ernie::FrameOf(scene, Path("/Render/Settings")), 12u);
    scene.Set("/Render/Settings", tokens::kFrame, 3.0);
    EXPECT_EQ(ernie::FrameOf(scene, Path("/Render/Settings")), 3u);
    scene.Set("/Render/Settings", tokens::kFrame, -1);
    EXPECT_THROW(ernie::FrameOf(scene, Path("/Render/Settings")), std::invalid_argument);
    scene.Set("/Render/Settings", tokens::kFrame, 2.5);
    EXPECT_THROW(ernie::FrameOf(scene, Path("/Render/Settings")), std::invalid_argument);
}
