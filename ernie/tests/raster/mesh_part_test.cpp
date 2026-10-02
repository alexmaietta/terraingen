#include "ernie/camera/orbit_camera.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/interop/cuda_interop.h"
#include "ernie/raster/mesh_part.h"
#include "ernie/raster/raster_renderer.h"
#include "ernie/raster/terrain_part.h"
#include "support/test_raster.h"
#include "support/vulkan_fixture.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using ernie::FrameParams;
using ernie::InstanceData;
using ernie::MeshData;
using ernie::MeshItem;
using ernie::MeshPart;
using ernie::OffscreenTarget;
using ernie::OrbitCamera;
using ernie::RasterPart;
using ernie::RasterRenderer;
using ernie::Tensor;
using ernie::TerrainPart;
using ernie::testing::TestRaster;

namespace {

constexpr std::uint32_t kSize = 64;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct Pixel {
    std::uint8_t r, g, b, a;

    int Luminance() const {
        return r + g + b;
    }

    bool operator==(const Pixel&) const = default;
};

/// A unit square in the x-y plane centred on the origin, as two triangles, optionally with
/// upward normals and per-vertex colours.
MeshData Square(float half = 0.5f, bool normals = false, bool colors = false) {
    MeshData mesh;
    mesh.points = {-half, -half, 0.0f, half, -half, 0.0f, half, half, 0.0f, -half, half, 0.0f};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    if (normals) {
        mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    }
    if (colors) {
        mesh.colors = {1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0};
    }
    return mesh;
}

/// A closed axis-aligned box of half-size `half` centred on the origin.
MeshData Box(float half) {
    MeshData mesh;
    for (int i = 0; i < 8; ++i) {
        mesh.points.push_back((i & 1) ? half : -half);
        mesh.points.push_back((i & 2) ? half : -half);
        mesh.points.push_back((i & 4) ? half : -half);
    }
    mesh.indices = {0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1,
                    2, 3, 7, 2, 7, 6, 0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3};
    return mesh;
}

/// A renderer with a terrain part and a mesh part, as `RasterDelegate` sets them up.
struct Frame {
    RasterRenderer renderer;
    TerrainPart terrain;
    MeshPart meshes;

    explicit Frame(const ernie::VulkanContext& context)
        : renderer(context, kColorFormat), terrain(renderer), meshes(renderer) {
        ernie::TerrainParams params;
        params.colorMode = ernie::TerrainColorMode::Hillshade;
        terrain.SetParams(params);
    }
};

class MeshPartTest : public ernie::testing::VulkanTest {
  protected:
    /// Draws the frame from straight above the origin, with a sun overhead unless `sun` is given.
    std::vector<Pixel> Render(Frame& frame, float distance = 40.0f,
                              glm::vec3 sun = {0.0f, 0.0f, 1.0f}, bool shadows = true) {
        FrameParams params = View(distance);
        params.sunDirection = glm::normalize(sun);
        params.ambient = 0.0f;
        params.shadows.enabled = shadows;
        RasterPart* parts[] = {&frame.terrain, &frame.meshes};
        frame.renderer.Update(params, parts);
        OffscreenTarget target(*context_, kSize, kSize, kColorFormat);
        ernie::RenderTargets targets(*context_, {kSize, kSize}, params.physical.bands.count, false);
        const RasterPart* drawn[] = {&frame.terrain, &frame.meshes};
        context_->Submit([&](VkCommandBuffer commands) {
            const ernie::RenderingTarget present =
                target.Prepare(commands, {{0.0f, 0.0f, 0.0f, 1.0f}});
            frame.renderer.Record(commands, targets, drawn, &present);
        });
        const std::vector<std::uint8_t> bytes = target.ReadColor();
        std::vector<Pixel> pixels(kSize * kSize);
        std::memcpy(pixels.data(), bytes.data(), bytes.size());
        return pixels;
    }

    static FrameParams View(float distance) {
        OrbitCamera camera;
        camera.pitch = OrbitCamera::kMaxPitch;
        camera.distance = distance;
        FrameParams params;
        params.camera = camera.MakeView(1.0f);
        params.physical.width = kSize;
        params.physical.height = kSize;
        return params;
    }

    /// The pixel showing world point `p` in a `View(distance)` render.
    static std::size_t PixelOf(glm::vec3 p, float distance = 40.0f) {
        const glm::vec4 clip = View(distance).camera.ViewProjection() * glm::vec4(p, 1.0f);
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        const auto col = static_cast<std::size_t>((ndc.x * 0.5f + 0.5f) * kSize);
        const auto row = static_cast<std::size_t>((0.5f - ndc.y * 0.5f) * kSize);
        return row * kSize + col;
    }
};

}  // namespace

TEST_F(MeshPartTest, DrawsALitMeshWithItsAlbedoAndVertexColours) {
    Frame frame(*context_);
    MeshPart& meshes = frame.meshes;
    const std::uint32_t grey = meshes.AddMesh(Square(4.0f, true));
    const std::uint32_t red = meshes.AddMesh(Square(2.0f, false, true));
    MeshItem left;
    left.mesh = grey;
    left.transform = glm::translate(glm::mat4(1.0f), {-6.0f, 0.0f, 0.0f});
    left.tint = glm::vec3(0.8f);
    MeshItem right;
    right.mesh = red;
    right.transform = glm::translate(glm::mat4(1.0f), {6.0f, 0.0f, 0.0f});
    meshes.SetItems({left, right});
    EXPECT_EQ(meshes.MeshCount(), 2u);

    const std::vector<Pixel> pixels = Render(frame);
    const Pixel g = pixels[PixelOf({-6.0f, 0.0f, 0.0f})];
    const Pixel r = pixels[PixelOf({6.0f, 0.0f, 0.0f})];
    EXPECT_GT(g.Luminance(), 300);
    EXPECT_NEAR(g.r, g.b, 2);
    EXPECT_GT(r.r, 200);
    EXPECT_LT(r.g + r.b, r.r / 2) << "a spectrum cannot reproduce the sRGB red primary";
    EXPECT_EQ(pixels[PixelOf({0.0f, 0.0f, 0.0f})].Luminance(), 0) << "background between";
    EXPECT_EQ(meshes.TriangleCount(), 4u);
}

TEST_F(MeshPartTest, InstancesFollowPositionsOrientationsAndScales) {
    Frame frame(*context_);
    MeshPart& meshes = frame.meshes;
    const std::uint32_t square = meshes.AddMesh(Square(1.0f));
    InstanceData data;
    data.positions = {-8, -8, 0, 8, -8, 0, 8, 8, 0, -8, 8, 0};
    data.scales = {1, 1, 1, 2, 2, 1, 1, 1, 1, 3, 1, 1};
    const float s = std::sqrt(0.5f);
    data.orientations = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, s, s, 0, 0, 0, 1};
    const std::uint32_t instancer = meshes.AddInstancer(data);
    MeshItem item;
    item.mesh = square;
    item.instancer = instancer;
    item.instances = {0, 1, 2, 3};
    meshes.SetItems({item});
    std::vector<Pixel> pixels = Render(frame);
    for (const glm::vec3 p :
         {glm::vec3(-8, -8, 0), glm::vec3(8, -8, 0), glm::vec3(8, 8, 0), glm::vec3(-8, 8, 0)}) {
        EXPECT_GT(pixels[PixelOf(p)].Luminance(), 100) << p.x << ", " << p.y;
    }
    EXPECT_GT(pixels[PixelOf({9.6f, -8.0f, 0.0f})].Luminance(), 100) << "scaled by 2";
    EXPECT_EQ(pixels[PixelOf({-8.0f, -9.8f, 0.0f})].Luminance(), 0) << "unit size";
    EXPECT_GT(pixels[PixelOf({-10.5f, 8.0f, 0.0f})].Luminance(), 100) << "stretched in x";
    EXPECT_EQ(pixels[PixelOf({-8.0f, 10.5f, 0.0f})].Luminance(), 0);
    EXPECT_EQ(meshes.TriangleCount(), 8u);

    item.instances = {0};
    meshes.SetItems({item});
    pixels = Render(frame);
    EXPECT_GT(pixels[PixelOf({-8, -8, 0})].Luminance(), 100);
    EXPECT_EQ(pixels[PixelOf({8, 8, 0})].Luminance(), 0) << "only the listed instances";

    data.positions = {0, 0, 0, 8, -8, 0, 8, 8, 0, -8, 8, 0};
    meshes.SetInstances(instancer, data);
    pixels = Render(frame);
    EXPECT_GT(pixels[PixelOf({0, 0, 0})].Luminance(), 100) << "moved instance";
    EXPECT_EQ(pixels[PixelOf({-8, -8, 0})].Luminance(), 0);
}

TEST_F(MeshPartTest, PrototypeTransformAppliesBeforeTheInstance) {
    Frame frame(*context_);
    MeshPart& meshes = frame.meshes;
    MeshItem item;
    item.mesh = meshes.AddMesh(Square(1.0f));
    item.instancer = meshes.AddInstancer({{0, 0, 0}, {}, {}});
    item.instances = {0};
    item.prototype = glm::translate(glm::mat4(1.0f), {5.0f, 0.0f, 0.0f});
    item.transform = glm::translate(glm::mat4(1.0f), {0.0f, 5.0f, 0.0f});
    meshes.SetItems({item});
    const std::vector<Pixel> pixels = Render(frame);
    EXPECT_GT(pixels[PixelOf({5, 5, 0})].Luminance(), 100);
    EXPECT_EQ(pixels[PixelOf({0, 0, 0})].Luminance(), 0);
    const auto bounds = meshes.Bounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_LE(bounds->first.x, 4.0f);
    EXPECT_GE(bounds->second.y, 6.0f);
}

TEST_F(MeshPartTest, MeshesCastShadowsOnTheTerrainAndReceiveThem) {
    Frame frame(*context_);
    frame.terrain.SetHeightfield(TestRaster::Centred(65, 65, 0.5));
    MeshPart& meshes = frame.meshes;
    MeshItem box;
    box.mesh = meshes.AddMesh(Box(3.0f));
    box.transform = glm::translate(glm::mat4(1.0f), {0.0f, 0.0f, 6.0f});
    meshes.SetItems({box});
    const glm::vec3 sun = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    const std::vector<Pixel> lit = Render(frame, 40.0f, sun, false);
    const std::vector<Pixel> shadowed = Render(frame, 40.0f, sun, true);
    const std::size_t underBox = PixelOf({-8.0f, 0.0f, 0.0f});
    EXPECT_GT(lit[underBox].Luminance(), 100);
    EXPECT_LT(shadowed[underBox].Luminance(), lit[underBox].Luminance() / 4)
        << "the box shadows the ground away from the sun";
    const std::size_t clear = PixelOf({12.0f, 10.0f, 0.0f});
    EXPECT_NEAR(shadowed[clear].Luminance(), lit[clear].Luminance(), 6);
    const std::size_t top = PixelOf({0.0f, 0.0f, 9.0f});
    EXPECT_NEAR(shadowed[top].Luminance(), lit[top].Luminance(), 6) << "no self-shadow acne";
}

TEST_F(MeshPartTest, SetPointsMovesGeometryInPlace) {
    Frame frame(*context_);
    MeshPart& meshes = frame.meshes;
    MeshItem item;
    item.mesh = meshes.AddMesh(Square(2.0f));
    meshes.SetItems({item});
    std::vector<Pixel> pixels = Render(frame);
    EXPECT_GT(pixels[PixelOf({0, 0, 0})].Luminance(), 100);
    MeshData moved = Square(2.0f);
    for (std::size_t i = 0; i < moved.points.size(); i += 3) {
        moved.points[i] += 10.0f;
    }
    meshes.SetPoints(item.mesh, moved.points);
    pixels = Render(frame);
    EXPECT_EQ(pixels[PixelOf({0, 0, 0})].Luminance(), 0);
    EXPECT_GT(pixels[PixelOf({10, 0, 0})].Luminance(), 100);
    EXPECT_THROW(meshes.SetPoints(item.mesh, std::vector<float>(3)), std::invalid_argument);
    EXPECT_THROW(meshes.SetPoints(999, moved.points), std::invalid_argument);
}

TEST_F(MeshPartTest, EveryBuiltinBrdfCompilesForMeshes) {
    Frame frame(*context_);
    MeshPart& meshes = frame.meshes;
    const std::uint32_t square = meshes.AddMesh(Square(1.0f));
    std::vector<MeshItem> items;
    float x = -12.0f;
    for (const ernie::BrdfModel& model : frame.renderer.Brdfs().Models()) {
        MeshItem item;
        item.mesh = square;
        item.transform = glm::translate(glm::mat4(1.0f), {x, 0.0f, 0.0f});
        item.material = frame.renderer.AddMaterial({model.id});
        items.push_back(item);
        x += 6.0f;
    }
    meshes.SetItems(items);
    const std::vector<Pixel> pixels = Render(frame, 40.0f, glm::vec3(0.3f, 0.2f, 1.0f));
    for (const MeshItem& item : items) {
        EXPECT_GT(pixels[PixelOf(glm::vec3(item.transform[3]))].Luminance(), 30) << item.material;
    }
}

TEST_F(MeshPartTest, RejectsBadMeshesInstancersAndItems) {
    Frame frame(*context_);
    MeshPart& meshes = frame.meshes;
    MeshData bad = Square();
    bad.indices.push_back(9);
    bad.indices.push_back(0);
    bad.indices.push_back(1);
    EXPECT_THROW(meshes.AddMesh(bad), std::invalid_argument);
    bad = Square();
    bad.points.pop_back();
    EXPECT_THROW(meshes.AddMesh(bad), std::invalid_argument);
    bad = Square();
    bad.normals = {0, 0, 1};
    EXPECT_THROW(meshes.AddMesh(bad), std::invalid_argument);
    EXPECT_THROW(meshes.AddInstancer({{0, 0}, {}, {}}), std::invalid_argument);
    EXPECT_THROW(meshes.AddInstancer({{0, 0, 0}, {0, 0, 1}, {}}), std::invalid_argument);
    EXPECT_THROW(meshes.AddInstancer({{0, 0, 0}, {}, {1, 1}}), std::invalid_argument);

    const std::uint32_t mesh = meshes.AddMesh(Square());
    const std::uint32_t instancer = meshes.AddInstancer({{0, 0, 0}, {}, {}});
    MeshItem item;
    item.mesh = 999;
    EXPECT_THROW(meshes.SetItems({item}), std::invalid_argument);
    item.mesh = mesh;
    item.material = 999;
    EXPECT_THROW(meshes.SetItems({item}), std::invalid_argument);
    item.material = RasterRenderer::kDefaultMaterial;
    item.instancer = 999;
    EXPECT_THROW(meshes.SetItems({item}), std::invalid_argument);
    item.instancer = instancer;
    item.instances = {1};
    EXPECT_THROW(meshes.SetItems({item}), std::invalid_argument);
    EXPECT_THROW(meshes.SetInstances(999, InstanceData{}), std::invalid_argument);

    item.instances = {0};
    meshes.SetItems({item});
    meshes.RemoveInstancer(instancer);
    meshes.RemoveMesh(mesh);
    EXPECT_EQ(meshes.MeshCount(), 0u);
    EXPECT_EQ(meshes.InstancerCount(), 0u);
    EXPECT_FALSE(meshes.Bounds().has_value());
    EXPECT_THROW(meshes.RemoveMesh(mesh), std::invalid_argument);
    EXPECT_THROW(meshes.RemoveInstancer(instancer), std::invalid_argument);
    const std::vector<Pixel> pixels = Render(frame);
    EXPECT_EQ(pixels[PixelOf({0, 0, 0})].Luminance(), 0);
}

TEST_F(MeshPartTest, MeshesShareTheRenderersMaterials) {
    Frame frame(*context_);
    ernie::MaterialSettings red;
    red.albedo = {1.0f, 0.0f, 0.0f};
    const std::uint32_t material = frame.renderer.AddMaterial(red);
    MeshItem item;
    item.mesh = frame.meshes.AddMesh(Square(4.0f, true));
    item.material = material;
    frame.meshes.SetItems({item});
    std::vector<Pixel> pixels = Render(frame);
    Pixel centre = pixels[PixelOf({0, 0, 0})];
    EXPECT_GT(centre.r, 200);
    EXPECT_LT(centre.g + centre.b, centre.r / 2);

    ernie::MaterialSettings blue = red;
    blue.albedo = {0.0f, 0.0f, 1.0f};
    frame.renderer.SetMaterial(material, blue);
    pixels = Render(frame);
    centre = pixels[PixelOf({0, 0, 0})];
    EXPECT_GT(centre.b, 200) << "edits to the material reach every draw using it";

    frame.renderer.RemoveMaterial(material);
    pixels = Render(frame);
    centre = pixels[PixelOf({0, 0, 0})];
    EXPECT_NEAR(centre.r, centre.b, 2) << "falls back to the default material";
    EXPECT_GT(centre.Luminance(), 300);
}

TEST_F(MeshPartTest, DetailTexturesProjectOntoMeshesTriplanar) {
    Frame frame(*context_);
    ernie::Image checker(8, 8);
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            checker.At(x, y) = glm::vec4(glm::vec3(((x / 4 + y / 4) % 2) ? 0.9f : 0.1f), 1.0f);
        }
    }
    ernie::MaterialSettings settings;
    settings.detail.tileSize = 16.0f;
    settings.detail.hexTiling = false;
    const std::uint32_t material = frame.renderer.AddMaterial(settings);
    frame.renderer.Material(material).SetDetailAlbedo(&checker);
    MeshItem item;
    item.mesh = frame.meshes.AddMesh(Square(8.0f, true));
    item.material = material;
    frame.meshes.SetItems({item});
    const std::vector<Pixel> pixels = Render(frame);
    const int a = pixels[PixelOf({2.0f, 2.0f, 0.0f})].Luminance();
    const int b = pixels[PixelOf({-2.0f, 2.0f, 0.0f})].Luminance();
    EXPECT_GT(std::abs(a - b), 100) << "neighbouring checker squares differ";
}

TEST_F(MeshPartTest, CudaInstancesAndPointsRenderLikeHostOnes) {
    std::string why;
    auto cuda = ernie::CudaInterop::Create(*context_, &why);
    if (cuda == nullptr) {
        GTEST_SKIP() << "No CUDA interop: " << why;
    }
    const std::array<float, 9> positions{-8, -8, 0, 8, -8, 0, 0, 8, 0};
    const std::array<float, 9> scales{1, 1, 1, 2, 2, 1, 1, 3, 1};
    const float s = std::sqrt(0.5f);
    const std::array<float, 12> orientations{0, 0, 0, 1, 0, 0, s, s, 0, 0, 0, 1};

    Frame host(*context_);
    MeshItem item;
    item.mesh = host.meshes.AddMesh(Square(1.0f));
    item.instancer = host.meshes.AddInstancer({{positions.begin(), positions.end()},
                                               {orientations.begin(), orientations.end()},
                                               {scales.begin(), scales.end()}});
    item.instances = {0, 1, 2};
    host.meshes.SetItems({item});
    const std::vector<Pixel> expected = Render(host);

    Frame device(*context_);
    device.meshes.SetCudaInterop(cuda.get());
    MeshItem deviceItem = item;
    deviceItem.mesh = device.meshes.AddMesh(Square(1.0f));
    deviceItem.instancer = device.meshes.AddInstancer({{0, 0, 0, 0, 0, 0, 0, 0, 0}, {}, {}});
    ernie::DeviceInstanceData data;
    data.positions = cuda->Upload(Tensor::Copy<float>(positions, {3, 3}));
    data.orientations = cuda->Upload(Tensor::Copy<float>(orientations, {3, 4}));
    data.scales = cuda->Upload(Tensor::Copy<float>(scales, {3, 3}));
    device.meshes.SetInstances(*deviceItem.instancer, data);
    device.meshes.SetItems({deviceItem});
    EXPECT_EQ(Render(device), expected) << "same pixels from CUDA arrays";
    EXPECT_TRUE(device.meshes.Bounds().has_value());

    const std::array<float, 12> moved{-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0};
    MeshItem plain;
    plain.mesh = deviceItem.mesh;
    plain.transform = glm::translate(glm::mat4(1.0f), {12.0f, 12.0f, 0.0f});
    device.meshes.SetItems({deviceItem, plain});
    std::array<float, 12> shifted = moved;
    for (std::size_t i = 0; i < shifted.size(); i += 3) {
        shifted[i] -= 4.0f;
    }
    device.meshes.SetPoints(deviceItem.mesh, cuda->Upload(Tensor::Copy<float>(shifted, {4, 3})));
    const std::vector<Pixel> deformed = Render(device);
    EXPECT_GT(deformed[PixelOf({8.0f, 12.0f, 0.0f})].Luminance(), 100) << "points moved on the GPU";
    EXPECT_EQ(deformed[PixelOf({12.0f, 12.0f, 0.0f})].Luminance(), 0);

    data.positions = cuda->Upload(Tensor::Copy<float>(positions, {9}));
    data.extent = std::pair(glm::vec3(-20.0f), glm::vec3(20.0f));
    device.meshes.SetInstances(*deviceItem.instancer, data);
    const auto bounds = device.meshes.Bounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_LE(bounds->first.x, -20.0f) << "the given extent bounds the instances";

    EXPECT_THROW(host.meshes.SetInstances(*item.instancer, data), std::invalid_argument)
        << "no CUDA interop";
    data.positions = Tensor::Copy<float>(positions, {3, 3});
    EXPECT_THROW(device.meshes.SetInstances(*deviceItem.instancer, data), std::invalid_argument)
        << "a host tensor";
    data.positions = cuda->Upload(Tensor::Copy<float>(positions, {3, 3}));
    data.scales =
        cuda->Upload(Tensor::Copy<float>(std::span<const float>(positions).first(6), {2, 3}));
    EXPECT_THROW(device.meshes.SetInstances(*deviceItem.instancer, data), std::invalid_argument)
        << "scales do not match";
}

TEST_F(MeshPartTest, BoundArraysAreDoubleBufferedAcrossFrames) {
    std::string why;
    auto cuda = ernie::CudaInterop::Create(*context_, &why);
    if (cuda == nullptr) {
        GTEST_SKIP() << "No CUDA interop: " << why;
    }
    Frame frame(*context_);
    frame.meshes.SetCudaInterop(cuda.get());
    MeshItem item;
    item.mesh = frame.meshes.AddMesh(Square(1.0f));
    item.instancer = frame.meshes.AddInstancer({{-8, 0, 0}, {}, {}});
    item.instances = {0};
    frame.meshes.SetItems({item});
    EXPECT_THROW(MeshPart(frame.renderer).MapPoints(item.mesh), std::invalid_argument)
        << "no CUDA interop";

    const auto write = [&](const ernie::BoundSlot& slot, float x) {
        const std::array<float, 3> position{x, 0.0f, 0.0f};
        cuda->CopyToBuffer(cuda->Upload(Tensor::Copy<float>(position, {1, 3})), *slot.buffer);
        cuda->Synchronize();
    };
    const ernie::BoundSlot first =
        frame.meshes.MapInstances(*item.instancer, ernie::InstanceArray::Positions);
    EXPECT_EQ(first.readDone, 0u) << "no frame has read it";
    EXPECT_EQ(frame.meshes.MapInstances(*item.instancer, ernie::InstanceArray::Positions).buffer,
              first.buffer)
        << "mapping again returns the same slot";
    write(first, 8.0f);
    frame.meshes.UnmapInstances(*item.instancer, ernie::InstanceArray::Positions);
    std::vector<Pixel> pixels = Render(frame);
    frame.meshes.MarkRead(11);
    EXPECT_GT(pixels[PixelOf({8, 0, 0})].Luminance(), 100) << "frame 1 reads the written slot";
    EXPECT_EQ(pixels[PixelOf({-8, 0, 0})].Luminance(), 0);

    const ernie::BoundSlot second =
        frame.meshes.MapInstances(*item.instancer, ernie::InstanceArray::Positions);
    EXPECT_NE(second.buffer, first.buffer) << "frame 2 is written while frame 1 reads the other";
    EXPECT_EQ(second.readDone, 0u) << "so it waits for nothing frame 1 does";
    write(second, 0.0f);
    frame.meshes.UnmapInstances(*item.instancer, ernie::InstanceArray::Positions);
    pixels = Render(frame);
    frame.meshes.MarkRead(12);
    EXPECT_GT(pixels[PixelOf({0, 0, 0})].Luminance(), 100) << "frame 2 reads its slot";
    EXPECT_EQ(pixels[PixelOf({8, 0, 0})].Luminance(), 0);

    const ernie::BoundSlot third =
        frame.meshes.MapInstances(*item.instancer, ernie::InstanceArray::Positions);
    EXPECT_EQ(third.buffer, first.buffer);
    EXPECT_EQ(third.readDone, 11u) << "waits for frame 1, the last reader, not frame 2";
    frame.meshes.UnmapInstances(*item.instancer, ernie::InstanceArray::Positions);

    pixels = Render(frame);
    EXPECT_GT(pixels[PixelOf({8, 0, 0})].Luminance(), 100)
        << "an unwritten slot shows what was written two frames before";

    frame.meshes.SetInstances(*item.instancer, InstanceData{{-8, 0, 0}, {}, {}});
    pixels = Render(frame);
    EXPECT_GT(pixels[PixelOf({-8, 0, 0})].Luminance(), 100) << "SetInstances takes over again";

    const ernie::BoundSlot points = frame.meshes.MapPoints(item.mesh);
    EXPECT_EQ(points.count, 4u);
    EXPECT_EQ(points.components, 3u);
}
