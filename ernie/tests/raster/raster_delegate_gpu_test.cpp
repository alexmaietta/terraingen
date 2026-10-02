#include "ernie/gpu/offscreen_target.h"
#include "ernie/raster/raster_delegate.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"
#include "support/test_raster.h"
#include "support/vulkan_fixture.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <vector>

using ernie::DType;
using ernie::OrbitCamera;
using ernie::Path;
using ernie::RasterDelegate;
using ernie::Scene;
using ernie::Tensor;
using ernie::testing::RollingTerrain;
using ernie::testing::TestRaster;
namespace tokens = ernie::tokens;

namespace {

constexpr std::uint32_t kSize = 48;

Tensor HeightsOf(const TestRaster& raster) {
    auto owner = std::make_shared<std::vector<float>>(raster.values);
    return Tensor(
        owner->data(), DType::Float32,
        {static_cast<std::int64_t>(raster.height), static_cast<std::int64_t>(raster.width)}, {}, {},
        owner);
}

OrbitCamera FramingCamera(const RasterDelegate& delegate) {
    OrbitCamera camera;
    camera.pitch = 0.8f;
    const auto [low, high] = delegate.Terrain().Bounds(1.0f);
    camera.Frame(low, high);
    return camera;
}

/// A terrain lit by a low sun, with render settings for a `kSize` square image.
Scene TerrainScene(const TestRaster& raster) {
    Scene scene;
    scene.Define("/World/Terrain", tokens::kTerrain)
        .Write(tokens::kHeights, HeightsOf(raster))
        .Set(tokens::kOrigin, glm::dvec2(raster.originX, raster.originY))
        .Set(tokens::kCellSize, raster.cellSize)
        .Set(tokens::kMaterialBinding, Path("/Looks/Rock"));
    scene.Define("/Looks/Rock", tokens::kMaterial).Set(tokens::kAlbedo, glm::vec3(0.6f));
    scene.Define("/World/Sun", tokens::kDistantLight)
        .Set(tokens::kTransform, ernie::DistantLightTransform({1.0, 1.0, 0.6}));
    scene.Define("/Render/Settings", tokens::kRenderSettings)
        .Set(tokens::kCameraRel, Path("/World/Camera"))
        .Set(tokens::kResolution, glm::ivec2(kSize, kSize))
        .Set(tokens::kClearColor, glm::vec3(0.0f, 0.0f, 1.0f));
    return scene;
}

class RasterDelegateTest : public ernie::testing::VulkanTest {};

}  // namespace

TEST_F(RasterDelegateTest, RendersTheSameImageAsTheRendererDrivenDirectly) {
    const TestRaster raster = RollingTerrain(65, 65, 2.0);
    Scene scene = TerrainScene(raster);
    RasterDelegate delegate(*context_);
    delegate.Sync(scene);
    ASSERT_TRUE(delegate.Terrain().HasHeightfield());
    const OrbitCamera camera = FramingCamera(delegate);
    ernie::SetCamera(scene, "/World/Camera", camera);

    const Tensor image = delegate.Render(scene);
    ASSERT_EQ(image.Shape(), (std::vector<std::int64_t>{kSize, kSize, 4}));
    EXPECT_EQ(delegate.Prims().terrain, Path("/World/Terrain"));

    ernie::RasterRenderer direct(*context_, VK_FORMAT_R8G8B8A8_UNORM);
    ernie::TerrainPart terrainPart(direct);
    terrainPart.SetHeightfield(raster);
    const std::uint32_t material = direct.AddMaterial(
        ernie::MaterialSettingsOf(scene, delegate.Prims().material, direct.Brdfs()));
    ernie::TerrainParams terrainParams = ernie::TerrainParamsOf(scene, delegate.Prims());
    terrainParams.material = material;
    terrainPart.SetParams(terrainParams);
    ernie::RasterPart* parts[] = {&terrainPart};
    direct.Update(ernie::FrameParamsOf(scene, delegate.Prims(), 1.0f), parts);
    ernie::OffscreenTarget target(*context_, kSize, kSize);
    const ernie::RasterPart* drawn[] = {&terrainPart};
    context_->Submit([&](VkCommandBuffer commands) {
        direct.Record(commands, target.Prepare(commands, {{0.0f, 0.0f, 1.0f, 1.0f}}), drawn);
    });
    const std::vector<std::uint8_t> expected = target.ReadColor();
    ASSERT_EQ(expected.size(), static_cast<std::size_t>(image.Elements()));
    EXPECT_EQ(std::memcmp(expected.data(), image.Data(), expected.size()), 0);

    const auto* pixels = image.As<std::uint8_t>();
    EXPECT_EQ(pixels[2], 255) << "clear colour from ernie:clearColor";
    int terrain = 0;
    for (std::size_t i = 0; i < expected.size(); i += 4) {
        terrain += pixels[i + 2] != 255 ? 1 : 0;
    }
    EXPECT_GT(terrain, static_cast<int>(kSize * kSize / 4)) << "the terrain covers the view";
}

TEST_F(RasterDelegateTest, BgraTargetsComeBackAsRgba) {
    const TestRaster raster = RollingTerrain(33, 33, 2.0);
    Scene scene = TerrainScene(raster);
    RasterDelegate rgba(*context_);
    rgba.Sync(scene);
    ernie::SetCamera(scene, "/World/Camera", FramingCamera(rgba));
    RasterDelegate bgra(*context_, VK_FORMAT_B8G8R8A8_UNORM);
    const Tensor a = rgba.Render(scene, "/Render/Settings");
    const Tensor b = bgra.Render(scene, "/Render/Settings");
    ASSERT_EQ(a.Elements(), b.Elements());
    EXPECT_EQ(std::memcmp(a.Data(), b.Data(), static_cast<std::size_t>(a.Elements())), 0);

    RasterDelegate wide(*context_, VK_FORMAT_R16G16B16A16_SFLOAT);
    EXPECT_THROW(wide.Render(scene), std::invalid_argument);
}

TEST_F(RasterDelegateTest, SyncsHeightfieldEditsRemovalsAndTextures) {
    Scene scene = TerrainScene(RollingTerrain(33, 17, 2.0));
    RasterDelegate delegate(*context_);
    delegate.Sync(scene);
    EXPECT_EQ(delegate.Terrain().Width(), 33u);
    EXPECT_EQ(delegate.Terrain().Height(), 17u);

    scene.Write("/World/Terrain", tokens::kHeights, HeightsOf(RollingTerrain(9, 5, 2.0)));
    delegate.Sync(scene);
    EXPECT_EQ(delegate.Terrain().Width(), 9u);

    scene.Write("/Looks/Rock", tokens::kDetailAlbedo, Tensor::Zeros(DType::UInt8, {8, 8, 3}));
    scene.Write("/Looks/Rock", tokens::kDetailNormal, Tensor::Zeros(DType::Float32, {8, 8, 4}));
    scene.Write("/Looks/Rock", tokens::kAlbedoMap, Tensor::Zeros(DType::Float32, {4, 4}));
    delegate.Sync(scene);
    ASSERT_TRUE(delegate.MaterialId("/Looks/Rock").has_value());
    const ernie::RasterMaterial& rock =
        delegate.Renderer().Material(*delegate.MaterialId("/Looks/Rock"));
    EXPECT_TRUE(rock.HasDetailAlbedo());
    EXPECT_TRUE(rock.HasDetailNormal());
    EXPECT_TRUE(rock.HasAlbedoMap());

    scene.Clear("/Looks/Rock", tokens::kDetailAlbedo);
    delegate.Sync(scene);
    EXPECT_FALSE(rock.HasDetailAlbedo());
    EXPECT_TRUE(rock.HasDetailNormal());
    scene.Remove("/Looks/Rock");
    delegate.Sync(scene);
    EXPECT_FALSE(delegate.MaterialId("/Looks/Rock").has_value());

    scene.Set("/World/Terrain", tokens::kVisibility, tokens::kInvisible);
    delegate.Sync(scene);
    EXPECT_FALSE(delegate.Terrain().HasHeightfield());
    scene.Set("/World/Terrain", tokens::kVisibility, tokens::kInherited);
    delegate.Sync(scene);
    EXPECT_TRUE(delegate.Terrain().HasHeightfield());
}

TEST_F(RasterDelegateTest, ReportsBadTensorsOnceWithThePrimAndAttribute) {
    Scene scene = TerrainScene(RollingTerrain(9, 9, 2.0));
    scene.Write("/World/Terrain", tokens::kHeights, Tensor::Zeros(DType::Float64, {9, 9}));
    RasterDelegate delegate(*context_);
    try {
        delegate.Sync(scene);
        FAIL() << "expected an upload error";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("heights of '/World/Terrain'"), std::string::npos)
            << error.what();
    }
    scene.Set("/World/Terrain", tokens::kExaggeration, 2.0f);
    EXPECT_NO_THROW(delegate.Sync(scene)) << "not retried until the heights change";

    scene.Write("/Looks/Rock", tokens::kDetailNormal, Tensor::Zeros(DType::Int32, {2, 2}));
    EXPECT_THROW(delegate.Sync(scene), std::invalid_argument);
    scene.Write("/Looks/Rock", tokens::kAlbedoMap, Tensor::Zeros(DType::Float32, {4}));
    EXPECT_THROW(delegate.Sync(scene), std::invalid_argument);
}

TEST_F(RasterDelegateTest, SwitchingScenesResyncsEverything) {
    Scene first = TerrainScene(RollingTerrain(33, 17, 2.0));
    for (int i = 0; i < 20; ++i) {
        first.Set("/World/Terrain", tokens::kExaggeration, 1.0f);
    }
    RasterDelegate delegate(*context_);
    delegate.Sync(first);
    EXPECT_EQ(delegate.Terrain().Width(), 33u);

    Scene second = TerrainScene(RollingTerrain(9, 5, 2.0));
    ASSERT_LT(second.Version(), first.Version()) << "an older cursor must not hide it";
    delegate.Sync(second);
    EXPECT_EQ(delegate.Terrain().Width(), 9u);

    Scene empty;
    delegate.Sync(empty);
    EXPECT_FALSE(delegate.Terrain().HasHeightfield());
    delegate.Sync(second);
    EXPECT_TRUE(delegate.Terrain().HasHeightfield());
}

namespace {

/// A square of side 4 in the x-y plane centred on the origin, as one quad face.
void DefineQuad(Scene& scene, const Path& path, float half = 2.0f) {
    const std::array<float, 12> points{-half, -half, 0, half,  -half, 0,
                                       half,  half,  0, -half, half,  0};
    const std::array<std::int64_t, 1> counts{4};
    const std::array<std::int64_t, 4> indices{0, 1, 2, 3};
    scene.Define(path, tokens::kMesh)
        .Write(tokens::kPoints, Tensor::Copy<float>(points, {4, 3}))
        .Write(tokens::kFaceVertexCounts, Tensor::Copy<std::int64_t>(counts, {1}))
        .Write(tokens::kFaceVertexIndices, Tensor::Copy<std::int64_t>(indices, {4}));
}

/// Meshes seen from straight above, lit from overhead, against a black background.
Scene MeshScene() {
    Scene scene;
    scene.Define("/World/Camera", tokens::kCamera)
        .Set(tokens::kTransform,
             ernie::LookAtTransform({0.0, 0.0, 60.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}))
        .Set(tokens::kFocalLength, 18.0f);
    scene.Define("/World/Sun", tokens::kDistantLight)
        .Set(tokens::kTransform, ernie::DistantLightTransform({0.0, 0.0, 1.0}));
    scene.Define("/Render/Settings", tokens::kRenderSettings)
        .Set(tokens::kResolution, glm::ivec2(kSize, kSize))
        .Set(tokens::kAmbient, 0.0f);
    scene.Define("/Looks/Red", tokens::kMaterial).Set(tokens::kAlbedo, glm::vec3(1.0f, 0.0f, 0.0f));
    DefineQuad(scene, "/World/Quad");
    scene.Set("/World/Quad", tokens::kTransform,
              glm::translate(glm::dmat4(1.0), glm::dvec3(-12.0, 0.0, 0.0)));
    scene.Set("/World/Quad", tokens::kMaterialBinding, Path("/Looks/Red"));

    const std::array<float, 9> positions{12, -10, 0, 12, 0, 0, 12, 10, 0};
    scene.Define("/World/Rocks", tokens::kPointInstancer)
        .Write(tokens::kPositions, Tensor::Copy<float>(positions, {3, 3}))
        .Set(tokens::kPrototypes, std::vector<Path>{"/World/Rocks/Rock"});
    DefineQuad(scene, "/World/Rocks/Rock", 1.5f);
    scene.Set("/World/Rocks/Rock", tokens::kDisplayColor, glm::vec3(0.0f, 1.0f, 0.0f));
    return scene;
}

/// The RGBA texel showing world point `p` in `image`.
const std::uint8_t* TexelAt(const Scene& scene, const Tensor& image, glm::vec3 p) {
    const glm::vec4 clip =
        ernie::CameraViewOf(scene, "/World/Camera", 1.0f).ViewProjection() * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const auto col = static_cast<std::size_t>((ndc.x * 0.5f + 0.5f) * kSize);
    const auto row = static_cast<std::size_t>((0.5f - ndc.y * 0.5f) * kSize);
    return image.As<std::uint8_t>() + 4 * (row * kSize + col);
}

}  // namespace

TEST_F(RasterDelegateTest, DrawsMeshesAndInstancersWithTheirMaterials) {
    Scene scene = MeshScene();
    RasterDelegate delegate(*context_);
    Tensor image = delegate.Render(scene);
    EXPECT_EQ(delegate.Meshes().MeshCount(), 2u);
    EXPECT_EQ(delegate.Meshes().InstancerCount(), 1u);
    const std::uint8_t* quad = TexelAt(scene, image, {-12, 0, 0});
    EXPECT_GT(quad[0], 200);
    EXPECT_LT(quad[1] + quad[2], 10) << "red from its material";
    for (const float y : {-10.0f, 0.0f, 10.0f}) {
        const std::uint8_t* rock = TexelAt(scene, image, {12, y, 0});
        EXPECT_GT(rock[1], 200) << "green from displayColor, instance at y " << y;
        EXPECT_LT(rock[0] + rock[2], 10);
    }
    const std::uint8_t* prototype = TexelAt(scene, image, {0, 0, 0});
    EXPECT_EQ(prototype[0] + prototype[1] + prototype[2], 0) << "prototypes draw only as instances";
    EXPECT_TRUE(delegate.Renderer().Cascades().size() > 0) << "meshes alone enable shadows";
}

TEST_F(RasterDelegateTest, MovesInstancesAndDeformsMeshesInPlace) {
    Scene scene = MeshScene();
    RasterDelegate delegate(*context_);
    delegate.Render(scene);

    const std::array<float, 9> moved{0, -10, 0, 12, 0, 0, 12, 10, 0};
    scene.Write("/World/Rocks", tokens::kPositions, Tensor::Copy<float>(moved, {3, 3}));
    const std::array<float, 12> points{-2, 6, 0, 2, 6, 0, 2, 10, 0, -2, 10, 0};
    scene.Write("/World/Quad", tokens::kPoints, Tensor::Copy<float>(points, {4, 3}));
    const Tensor image = delegate.Render(scene);
    EXPECT_EQ(delegate.Meshes().MeshCount(), 2u);
    EXPECT_GT(TexelAt(scene, image, {0, -10, 0})[1], 200) << "the instance moved";
    EXPECT_EQ(TexelAt(scene, image, {12, -10, 0})[1], 0);
    EXPECT_GT(TexelAt(scene, image, {-12, 8, 0})[0], 200) << "the mesh deformed";
    EXPECT_EQ(TexelAt(scene, image, {-12, 0, 0})[0], 0);

    scene.Remove("/World/Rocks");
    const Tensor removed = delegate.Render(scene);
    EXPECT_EQ(delegate.Meshes().InstancerCount(), 0u);
    EXPECT_EQ(TexelAt(scene, removed, {12, 0, 0})[1], 0);
    EXPECT_EQ(delegate.Meshes().MeshCount(), 1u) << "the prototype went with its instancer";
}

TEST_F(RasterDelegateTest, ABadMeshDoesNotBlockTheOthers) {
    Scene scene = MeshScene();
    DefineQuad(scene, "/World/Broken");
    const std::array<std::int64_t, 4> bad{0, 1, 2, 9};
    scene.Write("/World/Broken", tokens::kFaceVertexIndices, Tensor::Copy<std::int64_t>(bad, {4}));
    RasterDelegate delegate(*context_);
    try {
        delegate.Sync(scene);
        FAIL() << "expected an upload error";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("'/World/Broken' mesh"), std::string::npos)
            << error.what();
    }
    const Tensor image = delegate.Render(scene);
    EXPECT_GT(TexelAt(scene, image, {-12, 0, 0})[0], 200) << "the good quad still draws";
}

TEST_F(RasterDelegateTest, CudaArraysAndCudaOutputMatchTheHostPath) {
    RasterDelegate delegate(*context_);
    std::string why;
    ernie::CudaInterop* cuda = delegate.Cuda(&why);
    if (cuda == nullptr) {
        GTEST_SKIP() << "No CUDA interop: " << why;
    }
    Scene host = MeshScene();
    RasterDelegate reference(*context_);
    const Tensor expected = reference.Render(host);

    Scene device = MeshScene();
    device.Write("/World/Rocks", tokens::kPositions,
                 cuda->Upload(*device.Read("/World/Rocks", tokens::kPositions)));
    device.Set("/World/Rocks", tokens::kExtent,
               std::vector<float>{-2.0f, -12.0f, -1.0f, 14.0f, 12.0f, 1.0f});
    device.Write("/World/Quad", tokens::kPoints,
                 cuda->Upload(*device.Read("/World/Quad", tokens::kPoints)));
    const Tensor image = delegate.Render(device);
    ASSERT_EQ(image.Elements(), expected.Elements());
    EXPECT_EQ(
        std::memcmp(image.Data(), expected.Data(), static_cast<std::size_t>(image.Elements())), 0)
        << "CUDA inputs draw the same image";

    const Tensor first = delegate.Render(device, {}, {ernie::DeviceKind::Cuda, cuda->Device()});
    EXPECT_EQ(first.Location().kind, ernie::DeviceKind::Cuda);
    EXPECT_EQ(first.Shape(), expected.Shape());
    const Tensor second = delegate.Render(device, {}, {ernie::DeviceKind::Cuda, cuda->Device()});
    EXPECT_NE(first.Data(), second.Data()) << "a live result is not overwritten";
    const Tensor downloaded = cuda->Download(first);
    EXPECT_EQ(std::memcmp(downloaded.Data(), expected.Data(),
                          static_cast<std::size_t>(expected.Elements())),
              0)
        << "the CUDA result holds the render";

    const std::array<float, 9> moved{12, -10, 0, 0, 0, 0, 12, 10, 0};
    device.Write("/World/Rocks", tokens::kPositions,
                 cuda->Upload(Tensor::Copy<float>(moved, {3, 3})));
    const Tensor after = delegate.Render(device);
    EXPECT_GT(TexelAt(device, after, {0, 0, 0})[1], 200) << "a moved CUDA instance";

    EXPECT_THROW(delegate.Render(device, {}, {ernie::DeviceKind::Cuda, cuda->Device() + 7}),
                 std::invalid_argument);
    RasterDelegate bgra(*context_, VK_FORMAT_B8G8R8A8_UNORM);
    EXPECT_THROW(bgra.Render(host, {}, {ernie::DeviceKind::Cuda, cuda->Device()}),
                 std::invalid_argument);
}

TEST(ExtentOf, ReadsValuesAndArrays) {
    Scene scene;
    scene.Define("/A", tokens::kMesh).Set(tokens::kExtent, std::vector<float>{0, 1, 2, 3, 4, 5});
    const auto extent = ernie::ExtentOf(scene, "/A");
    ASSERT_TRUE(extent.has_value());
    EXPECT_EQ(extent->second, glm::vec3(3.0f, 4.0f, 5.0f));
    const std::array<double, 6> values{-1, -1, -1, 1, 1, 1};
    scene.Write("/A", tokens::kExtent, Tensor::Copy<double>(values, {2, 3}));
    EXPECT_EQ(ernie::ExtentOf(scene, "/A")->first, glm::vec3(-1.0f));
    scene.Set("/A", tokens::kExtent, std::vector<float>{1, 2});
    EXPECT_FALSE(ernie::ExtentOf(scene, "/A").has_value());
    EXPECT_FALSE(ernie::ExtentOf(scene, "/Missing").has_value());
}
