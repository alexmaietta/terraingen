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

namespace {

/// FlatScene lit only by a point lamp `height` above the origin pointing down, with a radiant flux
/// of 1000 W over a flat spectrum and a cone of `coneAngle` degrees (180: unshaped).
Scene LampScene(double height, float coneAngle = 180.0f) {
    Scene scene = FlatScene();
    scene.Remove("/World/Sun");
    scene.Define("/World/Lamp", tokens::kSphereLight)
        .Set(tokens::kTransform,
             ernie::LookAtTransform({0.0, 0.0, height}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}))
        .Set(tokens::kTreatAsPoint, true)
        .Set(tokens::kLightPower, 1000.0)
        .Set(tokens::kShapingConeAngle, coneAngle);
    return scene;
}

/// The pixel of FlatScene's image showing ground point (x, y).
std::pair<std::size_t, std::size_t> PixelOf(const Scene& scene, float x, float y) {
    const glm::vec4 clip = ernie::CameraViewOf(scene, "/World/Camera", 1.0f).ViewProjection() *
                           glm::vec4(x, y, 0.0f, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return {static_cast<std::size_t>((0.5f - ndc.y * 0.5f) * kVarSize),
            static_cast<std::size_t>((ndc.x * 0.5f + 0.5f) * kVarSize)};
}

/// Spectral radiance in band 8 at ground point (x, y) of a step of `scene`.
float RadianceAt(RasterDelegate& delegate, const Scene& scene, float x, float y) {
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    const Tensor spectral =
        outputs.at("/Render/Product").vars.at("SpectralRadiance").Map(ernie::MapDevice::Cpu).tensor;
    const auto [row, col] = PixelOf(scene, x, y);
    return static_cast<const float*>(
        spectral.Data())[8 * kVarSize * kVarSize + row * kVarSize + col];
}

}  // namespace

TEST_F(RasterDelegateTest, LampsLightByTheInverseSquareLaw) {
    RasterDelegate delegate(*context_);
    const Scene near = LampScene(10.0);
    const float intensity = static_cast<float>(1000.0 / (4.0 * M_PI) / 470.0);
    const float x = 3.0f;
    const float distance2 = 100.0f + x * x;
    const float expected =
        intensity * (10.0f / std::sqrt(distance2)) / distance2 * 0.5f / static_cast<float>(M_PI);
    EXPECT_NEAR(RadianceAt(delegate, near, x, 0.0f), expected, 0.04f * expected)
        << "L = I cos(i) / d^2 * albedo / pi";
    EXPECT_FALSE(delegate.Prims().lights.empty());

    const float twice = RadianceAt(delegate, LampScene(20.0), 0.0f, 0.0f);
    const float once = RadianceAt(delegate, LampScene(10.0), 0.0f, 0.0f);
    EXPECT_NEAR(once / twice, 4.0f, 0.1f);

    const Scene spot = LampScene(10.0, 10.0f);
    EXPECT_NEAR(RadianceAt(delegate, spot, 0.0f, 0.0f), once, 0.02f * once) << "inside the cone";
    EXPECT_EQ(RadianceAt(delegate, spot, 6.0f, 0.0f), 0.0f) << "outside a 10 degree cone";
}

TEST_F(RasterDelegateTest, LampsCastShadowsFromSpotAndCubeMaps) {
    RasterDelegate delegate(*context_);
    for (const float cone : {60.0f, 180.0f}) {
        Scene scene = LampScene(10.0, cone);
        const float lit = RadianceAt(delegate, scene, 3.0f, 0.0f);
        DefineQuad(scene, "/World/Blocker");
        scene.Set("/World/Blocker", tokens::kTransform,
                  glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 5.0)));
        EXPECT_LT(RadianceAt(delegate, scene, 3.0f, 0.0f), 0.1f * lit)
            << "behind the blocker, cone " << cone;
        EXPECT_NEAR(RadianceAt(delegate, scene, 6.0f, 6.0f),
                    RadianceAt(delegate, LampScene(10.0, cone), 6.0f, 6.0f), 0.05f * lit)
            << "beside the shadow, cone " << cone;
        scene.Set("/World/Lamp", tokens::kShadowEnable, false);
        EXPECT_NEAR(RadianceAt(delegate, scene, 3.0f, 0.0f), lit, 0.02f * lit);
    }
    EXPECT_EQ(delegate.Renderer().LightShadowViews().size(), 0u) << "shadows off";
}

TEST_F(RasterDelegateTest, OtherDistantLightsAddToTheSun) {
    Scene scene = FlatScene();
    scene.Define("/World/Zenith", tokens::kDistantLight)
        .Set(tokens::kTransform, ernie::DistantLightTransform({0.0, 0.0, 1.0}))
        .Set(tokens::kIntensity, 0.5f);
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    const auto vars = MapVars(outputs);
    const std::size_t c = kVarSize / 2;
    EXPECT_NEAR(At<float>(vars.at("HdrColor"), c, c, 1), 0.75f, 0.03f)
        << "0.5 cd/m^2 from the sun and 0.25 from Earthshine";
    EXPECT_EQ(delegate.Renderer().LightShadowViews().size(), 1u);
}

TEST_F(RasterDelegateTest, TheSunsDiskWidensPenumbrae) {
    Scene scene = FlatScene();
    scene.Set("/World/Sun", tokens::kShadowEnable, true);
    scene.Set("/World/Sun", tokens::kTransform, ernie::DistantLightTransform({0.3, 0.0, 1.0}));
    DefineQuad(scene, "/World/Blocker", 4.0f);
    scene.Set("/World/Blocker", tokens::kTransform,
              glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 30.0)));
    RasterDelegate delegate(*context_);
    const auto partial = [&](float angle) {
        scene.Set("/World/Sun", tokens::kAngle, angle);
        ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
        const Tensor hdr =
            outputs.at("/Render/Product").vars.at("HdrColor").Map(ernie::MapDevice::Cpu).tensor;
        const Tensor ids =
            outputs.at("/Render/Product").vars.at("PrimId").Map(ernie::MapDevice::Cpu).tensor;
        const std::uint32_t ground = At<std::uint32_t>(ids, 0, 0);
        float lit = 0.0f;
        for (std::size_t row = 0; row < kVarSize; ++row) {
            for (std::size_t col = 0; col < kVarSize; ++col) {
                lit = std::max(lit, At<float>(hdr, row, col, 1));
            }
        }
        int count = 0;
        int shadowed = 0;
        for (std::size_t row = 0; row < kVarSize; ++row) {
            for (std::size_t col = 0; col < kVarSize; ++col) {
                if (At<std::uint32_t>(ids, row, col) != ground) {
                    continue;
                }
                const float value = At<float>(hdr, row, col, 1) / lit;
                count += value > 0.1f && value < 0.9f ? 1 : 0;
                shadowed += value <= 0.1f ? 1 : 0;
            }
        }
        return std::pair(count, shadowed);
    };
    const auto [hard, umbra] = partial(0.0f);
    EXPECT_GT(umbra, 20) << "the blocker's shadow is in view";
    const auto [soft, smaller] = partial(4.0f);
    EXPECT_GT(soft, hard + 10) << "a wider disk widens the penumbra";
    EXPECT_LT(smaller, umbra);
}

namespace {

/// FlatScene lit only by an area light of `type` 10 m above the origin facing down, with a
/// luminance of 1000 cd/m^2.
Scene AreaLightScene(std::string_view type) {
    Scene scene = FlatScene();
    scene.Remove("/World/Sun");
    scene.Define("/World/Panel", type)
        .Set(tokens::kTransform,
             ernie::LookAtTransform({0.0, 0.0, 10.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0}))
        .Set(tokens::kIntensity, 1000.0)
        .Set(tokens::kShadowEnable, false);
    return scene;
}

/// Spectral radiance of a flat 1 cd/m^2 white emitter, per band.
double UnitLuminanceRadiance() {
    double sum = 0.0;
    for (double w = 360.0; w <= 830.0; w += 1.0) {
        sum += ernie::CieXyz(w).y;
    }
    return 1.0 / (ernie::kLuminousEfficacy * sum);
}

/// Form factor from a point to a parallel rectangle `a` x `b` whose corner lies `h` above it.
double CornerFormFactor(double a, double b, double h) {
    const double x = a / h;
    const double y = b / h;
    const double sx = std::sqrt(1.0 + x * x);
    const double sy = std::sqrt(1.0 + y * y);
    return (x / sx * std::atan(y / sx) + y / sy * std::atan(x / sy)) / (2.0 * M_PI);
}

}  // namespace

TEST_F(RasterDelegateTest, RectAndDiskLightsMatchTheirFormFactors) {
    RasterDelegate delegate(*context_);
    const double radiance = 1000.0 * UnitLuminanceRadiance();

    Scene rect = AreaLightScene(tokens::kRectLight);
    rect.Set("/World/Panel", tokens::kWidth, 8.0);
    rect.Set("/World/Panel", tokens::kHeight, 6.0);
    const double rectExpected =
        0.5 / M_PI * radiance * M_PI * 4.0 * CornerFormFactor(4.0, 3.0, 10.0);
    EXPECT_NEAR(RadianceAt(delegate, rect, 0.0f, 0.0f), rectExpected, 0.03 * rectExpected)
        << "albedo / pi times E = pi L F";

    Scene disk = AreaLightScene(tokens::kDiskLight);
    disk.Set("/World/Panel", tokens::kRadius, 5.0);
    const double diskExpected = 0.5 * radiance * 25.0 / (25.0 + 100.0);
    EXPECT_NEAR(RadianceAt(delegate, disk, 0.0f, 0.0f), diskExpected, 0.03 * diskExpected)
        << "E = pi L r^2 / (r^2 + h^2) on the axis";

    disk.Set("/World/Panel", tokens::kTransform,
             ernie::LookAtTransform({0.0, 0.0, 10.0}, {0.0, 0.0, 20.0}, {0.0, 1.0, 0.0}));
    EXPECT_EQ(RadianceAt(delegate, disk, 0.0f, 0.0f), 0.0f) << "facing away";
    disk.Set("/World/Panel", tokens::kTransform,
             ernie::LookAtTransform({0.0, 0.0, 0.5}, {10.0, 0.0, 0.5}, {0.0, 0.0, 1.0}));
    const float grazing = RadianceAt(delegate, disk, 3.0f, 0.0f);
    EXPECT_GT(grazing, 0.0f) << "half the disk is above the ground's horizon";
    EXPECT_LT(grazing, static_cast<float>(diskExpected));
}

TEST_F(RasterDelegateTest, AreaLightsCastShadows) {
    RasterDelegate delegate(*context_);
    Scene scene = AreaLightScene(tokens::kRectLight);
    scene.Set("/World/Panel", tokens::kShadowEnable, true);
    const float lit = RadianceAt(delegate, scene, 3.0f, 0.0f);
    DefineQuad(scene, "/World/Blocker");
    scene.Set("/World/Blocker", tokens::kTransform,
              glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 5.0)));
    EXPECT_LT(RadianceAt(delegate, scene, 3.0f, 0.0f), 0.2f * lit) << "behind the blocker";
    EXPECT_EQ(delegate.Renderer().LightShadowViews().size(), 6u) << "a cube at the centre";
}

TEST_F(RasterDelegateTest, DomesLightEverySurfaceDiffusely) {
    Scene scene = FlatScene();
    scene.Remove("/World/Sun");
    scene.Define("/World/Sky", tokens::kDomeLight).Set(tokens::kIntensity, 1.0);
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    const auto vars = MapVars(outputs);
    const std::size_t c = kVarSize / 2;
    EXPECT_NEAR(At<float>(vars.at("HdrColor"), c, c, 1), 0.5f, 0.02f)
        << "a uniform 1 cd/m^2 dome gives albedo 0.5 a luminance of 0.5";
    EXPECT_FALSE(delegate.Renderer().Bands().count == 0);

    std::vector<float> map(8 * 16 * 3, 0.0f);
    std::fill(map.begin(), map.begin() + map.size() / 2, 1.0f);
    scene.Write("/World/Sky", tokens::kLightTexture, Tensor::Copy<float>(map, {8, 16, 3}));
    ernie::RenderProductSetOutputs upper = delegate.Step(scene);
    EXPECT_NEAR(At<float>(MapVars(upper).at("HdrColor"), c, c, 1), 0.5f, 0.03f)
        << "the upper hemisphere alone lights upward surfaces fully";
    scene.Set("/World/Sky", tokens::kTransform,
              glm::rotate(glm::dmat4(1.0), M_PI, glm::dvec3(1.0, 0.0, 0.0)));
    ernie::RenderProductSetOutputs lower = delegate.Step(scene);
    EXPECT_LT(At<float>(MapVars(lower).at("HdrColor"), c, c, 1), 0.03f) << "turned upside down";
}

TEST_F(RasterDelegateTest, DrawsUsdGeomPrimitivesAndInstancesThem) {
    Scene scene = MeshScene();
    scene.Remove("/World/Quad");
    scene.Remove("/World/Rocks/Rock");
    scene.Define("/World/Ball", tokens::kSphere)
        .Set(tokens::kGeomRadius, 3.0)
        .Set(tokens::kMaterialBinding, Path("/Looks/Red"))
        .Set(tokens::kTransform, glm::translate(glm::dmat4(1.0), glm::dvec3(-12.0, 0.0, 0.0)));
    scene.Define("/World/Rocks/Rock", tokens::kCube)
        .Set(tokens::kSize, 3.0)
        .Set(tokens::kDisplayColor, glm::vec3(0.0f, 1.0f, 0.0f));
    RasterDelegate delegate(*context_);
    const Tensor image = delegate.Render(scene);
    EXPECT_EQ(delegate.Meshes().MeshCount(), 2u);
    const std::uint8_t* ball = TexelAt(scene, image, {-12, 0, 3});
    EXPECT_GT(ball[0], 150);
    EXPECT_LT(ball[1] + ball[2], ball[0] / 2) << "the sphere, red from its material";
    for (const float y : {-10.0f, 0.0f, 10.0f}) {
        const std::uint8_t* rock = TexelAt(scene, image, {12, y, 1.5f});
        EXPECT_GT(rock[1], 150) << "a cube prototype instanced at y " << y;
    }
    const std::uint8_t* origin = TexelAt(scene, image, {0, 0, 0});
    EXPECT_EQ(origin[0] + origin[1] + origin[2], 0) << "the prototype draws only as instances";

    scene.Set("/World/Ball", tokens::kGeomRadius, 0.5);
    const Tensor smaller = delegate.Render(scene);
    const std::uint8_t* rim = TexelAt(scene, smaller, {-10, 0, 0});
    EXPECT_EQ(rim[0], 0) << "editing the radius regenerates the mesh";
}

namespace {

/// FlatScene with its terrain replaced by a grey GroundPlane at `height`.
Scene GroundScene(double height) {
    Scene scene = FlatScene();
    scene.Remove("/World/Terrain");
    scene.Define("/World/Ground", tokens::kGroundPlane)
        .Set(tokens::kGroundHeight, height)
        .Set(tokens::kMaterialBinding, Path("/Looks/Grey"));
    return scene;
}

}  // namespace

TEST_F(RasterDelegateTest, GroundPlanesAreLitAndSortedLikeSurfaces) {
    Scene scene = GroundScene(10.0);
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    auto vars = MapVars(outputs);
    const std::size_t c = kVarSize / 2;
    EXPECT_TRUE(delegate.Ground().Enabled());
    EXPECT_NEAR(At<float>(vars.at("DistanceToImagePlane"), c, c), 90.0f, 0.5f);
    EXPECT_NEAR(At<float>(vars.at("DistanceToImagePlane"), 0, 0), 90.0f, 0.5f)
        << "a flat plane under a nadir camera";
    EXPECT_NEAR(At<float>(vars.at("Normal"), c, c, 2), 1.0f, 1e-4f);
    EXPECT_NEAR(At<float>(vars.at("HdrColor"), c, c, 1), 0.5f, 0.02f) << "albedo 0.5 under the sun";
    EXPECT_EQ(delegate.PrimPath(At<std::uint32_t>(vars.at("PrimId"), 0, 0)), Path("/World/Ground"));

    Scene withTerrain = FlatScene();
    withTerrain.Define("/World/Ground", tokens::kGroundPlane).Set(tokens::kGroundHeight, -5.0);
    ernie::RenderProductSetOutputs both = delegate.Step(withTerrain);
    vars = MapVars(both);
    EXPECT_EQ(delegate.PrimPath(At<std::uint32_t>(vars.at("PrimId"), c, c)), Path("/World/Terrain"))
        << "the terrain is in front of a plane below it";
    EXPECT_NEAR(At<float>(vars.at("DistanceToImagePlane"), c, c), 100.0f, 0.5f);

    withTerrain.Set("/World/Ground", tokens::kVisibility, tokens::kInvisible);
    delegate.Step(withTerrain);
    EXPECT_FALSE(delegate.Ground().Enabled());
}

TEST_F(RasterDelegateTest, GroundPlanesReachTheHorizonWithACheckerboard) {
    Scene scene = GroundScene(0.0);
    scene.Set("/World/Ground", tokens::kChecker, true);
    scene.Set("/World/Ground", tokens::kCheckerSize, 4.0);
    scene.Set("/World/Ground", tokens::kCheckerColor1, glm::vec3(0.0f));
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    const auto vars = MapVars(outputs);
    int light = 0;
    int dark = 0;
    for (std::size_t row = 0; row < kVarSize; ++row) {
        for (std::size_t col = 0; col < kVarSize; ++col) {
            const float value = At<float>(vars.at("HdrColor"), row, col, 1);
            light += value > 0.4f ? 1 : 0;
            dark += value < 0.1f ? 1 : 0;
        }
    }
    EXPECT_GT(light, 300) << "4 m squares at about 1 m per pixel are crisp";
    EXPECT_GT(dark, 300);

    scene.Set("/World/Camera", tokens::kTransform,
              ernie::LookAtTransform({0.0, 0.0, 2.0}, {0.0, 1000.0, 1.0}, {0.0, 0.0, 1.0}));
    ernie::RenderProductSetOutputs horizon = delegate.Step(scene);
    const auto far = MapVars(horizon);
    const std::size_t c = kVarSize / 2;
    const std::uint32_t top = At<std::uint32_t>(far.at("PrimId"), 0, c);
    const std::uint32_t bottom = At<std::uint32_t>(far.at("PrimId"), kVarSize - 1, c);
    EXPECT_EQ(top, 0u) << "sky above the horizon";
    EXPECT_EQ(delegate.PrimPath(bottom), Path("/World/Ground"));
    std::size_t lastGround = 0;
    for (std::size_t row = 0; row < kVarSize; ++row) {
        if (At<std::uint32_t>(far.at("PrimId"), row, c) != 0) {
            lastGround = row;
            break;
        }
    }
    EXPECT_GT(lastGround, kVarSize / 2 - 3) << "the plane reaches up to the horizon";
    EXPECT_LT(lastGround, kVarSize / 2 + 3);
    const float distant = At<float>(far.at("HdrColor"), lastGround, c, 1);
    EXPECT_NEAR(distant, 0.25f, 0.1f) << "the filtered checker fades to its mean far away";
}

TEST_F(RasterDelegateTest, PrimitivesStandOnTheGroundAndShadowIt) {
    Scene scene = GroundScene(0.0);
    scene.Set("/World/Sun", tokens::kShadowEnable, true);
    scene.Set("/World/Sun", tokens::kTransform, ernie::DistantLightTransform({1.0, 0.0, 1.0}));
    scene.Define("/World/Block", tokens::kCube)
        .Set(tokens::kSize, 6.0)
        .Set(tokens::kTransform, glm::translate(glm::dmat4(1.0), glm::dvec3(0.0, 0.0, 3.0)));
    RasterDelegate delegate(*context_);
    ernie::RenderProductSetOutputs outputs = delegate.Step(scene);
    const auto vars = MapVars(outputs);
    const auto [row, col] = PixelOf(scene, 0.0f, 0.0f);
    EXPECT_EQ(delegate.PrimPath(At<std::uint32_t>(vars.at("PrimId"), row, col)),
              Path("/World/Block"));
    const auto [shadowRow, shadowCol] = PixelOf(scene, -5.0f, 0.0f);
    const auto [litRow, litCol] = PixelOf(scene, 5.0f, 5.0f);
    EXPECT_EQ(delegate.PrimPath(At<std::uint32_t>(vars.at("PrimId"), shadowRow, shadowCol)),
              Path("/World/Ground"));
    EXPECT_LT(At<float>(vars.at("HdrColor"), shadowRow, shadowCol, 1),
              0.2f * At<float>(vars.at("HdrColor"), litRow, litCol, 1))
        << "the cube shadows the ground away from the sun";
}
