#include "ernie/gpu/offscreen_target.h"
#include "ernie/raster/raster_delegate.h"
#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"
#include "support/test_raster.h"
#include "support/vulkan_fixture.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <map>
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
    direct.Update(ernie::FrameParamsOf(scene, delegate.Prims(), kSize, kSize), parts);
    ernie::OffscreenTarget target(*context_, kSize, kSize);
    ernie::RenderTargets targets(*context_, {kSize, kSize}, direct.Bands().count, false);
    const ernie::RasterPart* drawn[] = {&terrainPart};
    context_->Submit([&](VkCommandBuffer commands) {
        const ernie::RenderingTarget present = target.Prepare(commands);
        direct.Record(commands, targets, drawn, &present);
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

TEST_F(RasterDelegateTest, OutputsDoNotDependOnThePresentFormat) {
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
    const Tensor c = wide.Render(scene, "/Render/Settings");
    EXPECT_EQ(std::memcmp(a.Data(), c.Data(), static_cast<std::size_t>(a.Elements())), 0);
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
    EXPECT_LT(quad[1] + quad[2], quad[0] / 2) << "red from its material";
    for (const float y : {-10.0f, 0.0f, 10.0f}) {
        const std::uint8_t* rock = TexelAt(scene, image, {12, y, 0});
        EXPECT_GT(rock[1], 200) << "green from displayColor, instance at y " << y;
        EXPECT_LT(rock[0] + rock[2], rock[1] / 2);
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

namespace {

/// MeshScene with two RenderProducts: the overhead camera at 32 x 32, and a second camera to the
/// side at 16 x 24, each with an LdrColor RenderVar.
Scene ProductScene() {
    Scene scene = MeshScene();
    scene.Define("/World/Side", tokens::kCamera)
        .Set(tokens::kTransform,
             ernie::LookAtTransform({60.0, 0.0, 20.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}))
        .Set(tokens::kFocalLength, 18.0f);
    scene.Define("/Render/Vars/Color", tokens::kRenderVar).Set(tokens::kSourceName, "LdrColor");
    scene.Define("/Render/Top", tokens::kRenderProduct)
        .Set(tokens::kCameraRel, Path("/World/Camera"))
        .Set(tokens::kResolution, glm::ivec2(32, 32))
        .Set(tokens::kOrderedVars, std::vector<Path>{"/Render/Vars/Color"});
    scene.Define("/Render/SideView", tokens::kRenderProduct)
        .Set(tokens::kCameraRel, Path("/World/Side"))
        .Set(tokens::kResolution, glm::ivec2(16, 24));
    scene.Set("/Render/Settings", tokens::kProducts,
              std::vector<Path>{"/Render/Top", "/Render/SideView"});
    return scene;
}

}  // namespace

TEST(ResolveRenderProducts, FollowsUsdRender) {
    Scene scene = ProductScene();
    std::vector<ernie::RenderProductSpec> specs = ernie::ResolveRenderProducts(scene, {}, {});
    ASSERT_EQ(specs.size(), 2u);
    EXPECT_EQ(specs[0].product, Path("/Render/Top"));
    EXPECT_EQ(specs[0].resolution.width, 32u);
    EXPECT_EQ(specs[0].vars.front().first, Path("/Render/Vars/Color"));
    EXPECT_EQ(specs[1].camera, Path("/World/Side"));
    EXPECT_EQ(specs[1].resolution.height, 24u);
    EXPECT_EQ(specs[1].vars.front().second, "LdrColor") << "default var";

    const Path only[] = {"/Render/SideView"};
    EXPECT_EQ(ernie::ResolveRenderProducts(scene, only, {}).size(), 1u);
    scene.Clear("/Render/Settings", tokens::kProducts);
    specs = ernie::ResolveRenderProducts(scene, {}, {});
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].product, Path("/Render/Settings")) << "default product";
    EXPECT_EQ(specs[0].resolution.width, kSize);

    const Path bad[] = {"/World/Camera"};
    EXPECT_THROW(ernie::ResolveRenderProducts(scene, bad, {}), std::invalid_argument);
    scene.Set("/Render/Vars/Color", tokens::kSourceName, "Depth");
    const Path top[] = {"/Render/Top"};
    EXPECT_THROW(ernie::ResolveRenderProducts(scene, top, {}), std::invalid_argument);
}

TEST_F(RasterDelegateTest, StepsRenderEveryProductSyncOrAsync) {
    Scene scene = ProductScene();
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    ASSERT_EQ(outputs.size(), 2u);
    ernie::RenderVar& top = outputs.at("/Render/Top").vars.at("LdrColor");
    EXPECT_EQ(top.GetPath(), Path("/Render/Vars/Color"));
    const Tensor topImage = top.Map(ernie::MapDevice::Cpu).tensor;
    EXPECT_EQ(topImage.Shape(), (std::vector<std::int64_t>{32, 32, 4}));
    const Tensor side =
        outputs.at("/Render/SideView").vars.at("LdrColor").Map(ernie::MapDevice::Cpu).tensor;
    EXPECT_EQ(side.Shape(), (std::vector<std::int64_t>{24, 16, 4}));
    scene.Define("/Render/TopSmall", tokens::kRenderProduct)
        .Set(tokens::kCameraRel, Path("/World/Camera"))
        .Set(tokens::kResolution, glm::ivec2(16, 24));
    const Path pair[] = {"/Render/TopSmall", "/Render/SideView"};
    ernie::RenderProductSetOutputs both = delegate.Step(scene, pair);
    const Tensor small =
        both.at("/Render/TopSmall").vars.at("LdrColor").Map(ernie::MapDevice::Cpu).tensor;
    const Tensor sideAgain =
        both.at("/Render/SideView").vars.at("LdrColor").Map(ernie::MapDevice::Cpu).tensor;
    EXPECT_NE(
        std::memcmp(small.Data(), sideAgain.Data(), static_cast<std::size_t>(small.Elements())), 0)
        << "each product renders through its own camera";
    EXPECT_EQ(std::memcmp(side.Data(), sideAgain.Data(), static_cast<std::size_t>(side.Elements())),
              0);

    ernie::StepOperation operation = delegate.StepAsync(scene);
    EXPECT_TRUE(operation.Wait());
    EXPECT_TRUE(operation.Done());
    const Tensor again =
        operation.Fetch().at("/Render/Top").vars.at("LdrColor").Map(ernie::MapDevice::Cpu).tensor;
    EXPECT_EQ(
        std::memcmp(again.Data(), topImage.Data(), static_cast<std::size_t>(topImage.Elements())),
        0)
        << "async and sync steps render the same";
}

TEST_F(RasterDelegateTest, FramesHeldByOutputsAreNotRenderedOver) {
    Scene scene = ProductScene();
    RasterDelegate delegate(*context_);
    const Path top[] = {"/Render/Top"};
    ernie::RenderProductSetOutputs first = delegate.Step(scene, top);
    const Tensor before =
        first.at("/Render/Top").vars.at("LdrColor").Map(ernie::MapDevice::Cpu).tensor;
    scene.Set("/Looks/Red", tokens::kAlbedo, glm::vec3(0.0f, 0.0f, 1.0f));
    ernie::RenderProductSetOutputs second = delegate.Step(scene, top);
    const Tensor held =
        first.at("/Render/Top").vars.at("LdrColor").Map(ernie::MapDevice::Cpu).tensor;
    EXPECT_EQ(std::memcmp(held.Data(), before.Data(), static_cast<std::size_t>(before.Elements())),
              0)
        << "the held frame kept its image";
    const Tensor after =
        second.at("/Render/Top").vars.at("LdrColor").Map(ernie::MapDevice::Cpu).tensor;
    EXPECT_NE(std::memcmp(after.Data(), before.Data(), static_cast<std::size_t>(before.Elements())),
              0);
}

TEST_F(RasterDelegateTest, MapsRenderVarsToCudaArraysWithoutCopying) {
    RasterDelegate delegate(*context_);
    std::string why;
    ernie::CudaInterop* cuda = delegate.Cuda(&why);
    if (cuda == nullptr) {
        GTEST_SKIP() << "No CUDA interop: " << why;
    }
    Scene scene = ProductScene();
    const Path top[] = {"/Render/Top"};
    ernie::StepOperation operation = delegate.StepAsync(scene, top);
    ernie::RenderVar var = operation.Outputs().at("/Render/Top").vars.at("LdrColor");
    const Tensor expected = var.Map(ernie::MapDevice::Cpu).tensor;

    const ernie::MappedRenderVar array = var.Map(ernie::MapDevice::CudaArray);
    EXPECT_NE(array.array.array, 0u);
    EXPECT_TRUE(var.IsMapped());
    const Tensor fromArray = cuda->DownloadArray(array.array);
    EXPECT_EQ(std::memcmp(fromArray.Data(), expected.Data(),
                          static_cast<std::size_t>(expected.Elements())),
              0);
    EXPECT_EQ(var.Map(ernie::MapDevice::CudaArray).array.array, array.array.array)
        << "the frame's own image";
    var.Unmap();
    var.Unmap();
    EXPECT_FALSE(var.IsMapped());

    const ernie::MappedRenderVar linear = var.Map(ernie::MapDevice::Cuda);
    EXPECT_EQ(linear.tensor.Location().kind, ernie::DeviceKind::Cuda);
    const Tensor fromLinear = cuda->Download(linear.tensor);
    EXPECT_EQ(std::memcmp(fromLinear.Data(), expected.Data(),
                          static_cast<std::size_t>(expected.Elements())),
              0);

    var = {};
    operation = delegate.StepAsync(scene, top);
    EXPECT_TRUE(operation.Wait());
}

TEST_F(RasterDelegateTest, BoundAttributesAreWrittenInPlaceByCuda) {
    RasterDelegate delegate(*context_);
    std::string why;
    ernie::CudaInterop* cuda = delegate.Cuda(&why);
    if (cuda == nullptr) {
        GTEST_SKIP() << "No CUDA interop: " << why;
    }
    Scene scene = MeshScene();
    scene.Set("/World/Rocks", tokens::kExtent,
              std::vector<float>{-14.0f, -14.0f, -1.0f, 14.0f, 14.0f, 1.0f});
    ernie::AttributeBinding positions = delegate.Bind(scene, "/World/Rocks", tokens::kPositions);
    EXPECT_EQ(positions.GetPath(), Path("/World/Rocks"));

    const Tensor array = positions.Map();
    EXPECT_TRUE(positions.IsMapped());
    EXPECT_EQ(array.Location().kind, ernie::DeviceKind::Cuda);
    EXPECT_EQ(array.Shape(), (std::vector<std::int64_t>{3, 3}));
    EXPECT_EQ(cuda->Download(array).As<float>()[0], 12.0f) << "the synced positions";
    const std::array<float, 9> moved{0, 0, 0, 12, 0, 0, 12, 10, 0};
    cuda->Copy(cuda->Upload(Tensor::Copy<float>(moved, {3, 3})), array);
    positions.Unmap();
    EXPECT_FALSE(positions.IsMapped());

    Tensor image = delegate.Render(scene);
    EXPECT_GT(TexelAt(scene, image, {0, 0, 0})[1], 200) << "drawn from the CUDA write";
    EXPECT_EQ(TexelAt(scene, image, {12, -10, 0})[1], 0);
    EXPECT_EQ(scene.Read("/World/Rocks", tokens::kPositions)->Location().kind,
              ernie::DeviceKind::Cpu)
        << "the scene's array was not touched";

    ernie::AttributeBinding points = delegate.Bind(scene, "/World/Quad", tokens::kPoints);
    const Tensor quad = points.Map();
    const std::array<float, 12> shifted{-2, 6, 0, 2, 6, 0, 2, 10, 0, -2, 10, 0};
    cuda->Copy(cuda->Upload(Tensor::Copy<float>(shifted, {4, 3})), quad);
    points.Unmap();

    DefineQuad(scene, "/World/Extra", 1.0f);
    scene.Set("/World/Extra", tokens::kTransform,
              glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, -12.0, 0.0)));
    image = delegate.Render(scene);
    EXPECT_GT(TexelAt(scene, image, {0, 0, 0})[1], 200) << "bound positions survive a rebuild";
    EXPECT_GT(TexelAt(scene, image, {-12, 8, 0})[0], 200) << "bound points survive a rebuild";
    EXPECT_GT(TexelAt(scene, image, {0, -12, 0})[0] + TexelAt(scene, image, {0, -12, 0})[1], 0)
        << "the added mesh draws";

    const std::array<float, 9> again{12, -10, 0, 12, 0, 0, 12, 10, 0};
    scene.Write("/World/Rocks", tokens::kPositions, Tensor::Copy<float>(again, {3, 3}));
    image = delegate.Render(scene);
    EXPECT_GT(TexelAt(scene, image, {12, -10, 0})[1], 200) << "the scene takes over again";

    EXPECT_THROW(delegate.Bind(scene, "/World/Rocks", tokens::kPoints), std::invalid_argument);
    EXPECT_THROW(delegate.Bind(scene, "/World/Camera", tokens::kPoints), std::invalid_argument);
    EXPECT_THROW(ernie::AttributeBinding().Map(), std::invalid_argument);
}

TEST(ResolveRenderProducts, ReadsSensorFilterChains) {
    Scene scene = ProductScene();
    scene.Define("/Render/Filters/Cie", tokens::kSensorFilter).Set(tokens::kFilterType, "cie");
    scene.Define("/Render/Filters/Exposure", tokens::kSensorFilter)
        .Set(tokens::kFilterType, "exposure");
    scene.Set("/Render/Top", tokens::kFilters,
              std::vector<Path>{"/Render/Filters/Cie", "/Render/Filters/Exposure"});
    const Path top[] = {"/Render/Top"};
    const std::vector<ernie::RenderProductSpec> specs =
        ernie::ResolveRenderProducts(scene, top, {});
    ASSERT_EQ(specs.size(), 1u);
    EXPECT_EQ(specs[0].filters, (std::vector<ernie::SensorFilter>{ernie::SensorFilter::Cie,
                                                                  ernie::SensorFilter::Exposure}));

    scene.Define("/Render/Vars/Dn", tokens::kRenderVar).Set(tokens::kSourceName, "RawDN");
    scene.Set("/Render/Top", tokens::kOrderedVars, std::vector<Path>{"/Render/Vars/Dn"});
    EXPECT_THROW(ernie::ResolveRenderProducts(scene, top, {}), std::invalid_argument)
        << "the chain has no ADC";
    scene.Set("/Render/Top", tokens::kOrderedVars, std::vector<Path>{"/Render/Vars/Color"});
    scene.Set("/Render/Filters/Cie", tokens::kFilterType, "bloom");
    EXPECT_THROW(ernie::ResolveRenderProducts(scene, top, {}), std::invalid_argument);
    scene.Set("/Render/Top", tokens::kFilters, std::vector<Path>{"/World/Camera"});
    EXPECT_THROW(ernie::ResolveRenderProducts(scene, top, {}), std::invalid_argument);
    scene.Define("/Render/Filters/Adc", tokens::kSensorFilter).Set(tokens::kFilterType, "adc");
    scene.Set("/Render/Top", tokens::kFilters, std::vector<Path>{"/Render/Filters/Adc"});
    EXPECT_THROW(ernie::ResolveRenderProducts(scene, top, {}), std::invalid_argument)
        << "adc needs qe";
}

namespace {

constexpr std::uint32_t kVarSize = 32;

/// A flat grey (albedo 0.5) Hillshade terrain 100 m below a camera looking straight down, lit by
/// a relative white sun overhead without shadows, rendering every var at 32 x 32.
Scene FlatScene() {
    Scene scene;
    scene.Define("/World/Terrain", tokens::kTerrain)
        .Write(tokens::kHeights, HeightsOf(TestRaster::Centred(65, 65, 2.0)))
        .Set(tokens::kOrigin, glm::dvec2(-65.0, 65.0))
        .Set(tokens::kCellSize, 2.0)
        .Set(tokens::kMaterialBinding, Path("/Looks/Grey"));
    scene.Define("/Looks/Grey", tokens::kMaterial).Set(tokens::kAlbedo, glm::vec3(0.5f));
    scene.Define("/World/Sun", tokens::kDistantLight)
        .Set(tokens::kTransform, ernie::DistantLightTransform({0.0, 0.0, 1.0}))
        .Set(tokens::kShadowEnable, false);
    scene.Define("/World/Camera", tokens::kCamera)
        .Set(tokens::kTransform,
             ernie::LookAtTransform({0.0, 0.0, 100.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}))
        .Set(tokens::kSensorNoise, false);
    std::vector<Path> vars;
    for (const std::string_view name : ernie::RenderTargets::VarNames()) {
        const Path var = Path("/Render/Vars").Child(std::string(name));
        scene.Define(var, tokens::kRenderVar).Set(tokens::kSourceName, std::string(name));
        vars.push_back(var);
    }
    scene.Define("/Render/Settings", tokens::kRenderSettings)
        .Set(tokens::kCameraRel, Path("/World/Camera"))
        .Set(tokens::kResolution, glm::ivec2(kVarSize, kVarSize))
        .Set(tokens::kColorMode, tokens::kHillshade)
        .Set(tokens::kAmbient, 0.0f);
    scene.Define("/Render/Product", tokens::kRenderProduct).Set(tokens::kOrderedVars, vars);
    scene.Set("/Render/Settings", tokens::kProducts, std::vector<Path>{"/Render/Product"});
    return scene;
}

/// Every var of `outputs`' product mapped to the CPU.
std::map<std::string, Tensor, std::less<>> MapVars(ernie::RenderProductSetOutputs& outputs) {
    std::map<std::string, Tensor, std::less<>> tensors;
    for (auto& [name, var] : outputs.at("/Render/Product").vars) {
        tensors[name] = var.Map(ernie::MapDevice::Cpu).tensor;
    }
    return tensors;
}

template <typename T>
T At(const Tensor& tensor, std::size_t row, std::size_t col, std::size_t channel = 0) {
    const std::size_t channels = tensor.Shape().size() == 3 ? tensor.Shape()[2] : 1;
    return static_cast<const T*>(tensor.Data())[(row * kVarSize + col) * channels + channel];
}

}  // namespace

TEST_F(RasterDelegateTest, RenderVarsMeasureTheScene) {
    Scene scene = FlatScene();
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    const auto vars = MapVars(outputs);
    ASSERT_EQ(vars.size(), ernie::RenderTargets::VarNames().size());
    const std::size_t c = kVarSize / 2;

    const Tensor& spectral = vars.at("SpectralRadiance");
    ASSERT_EQ(spectral.Shape(), (std::vector<std::int64_t>{16, kVarSize, kVarSize}));
    EXPECT_EQ(spectral.Type(), DType::Float32);
    double luminanceIntegral = 0.0;
    for (double w = 360.0; w <= 830.0; w += 1.0) {
        luminanceIntegral += ernie::CieXyz(w).y;
    }
    const float* radiance = static_cast<const float*>(spectral.Data());
    const double expected = 0.5 / (ernie::kLuminousEfficacy * luminanceIntegral);
    EXPECT_NEAR(radiance[8 * kVarSize * kVarSize + c * kVarSize + c], expected, 0.03 * expected)
        << "L = E / pi * albedo for a flat sun of white luminance 1 cd/m^2";

    const Tensor& hdr = vars.at("HdrColor");
    EXPECT_EQ(hdr.Shape(), (std::vector<std::int64_t>{kVarSize, kVarSize, 4}));
    for (std::size_t k = 0; k < 3; ++k) {
        EXPECT_NEAR(At<float>(hdr, c, c, k), 0.5f, 0.02f) << "neutral grey of 0.5 cd/m^2";
    }
    const int ldr = At<std::uint8_t>(vars.at("LdrColor"), c, c);
    EXPECT_NEAR(ldr, 188, 3) << "sRGB encoding of 0.5";

    EXPECT_NEAR(At<float>(vars.at("DistanceToImagePlane"), c, c), 100.0f, 0.5f);
    EXPECT_NEAR(At<float>(vars.at("DistanceToCamera"), c, c), 100.0f, 0.5f);
    EXPECT_GT(At<float>(vars.at("DistanceToCamera"), 0, 0),
              At<float>(vars.at("DistanceToImagePlane"), 0, 0))
        << "off-axis pixels are further from the eye than the image plane";
    EXPECT_NEAR(At<float>(vars.at("Normal"), c, c, 2), 1.0f, 1e-3f);

    const std::uint32_t id = At<std::uint32_t>(vars.at("PrimId"), c, c);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(delegate.PrimPath(id), Path("/World/Terrain"));
    EXPECT_EQ(delegate.PrimPath(0), Path());
    EXPECT_EQ(At<std::uint32_t>(vars.at("InstanceId"), c, c), 0u);

    const ernie::PhysicalCamera camera =
        ernie::PhysicalCameraOf(scene, "/World/Camera", kVarSize, kVarSize);
    const float electrons = At<float>(vars.at("Electrons"), c, c, 1);
    EXPECT_GT(electrons, 0.0f);
    EXPECT_EQ(At<float>(vars.at("Electrons"), c, c, 3), 0.0f) << "an RGB sensor has 3 channels";
    const double perDn = camera.sensor.ElectronsPerDn(camera.iso);
    EXPECT_EQ(vars.at("RawDN").Type(), DType::UInt16);
    EXPECT_NEAR(At<std::uint16_t>(vars.at("RawDN"), c, c, 1), electrons / perDn, 0.5 + 1e-3);
}

TEST_F(RasterDelegateTest, ExposureNoiseAndDistortionFollowTheCamera) {
    Scene scene = FlatScene();
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs plain = delegate.Step(scene);
    const auto before = MapVars(plain);
    const std::size_t c = kVarSize / 2;

    scene.Set("/World/Camera", tokens::kCameraExposure, 1.0f);
    ernie::RenderProductSetOutputs brighter = delegate.Step(scene);
    const auto stop = MapVars(brighter);
    EXPECT_FLOAT_EQ(At<float>(stop.at("HdrColor"), c, c), At<float>(before.at("HdrColor"), c, c))
        << "exposure is photographic only";
    EXPECT_EQ(At<std::uint8_t>(stop.at("LdrColor"), c, c), 255) << "2 x 0.5 is white";

    scene.Set("/World/Camera", tokens::kCameraExposure, 0.0f);
    scene.Set("/World/Camera", tokens::kSensorNoise, true);
    ernie::RenderProductSetOutputs first = delegate.Step(scene);
    ernie::RenderProductSetOutputs second = delegate.Step(scene);
    const Tensor a =
        first.at("/Render/Product").vars.at("Electrons").Map(ernie::MapDevice::Cpu).tensor;
    const Tensor b =
        second.at("/Render/Product").vars.at("Electrons").Map(ernie::MapDevice::Cpu).tensor;
    double mean = 0.0;
    double clean = 0.0;
    int differing = 0;
    for (std::size_t row = 0; row < kVarSize; ++row) {
        for (std::size_t col = 0; col < kVarSize; ++col) {
            mean += At<float>(a, row, col, 1);
            clean += At<float>(before.at("Electrons"), row, col, 1);
            differing += At<float>(a, row, col, 1) != At<float>(b, row, col, 1) ? 1 : 0;
        }
    }
    EXPECT_GT(differing, static_cast<int>(kVarSize * kVarSize / 2)) << "temporal noise";
    EXPECT_NEAR(mean / clean, 1.0, 0.02) << "noise keeps the mean";

    scene.Set("/World/Camera", tokens::kSensorNoise, false);
    scene.Set("/World/Camera", tokens::kLensDistortion, std::vector<float>{-0.3f, 0, 0, 0, 0});
    ernie::RenderProductSetOutputs distorted = delegate.Step(scene);
    const auto barrel = MapVars(distorted);
    EXPECT_NEAR(At<float>(barrel.at("DistanceToCamera"), c, c), 100.0f, 0.5f);
    EXPECT_NEAR(At<float>(barrel.at("HdrColor"), c, c), 0.5f, 0.02f);
    EXPECT_NE(At<std::uint32_t>(barrel.at("PrimId"), 0, 0), 0u) << "corners stay covered";
    EXPECT_NE(At<float>(barrel.at("DistanceToCamera"), 0, 0),
              At<float>(before.at("DistanceToCamera"), 0, 0))
        << "barrel distortion sees wider at the corners";
}
