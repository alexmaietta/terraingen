#include "render/depth_buffer.h"
#include "render/gpu_context.h"
#include "render/orbit_camera.h"
#include "render/terrain_renderer.h"
#include "terraingen/models/synthetic_terrain.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using terraingen::GenerateSyntheticTerrain;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::render::DepthBuffer;
using terraingen::render::GpuContext;
using terraingen::render::OrbitCamera;
using terraingen::render::TerrainColorMode;
using terraingen::render::TerrainDrawParams;
using terraingen::render::TerrainRenderer;

namespace {

constexpr std::uint32_t kSize = 64;
constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;

struct Pixel {
    std::uint8_t r, g, b, a;

    int Luminance() const {
        return r + g + b;
    }
};

class TerrainRendererTest : public testing::Test {
  protected:
    void SetUp() override {
        try {
            gpu_ = GpuContext::Create();
        } catch (const std::runtime_error&) {
            GTEST_SKIP() << "No WebGPU adapter available";
        }
    }

    std::vector<Pixel> Render(TerrainRenderer& renderer, const TerrainDrawParams& params) {
        WGPUTextureDescriptor colorDescriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
        colorDescriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        colorDescriptor.dimension = WGPUTextureDimension_2D;
        colorDescriptor.size = {kSize, kSize, 1};
        colorDescriptor.format = kColorFormat;
        WGPUTexture color = wgpuDeviceCreateTexture(gpu_->Device(), &colorDescriptor);
        WGPUTextureView colorView = wgpuTextureCreateView(color, nullptr);
        DepthBuffer depth(gpu_->Device());
        depth.Resize(kSize, kSize);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = colorView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};
        WGPURenderPassDepthStencilAttachment depthAttachment =
            WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depthAttachment.view = depth.View();
        depthAttachment.depthLoadOp = WGPULoadOp_Clear;
        depthAttachment.depthStoreOp = WGPUStoreOp_Discard;
        depthAttachment.depthClearValue = 1.0f;
        WGPURenderPassDescriptor passDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDescriptor.colorAttachmentCount = 1;
        passDescriptor.colorAttachments = &colorAttachment;
        passDescriptor.depthStencilAttachment = &depthAttachment;

        constexpr std::uint32_t kBytesPerRow = kSize * 4;
        WGPUBufferDescriptor readbackDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDescriptor.size = kBytesPerRow * kSize;
        readbackDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(gpu_->Device(), &readbackDescriptor);

        renderer.Update(params);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu_->Device(), nullptr);
        renderer.EncodeShadows(encoder);
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        renderer.Draw(pass);
        wgpuRenderPassEncoderEnd(pass);
        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = color;
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = readback;
        destination.layout.bytesPerRow = kBytesPerRow;
        destination.layout.rowsPerImage = kSize;
        const WGPUExtent3D extent{kSize, kSize, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(gpu_->Queue(), 1, &commands);

        bool mapped = false;
        WGPUBufferMapCallbackInfo mapInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        mapInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        mapInfo.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* userdata1, void*) {
            *static_cast<bool*>(userdata1) = true;
        };
        mapInfo.userdata1 = &mapped;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, readbackDescriptor.size, mapInfo);
        gpu_->WaitUntil([&mapped] { return mapped; });

        std::vector<Pixel> pixels(kSize * kSize);
        std::memcpy(pixels.data(),
                    wgpuBufferGetConstMappedRange(readback, 0, readbackDescriptor.size),
                    readbackDescriptor.size);
        wgpuBufferUnmap(readback);

        wgpuCommandBufferRelease(commands);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuBufferRelease(readback);
        wgpuTextureViewRelease(colorView);
        wgpuTextureRelease(color);
        return pixels;
    }

    static TerrainDrawParams FramedView(const TerrainRenderer& renderer) {
        OrbitCamera camera;
        camera.pitch = OrbitCamera::kMaxPitch;
        const auto [low, high] = renderer.Bounds(1.0f);
        camera.Frame(low, high);
        TerrainDrawParams params;
        params.camera = camera.MakeView(1.0f);
        return params;
    }

    std::unique_ptr<GpuContext> gpu_;
};

Raster<float> Grid(std::size_t size, float (*height)(double x, double y)) {
    Raster<float> raster(size, size);
    const double half = 0.5 * static_cast<double>(size);
    raster.SetTransform(GeoTransform::NorthUp(-half, half, 1.0));
    for (std::size_t row = 0; row < size; ++row) {
        for (std::size_t col = 0; col < size; ++col) {
            const auto centre = raster.CellCenter(col, row);
            raster(col, row) = height(centre.x, centre.y);
        }
    }
    return raster;
}

std::pair<std::size_t, std::size_t> CoveredSpan(const std::vector<Pixel>& pixels, std::size_t row) {
    std::size_t first = kSize;
    std::size_t last = 0;
    for (std::size_t col = 0; col < kSize; ++col) {
        if (pixels[row * kSize + col].Luminance() > 0) {
            first = std::min(first, col);
            last = std::max(last, col);
        }
    }
    return {first, last};
}

Raster<float> Plane(float slopeX) {
    Raster<float> raster(33, 33);
    raster.SetTransform(GeoTransform::NorthUp(-16.5, 16.5, 1.0));
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            raster(col, row) = slopeX * static_cast<float>(raster.CellCenter(col, row).x);
        }
    }
    return raster;
}

}  // namespace

TEST_F(TerrainRendererTest, BuildsPipelineAndUploadsWithoutValidationErrors) {
    testing::internal::CaptureStderr();
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(
        GenerateSyntheticTerrain(65, 33, 2.0, terraingen::SyntheticTerrainParams{}));
    bool polled = false;
    gpu_->WaitUntil([&polled] { return std::exchange(polled, true); });
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "");

    EXPECT_TRUE(renderer.HasHeightfield());
    EXPECT_EQ(renderer.Width(), 65u);
    EXPECT_EQ(renderer.Height(), 33u);
    EXPECT_EQ(renderer.TriangleCount(), 0u);
    renderer.Update(FramedView(renderer));
    EXPECT_EQ(renderer.TriangleCount(), 2u * 64u * 32u);
    EXPECT_EQ(renderer.PatchCount(), 2u);
    EXPECT_GE(renderer.GpuBytes(), 65u * 33u * sizeof(float));
}

TEST_F(TerrainRendererTest, BoundsSpanCellCentresAndHeights) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(Plane(0.5f));
    const auto [low, high] = renderer.Bounds(2.0f);
    EXPECT_FLOAT_EQ(low.x, -16.0f);
    EXPECT_FLOAT_EQ(high.x, 16.0f);
    EXPECT_FLOAT_EQ(low.y, -16.0f);
    EXPECT_FLOAT_EQ(high.y, 16.0f);
    EXPECT_FLOAT_EQ(low.z, -16.0f);
    EXPECT_FLOAT_EQ(high.z, 16.0f);
}

TEST_F(TerrainRendererTest, RendersTerrainOverTheBackground) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    EXPECT_EQ(Render(renderer, TerrainDrawParams{})[0].Luminance(), 0);

    renderer.SetHeightfield(
        GenerateSyntheticTerrain(33, 33, 4.0, terraingen::SyntheticTerrainParams{}));
    const std::vector<Pixel> pixels = Render(renderer, FramedView(renderer));
    int covered = 0;
    for (const Pixel& p : pixels) {
        covered += p.Luminance() > 0 ? 1 : 0;
    }
    EXPECT_GT(pixels[kSize / 2 * kSize + kSize / 2].Luminance(), 0);
    EXPECT_GT(covered, static_cast<int>(kSize * kSize / 4));
}

TEST_F(TerrainRendererTest, SlopesFacingTheSunAreBrighter) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(Plane(0.5f));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;

    params.sunDirection = glm::normalize(glm::vec3(-1.0f, 0.0f, 1.0f));
    const Pixel facing = Render(renderer, params)[kSize / 2 * kSize + kSize / 2];
    params.sunDirection = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    const Pixel away = Render(renderer, params)[kSize / 2 * kSize + kSize / 2];
    EXPECT_GT(facing.Luminance(), away.Luminance() + 100);
}

TEST_F(TerrainRendererTest, RejectsUnsupportedHeightfields) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    EXPECT_THROW(renderer.SetHeightfield(Raster<float>(1, 5)), std::invalid_argument);

    Raster<float> rectangular(4, 4);
    rectangular.SetTransform(GeoTransform::FromGdal({0.0, 1.0, 0.0, 0.0, 0.0, -2.0}));
    EXPECT_THROW(renderer.SetHeightfield(rectangular), std::invalid_argument);

    Raster<float> rotated(4, 4);
    rotated.SetTransform(GeoTransform::FromGdal({0.0, 1.0, 0.2, 0.0, 0.0, -1.0}));
    EXPECT_THROW(renderer.SetHeightfield(rotated), std::invalid_argument);

    Raster<float> huge(std::size_t{1} << 20, 2);
    huge.SetTransform(GeoTransform::NorthUp(0.0, 0.0, 1.0));
    EXPECT_THROW(renderer.SetHeightfield(huge), std::invalid_argument);
    EXPECT_FALSE(renderer.HasHeightfield());
    EXPECT_EQ(renderer.TriangleCount(), 0u);
}

TEST_F(TerrainRendererTest, FlatGroundUnderALowSunHasNoShadowAcne) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(Grid(65, [](double, double) { return 0.0f; }));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.sunDirection = glm::normalize(glm::vec3(0.8f, 0.3f, 0.35f));
    const std::vector<Pixel> pixels = Render(renderer, params);

    int lowest = 1000;
    int highest = 0;
    int covered = 0;
    for (const Pixel& p : pixels) {
        if (p.Luminance() == 0) {
            continue;
        }
        ++covered;
        lowest = std::min(lowest, p.Luminance());
        highest = std::max(highest, p.Luminance());
    }
    EXPECT_GT(covered, static_cast<int>(kSize * kSize / 4));
    EXPECT_LE(highest - lowest, 6) << "speckled self-shadowing on flat ground";
}

TEST_F(TerrainRendererTest, RidgeShadowsTheGroundAwayFromTheSun) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(
        Grid(65, [](double x, double) { return std::fabs(x) < 2.0 ? 20.0f : 0.0f; }));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.sunDirection = glm::normalize(glm::vec3(1.0f, 0.0f, 0.36f));

    params.shadows.enabled = false;
    const std::vector<Pixel> unshadowed = Render(renderer, params);
    params.shadows.enabled = true;
    const std::vector<Pixel> shadowed = Render(renderer, params);

    const std::size_t row = kSize / 2;
    const auto [first, last] = CoveredSpan(unshadowed, row);
    ASSERT_LT(first, last);
    const std::size_t west = row * kSize + first + (last - first) / 5;
    const std::size_t east = row * kSize + last - (last - first) / 5;
    EXPECT_GT(unshadowed[west].Luminance(), 100);
    EXPECT_LT(shadowed[west].Luminance(), unshadowed[west].Luminance() / 4);
    EXPECT_GT(shadowed[east].Luminance(), unshadowed[east].Luminance() * 9 / 10);
}

TEST_F(TerrainRendererTest, FitsTheRequestedCascadesOnlyWhenTheSunIsUp) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    TerrainDrawParams params;
    renderer.Update(params);
    EXPECT_TRUE(renderer.Cascades().empty());

    renderer.SetHeightfield(Plane(0.1f));
    params = FramedView(renderer);
    params.shadows.cascades = 2;
    params.shadows.resolution = 1024;
    renderer.Update(params);
    EXPECT_EQ(renderer.Cascades().size(), 2u);
    const std::uint64_t bytes1024 = renderer.GpuBytes();

    params.shadows.resolution = 2048;
    renderer.Update(params);
    EXPECT_GT(renderer.GpuBytes(), bytes1024);

    params.sunDirection = glm::vec3(0.0f, 0.6f, -0.8f);
    renderer.Update(params);
    EXPECT_TRUE(renderer.Cascades().empty());

    params.sunDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    params.shadows.enabled = false;
    renderer.Update(params);
    EXPECT_TRUE(renderer.Cascades().empty());
}

TEST_F(TerrainRendererTest, DepthBufferRecreatesOnlyWhenTheSizeChanges) {
    DepthBuffer depth(gpu_->Device());
    EXPECT_EQ(depth.View(), nullptr);
    depth.Resize(32, 16);
    const WGPUTextureView first = depth.View();
    EXPECT_NE(first, nullptr);
    EXPECT_EQ(depth.Width(), 32u);
    EXPECT_EQ(depth.Height(), 16u);
    EXPECT_EQ(depth.Format(), WGPUTextureFormat_Depth32Float);
    depth.Resize(32, 16);
    EXPECT_EQ(depth.View(), first);
    depth.Resize(8, 8);
    EXPECT_EQ(depth.Width(), 8u);
}

TEST_F(TerrainRendererTest, LodImageMatchesFullResolution) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(Grid(257, [](double x, double y) {
        return static_cast<float>(12.0 * std::sin(x / 15.0) * std::cos(y / 21.0) + 0.05 * x);
    }));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    // Shadow PCF noise depends on exact caster depths; LOD shadows are checked separately.
    params.shadows.enabled = false;

    params.lod.enabled = false;
    const std::vector<Pixel> full = Render(renderer, params);
    EXPECT_EQ(renderer.TriangleCount(), 2u * 256u * 256u);
    EXPECT_FLOAT_EQ(renderer.LodFirstRange(), 0.0f);

    params.lod.enabled = true;
    const std::vector<Pixel> lod = Render(renderer, params);
    EXPECT_LT(renderer.TriangleCount(), 2u * 256u * 256u / 4u);
    EXPECT_GE(renderer.LodFirstRange(),
              renderer.Lod().MinFirstRange(params.exaggeration, params.lod.morphStart));

    int differing = 0;
    for (std::size_t i = 0; i < full.size(); ++i) {
        differing += std::abs(full[i].Luminance() - lod[i].Luminance()) > 30 ? 1 : 0;
    }
    EXPECT_LT(differing, static_cast<int>(full.size() / 50));
}

TEST_F(TerrainRendererTest, LodRidgeCastsTheSameShadowAsFullResolution) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(
        Grid(257, [](double x, double) { return std::fabs(x) < 8.0 ? 40.0f : 0.0f; }));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.sunDirection = glm::normalize(glm::vec3(1.0f, 0.0f, 0.36f));

    params.lod.enabled = false;
    const std::vector<Pixel> full = Render(renderer, params);
    params.lod.enabled = true;
    const std::vector<Pixel> lod = Render(renderer, params);
    ASSERT_GT(renderer.Lod().Levels(), 2u);
    ASSERT_GT(renderer.Cascades().size(), 0u);

    const std::size_t row = kSize / 2;
    const auto [first, last] = CoveredSpan(full, row);
    ASSERT_LT(first, last);
    const std::size_t west = row * kSize + first + (last - first) / 5;
    const std::size_t east = row * kSize + last - (last - first) / 5;
    EXPECT_LT(lod[west].Luminance(), lod[east].Luminance() / 4);
    EXPECT_NEAR(lod[west].Luminance(), full[west].Luminance(), 20);
    EXPECT_NEAR(lod[east].Luminance(), full[east].Luminance(), 20);
}

TEST_F(TerrainRendererTest, FreezeKeepsTheSelectionWhileTheCameraMoves) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(
        GenerateSyntheticTerrain(513, 513, 1.0, terraingen::SyntheticTerrainParams{}));
    OrbitCamera camera;
    camera.distance = 60.0f;
    camera.pitch = 0.4f;
    TerrainDrawParams params;
    params.camera = camera.MakeView(1.0f);
    params.lod.freeze = true;
    renderer.Update(params);
    const std::size_t frozen = renderer.PatchCount();
    const std::uint64_t triangles = renderer.TriangleCount();

    camera.target = {150.0f, -120.0f, 0.0f};
    camera.distance = 600.0f;
    params.camera = camera.MakeView(1.0f);
    renderer.Update(params);
    EXPECT_EQ(renderer.PatchCount(), frozen);
    EXPECT_EQ(renderer.TriangleCount(), triangles);

    params.lod.freeze = false;
    renderer.Update(params);
    EXPECT_NE(renderer.PatchCount(), frozen);
}

TEST_F(TerrainRendererTest, ShowLevelsTintsTheTerrain) {
    TerrainRenderer renderer(*gpu_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    const Pixel grey = Render(renderer, params)[kSize / 2 * kSize + kSize / 2];
    EXPECT_EQ(grey.r, grey.b);
    params.lod.showLevels = true;
    const Pixel tinted = Render(renderer, params)[kSize / 2 * kSize + kSize / 2];
    EXPECT_NE(tinted.r, tinted.b);
}

TEST_F(TerrainRendererTest, GrowsThePatchBufferForLargeSelections) {
    testing::internal::CaptureStderr();
    TerrainRenderer renderer(*gpu_, kColorFormat);
    const std::uint64_t before = renderer.GpuBytes();
    renderer.SetHeightfield(Grid(1025, [](double x, double y) {
        return static_cast<float>(std::sin(0.05 * x) * std::cos(0.07 * y));
    }));
    TerrainDrawParams params = FramedView(renderer);
    params.lod.enabled = false;
    const std::vector<Pixel> pixels = Render(renderer, params);
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
    EXPECT_EQ(renderer.PatchCount(), 32u * 32u);
    EXPECT_GT(renderer.GpuBytes(), before + 1025u * 1025u * 8u);
    EXPECT_GT(pixels[kSize / 2 * kSize + kSize / 2].Luminance(), 0);
}
