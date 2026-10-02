#include "ernie/camera/orbit_camera.h"
#include "ernie/core/image.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/material/brdf.h"
#include "ernie/raster/terrain_renderer.h"
#include "support/test_raster.h"
#include "support/vulkan_fixture.h"

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

using ernie::BrdfModel;
using ernie::Image;
using ernie::OffscreenTarget;
using ernie::OrbitCamera;
using ernie::RasterView;
using ernie::TerrainColorMode;
using ernie::TerrainDrawParams;
using ernie::TerrainRenderer;
using ernie::testing::RollingTerrain;
using ernie::testing::TestRaster;

namespace {

constexpr std::uint32_t kSize = 64;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;

struct Pixel {
    std::uint8_t r, g, b, a;

    int Luminance() const {
        return r + g + b;
    }
};

class TerrainRendererTest : public ernie::testing::VulkanTest {
  protected:
    std::vector<Pixel> Render(TerrainRenderer& renderer, const TerrainDrawParams& params,
                              bool update = true) {
        if (update) {
            renderer.Update(params);
        }
        OffscreenTarget target(*context_, kSize, kSize, kColorFormat);
        context_->Submit([&](VkCommandBuffer commands) {
            renderer.Record(commands, target.Prepare(commands, {{0.0f, 0.0f, 0.0f, 1.0f}}));
        });
        const std::vector<std::uint8_t> bytes = target.ReadColor();
        std::vector<Pixel> pixels(kSize * kSize);
        std::memcpy(pixels.data(), bytes.data(), bytes.size());
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
};

TestRaster Grid(std::size_t size, float (*height)(double x, double y)) {
    return ernie::testing::Sampled(size, size, 1.0, height);
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

TestRaster Plane(float slopeX) {
    return ernie::testing::Sampled(
        33, 33, 1.0, [slopeX](double x, double) { return static_cast<double>(slopeX) * x; });
}

}  // namespace

TEST_F(TerrainRendererTest, BuildsPipelineAndUploadsWithoutValidationErrors) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(RollingTerrain(65, 33, 2.0));

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
    TerrainRenderer renderer(*context_, kColorFormat);
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
    TerrainRenderer renderer(*context_, kColorFormat);
    EXPECT_EQ(Render(renderer, TerrainDrawParams{})[0].Luminance(), 0);

    renderer.SetHeightfield(RollingTerrain(33, 33, 4.0));
    const std::vector<Pixel> pixels = Render(renderer, FramedView(renderer));
    int covered = 0;
    for (const Pixel& p : pixels) {
        covered += p.Luminance() > 0 ? 1 : 0;
    }
    EXPECT_GT(pixels[kSize / 2 * kSize + kSize / 2].Luminance(), 0);
    EXPECT_GT(covered, static_cast<int>(kSize * kSize / 4));
}

TEST_F(TerrainRendererTest, SlopesFacingTheSunAreBrighter) {
    TerrainRenderer renderer(*context_, kColorFormat);
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
    TerrainRenderer renderer(*context_, kColorFormat);
    EXPECT_THROW(renderer.SetHeightfield(TestRaster(1, 5)), std::invalid_argument);
    const TestRaster small(4, 4);
    RasterView view = small;
    view.data = nullptr;
    EXPECT_THROW(renderer.SetHeightfield(view), std::invalid_argument);
    view = small;
    view.rowStride = 3;
    EXPECT_THROW(renderer.SetHeightfield(view), std::invalid_argument);
    for (const double cell : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()}) {
        view = small;
        view.cellSize = cell;
        EXPECT_THROW(renderer.SetHeightfield(view), std::invalid_argument);
    }
    view = small;
    view.originX = std::numeric_limits<double>::infinity();
    EXPECT_THROW(renderer.SetHeightfield(view), std::invalid_argument);

    const TestRaster huge(std::size_t{1} << 20, 2);
    EXPECT_THROW(renderer.SetHeightfield(huge), std::invalid_argument);
    EXPECT_FALSE(renderer.HasHeightfield());
    EXPECT_EQ(renderer.TriangleCount(), 0u);
}

TEST_F(TerrainRendererTest, HonoursTheRowStrideOfAView) {
    const TestRaster packed = Plane(0.5f);
    constexpr std::size_t kStride = 40;
    std::vector<float> padded(packed.height * kStride, std::numeric_limits<float>::quiet_NaN());
    for (std::size_t row = 0; row < packed.height; ++row) {
        for (std::size_t col = 0; col < packed.width; ++col) {
            padded[row * kStride + col] = packed(col, row);
        }
    }
    RasterView strided = packed;
    strided.data = padded.data();
    strided.rowStride = kStride;

    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(packed);
    const TerrainDrawParams params = FramedView(renderer);
    const std::vector<Pixel> expected = Render(renderer, params);
    renderer.SetHeightfield(strided);
    EXPECT_EQ(renderer.Bounds(1.0f),
              (std::pair{glm::vec3(-16.0f, -16.0f, -8.0f), glm::vec3(16.0f, 16.0f, 8.0f)}))
        << "padding outside the row is never read";
    const std::vector<Pixel> actual = Render(renderer, params);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        ASSERT_EQ(actual[i].Luminance(), expected[i].Luminance()) << "pixel " << i;
    }
}

TEST_F(TerrainRendererTest, FlatGroundUnderALowSunHasNoShadowAcne) {
    TerrainRenderer renderer(*context_, kColorFormat);
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
    TerrainRenderer renderer(*context_, kColorFormat);
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
    TerrainRenderer renderer(*context_, kColorFormat);
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

TEST_F(TerrainRendererTest, LodImageMatchesFullResolution) {
    TerrainRenderer renderer(*context_, kColorFormat);
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
    TerrainRenderer renderer(*context_, kColorFormat);
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
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(RollingTerrain(513, 513, 1.0));
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
    TerrainRenderer renderer(*context_, kColorFormat);
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
    TerrainRenderer renderer(*context_, kColorFormat);
    const std::uint64_t before = renderer.GpuBytes();
    renderer.SetHeightfield(Grid(1025, [](double x, double y) {
        return static_cast<float>(std::sin(0.05 * x) * std::cos(0.07 * y));
    }));
    TerrainDrawParams params = FramedView(renderer);
    params.lod.enabled = false;
    const std::vector<Pixel> pixels = Render(renderer, params);
    EXPECT_EQ(renderer.PatchCount(), 32u * 32u);
    EXPECT_GT(renderer.GpuBytes(), before + 1025u * 1025u * 8u);
    EXPECT_GT(pixels[kSize / 2 * kSize + kSize / 2].Luminance(), 0);
}

namespace {

int CentreLuminance(const std::vector<Pixel>& pixels) {
    return pixels[kSize / 2 * kSize + kSize / 2].Luminance();
}

/// Mean and standard deviation of luminance over the central half of the image.
std::pair<double, double> CentreStatistics(const std::vector<Pixel>& pixels) {
    double sum = 0.0;
    double squares = 0.0;
    int count = 0;
    for (std::size_t row = kSize / 4; row < 3 * kSize / 4; ++row) {
        for (std::size_t col = kSize / 4; col < 3 * kSize / 4; ++col) {
            const double l = pixels[row * kSize + col].Luminance();
            sum += l;
            squares += l * l;
            ++count;
        }
    }
    const double mean = sum / count;
    return {mean, std::sqrt(std::max(0.0, squares / count - mean * mean))};
}

Image TiltedNormals(glm::vec3 normal) {
    const glm::vec3 n = glm::normalize(normal);
    return Image(16, 16, glm::vec4(0.5f * n + 0.5f, 1.0f));
}

TerrainDrawParams LitFromEast(const TerrainRenderer& renderer) {
    OrbitCamera camera;
    camera.pitch = OrbitCamera::kMaxPitch;
    const auto [low, high] = renderer.Bounds(1.0f);
    camera.Frame(low, high);
    TerrainDrawParams params;
    params.camera = camera.MakeView(1.0f);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.shadows.enabled = false;
    params.sunDirection = glm::normalize(glm::vec3(1.0f, 0.0f, 0.5f));
    return params;
}

}  // namespace

TEST_F(TerrainRendererTest, EveryBuiltinBrdfRendersWithoutValidationErrors) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(RollingTerrain(65, 65, 1.0));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.sunDirection = glm::normalize(glm::vec3(0.3f, -0.2f, 1.0f));
    params.surface.albedo = glm::vec3(0.3f);
    params.surface.exposure = 3.0f;
    for (const BrdfModel& model : renderer.Brdfs().Models()) {
        params.surface.brdf = model.id;
        EXPECT_GT(CentreLuminance(Render(renderer, params)), 30) << model.id;
    }
    params.surface.brdf = "missing";
    EXPECT_THROW(renderer.Update(params), std::invalid_argument);
}

TEST_F(TerrainRendererTest, AddBrdfCompilesCustomModelsAndReportsErrors) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    const int lambert = CentreLuminance(Render(renderer, params));

    BrdfModel half;
    half.id = "half";
    half.name = "Half";
    half.typeName = "Half";
    half.slang = "import brdf;\n"
                 "public struct Half : IBrdf {\n"
                 "    public float3 radianceFactor(BrdfInput s, BrdfParams p) {\n"
                 "        return 0.5 * s.albedo * s.mu0 * p.get(0);\n"
                 "    }\n"
                 "}\n";
    half.params = {{"gain", "Gain", "", 1.0f, 0.0f, 2.0f, false, ""}};
    renderer.AddBrdf(half);
    ASSERT_NE(renderer.Brdfs().Find("half"), nullptr);
    params.surface.brdf = "half";
    EXPECT_NEAR(CentreLuminance(Render(renderer, params)), lambert / 2, 3);
    params.surface.brdfParams = {2.0f};
    EXPECT_NEAR(CentreLuminance(Render(renderer, params)), lambert, 3);

    BrdfModel broken;
    broken.id = "broken";
    broken.name = "Broken";
    broken.typeName = "Broken";
    broken.slang =
        "import brdf;\npublic struct Broken : IBrdf {\n"
        "    public float3 radianceFactor(BrdfInput s, BrdfParams p) { return s.nope; }\n"
        "}\n";
    try {
        renderer.AddBrdf(broken);
        ADD_FAILURE() << "an invalid Slang module was accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("broken failed to compile"), std::string::npos);
    }
    EXPECT_EQ(renderer.Brdfs().Find("broken"), nullptr);
    EXPECT_THROW(renderer.AddBrdf(half), std::invalid_argument);
    EXPECT_EQ(renderer.Brdfs().Models().size(), 6u);
}

TEST_F(TerrainRendererTest, AlbedoAndExposureScaleTheShading) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.sunDirection = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
    const int full = CentreLuminance(Render(renderer, params));
    params.surface.albedo = glm::vec3(0.5f);
    EXPECT_NEAR(CentreLuminance(Render(renderer, params)), full / 2, 3);
    params.surface.exposure = 2.0f;
    EXPECT_NEAR(CentreLuminance(Render(renderer, params)), full, 3);
    params.surface.albedo = glm::vec3(1.0f, 0.0f, 0.0f);
    params.surface.exposure = 1.0f;
    const Pixel red = Render(renderer, params)[kSize / 2 * kSize + kSize / 2];
    EXPECT_GT(red.r, 200);
    EXPECT_EQ(red.g, 0);
}

TEST_F(TerrainRendererTest, AlbedoMapVariesTheSurfaceAndFallsBackAtGaps) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    TestRaster albedo(8, 8, -16.5, 16.5, 33.0 / 8.0);
    for (std::size_t row = 0; row < 8; ++row) {
        for (std::size_t col = 0; col < 8; ++col) {
            albedo(col, row) = col < 4 ? 0.25f : 1.0f;
        }
    }
    albedo(6, 0) = std::numeric_limits<float>::quiet_NaN();
    renderer.Material().SetAlbedoMap(albedo);
    EXPECT_TRUE(renderer.Material().HasAlbedoMap());

    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.sunDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    params.surface.albedo = glm::vec3(0.0f);
    const std::vector<Pixel> mapped = Render(renderer, params);
    const auto [first, last] = CoveredSpan(mapped, kSize / 2);
    ASSERT_LT(first, last);
    const int west = mapped[kSize / 2 * kSize + first + (last - first) / 5].Luminance();
    const int east = mapped[kSize / 2 * kSize + last - (last - first) / 5].Luminance();
    EXPECT_NEAR(west * 4, east, 12);
    const std::size_t gap = (first + (last - first) * 13 / 16) + 2 * kSize;
    EXPECT_LT(mapped[gap + kSize].Luminance(), east) << "NaN cell falls back to the constant";

    params.surface.useAlbedoMap = false;
    params.surface.albedo = glm::vec3(1.0f);
    const std::vector<Pixel> constant = Render(renderer, params);
    EXPECT_NEAR(constant[kSize / 2 * kSize + first + (last - first) / 5].Luminance(), east, 3);

    renderer.Material().ClearAlbedoMap();
    EXPECT_FALSE(renderer.Material().HasAlbedoMap());
    EXPECT_THROW(renderer.Material().SetAlbedoMap(TestRaster()), std::invalid_argument);
    RasterView flipped = albedo;
    flipped.cellSize = -1.0;
    EXPECT_THROW(renderer.Material().SetAlbedoMap(flipped), std::invalid_argument);
    const TestRaster huge(std::size_t{1} << 20, 1);
    EXPECT_THROW(renderer.Material().SetAlbedoMap(huge), std::invalid_argument);
}

TEST_F(TerrainRendererTest, DetailAlbedoAddsVariationButKeepsTheMean) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.sunDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    params.surface.albedo = glm::vec3(0.5f);
    const auto [plainMean, plainSpread] = CentreStatistics(Render(renderer, params));
    EXPECT_LT(plainSpread, 1.0);

    const Image grey(8, 8, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
    renderer.Material().SetDetailTextures(&grey, nullptr);
    EXPECT_TRUE(renderer.Material().HasDetailAlbedo());
    EXPECT_NEAR(renderer.Material().DetailAlbedoMean().g, 0.2f, 2e-3f);
    EXPECT_NEAR(CentreStatistics(Render(renderer, params)).first, plainMean, 2.0)
        << "a uniform texture divided by its mean changes nothing";

    Image checker(64, 64);
    for (std::uint32_t y = 0; y < 64; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) {
            const float v = ((x / 8 + y / 8) % 2 == 0) ? 0.1f : 0.3f;
            checker.At(x, y) = glm::vec4(v, v, v, 1.0f);
        }
    }
    const std::uint64_t before = renderer.GpuBytes();
    renderer.Material().SetDetailTextures(&checker, nullptr);
    EXPECT_GT(renderer.GpuBytes(), before);
    params.surface.detail.tileSize = 16.0f;
    params.surface.detail.hexTiling = false;
    const auto [checkerMean, checkerSpread] = CentreStatistics(Render(renderer, params));
    EXPECT_GT(checkerSpread, 20.0);
    EXPECT_NEAR(checkerMean, plainMean, 0.1 * plainMean);
    params.surface.detail.hexTiling = true;
    EXPECT_GT(CentreStatistics(Render(renderer, params)).second, 10.0);
    params.surface.detail.albedoStrength = 0.0f;
    EXPECT_LT(CentreStatistics(Render(renderer, params)).second, 1.0);

    renderer.Material().SetDetailTextures(nullptr, nullptr);
    EXPECT_FALSE(renderer.Material().HasDetailAlbedo());
    const Image empty;
    EXPECT_THROW(renderer.Material().SetDetailTextures(&empty, nullptr), std::invalid_argument);
    const Image huge(std::uint32_t{1} << 20, 1);
    EXPECT_THROW(renderer.Material().SetDetailTextures(nullptr, &huge), std::invalid_argument);
}

TEST_F(TerrainRendererTest, DetailNormalsTiltTheShadingAndFollowTheirConvention) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    TerrainDrawParams params = LitFromEast(renderer);
    params.surface.detail.hexTiling = false;
    const double flat = CentreStatistics(Render(renderer, params)).first;

    const Image east = TiltedNormals({0.5f, 0.0f, 1.0f});
    renderer.Material().SetDetailTextures(nullptr, &east);
    EXPECT_TRUE(renderer.Material().HasDetailNormal());
    const auto [plain, plainSpread] = CentreStatistics(Render(renderer, params));
    EXPECT_GT(plain, flat + 40.0) << "a normal tilted towards the sun brightens the ground";
    EXPECT_LT(plainSpread, 2.0);

    // Hex tiles without rotation only offset the texture, which leaves a uniform tilt uniform;
    // with rotation each tile's tilt turns with it (see HexTilingGpuTest for the consistency).
    params.surface.detail.hexTiling = true;
    params.surface.detail.hexRotation = 0.0f;
    const auto [offsetOnly, offsetSpread] = CentreStatistics(Render(renderer, params));
    EXPECT_NEAR(offsetOnly, plain, 2.0);
    EXPECT_LT(offsetSpread, 2.0);
    params.surface.detail.hexRotation = 1.0f;
    EXPECT_GT(CentreStatistics(Render(renderer, params)).second, 20.0);

    params.surface.detail.normalStrength = 0.0f;
    EXPECT_NEAR(CentreStatistics(Render(renderer, params)).first, flat, 2.0);

    const Image north = TiltedNormals({0.0f, 0.5f, 1.0f});
    renderer.Material().SetDetailTextures(nullptr, &north);
    params.surface.detail.normalStrength = 1.0f;
    params.surface.detail.hexTiling = false;
    params.sunDirection = glm::normalize(glm::vec3(0.0f, 1.0f, 0.5f));
    const double openGl = CentreStatistics(Render(renderer, params)).first;
    params.surface.detail.directXNormals = true;
    const double directX = CentreStatistics(Render(renderer, params)).first;
    EXPECT_GT(openGl, directX + 60.0) << "green up the image points north in OpenGL maps";
}

TEST_F(TerrainRendererTest, DrawingBeforeTheFirstUpdateDrawsNothing) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    for (const Pixel& p : Render(renderer, FramedView(renderer), false)) {
        ASSERT_EQ(p.Luminance(), 0);
    }
}

TEST_F(TerrainRendererTest, FlatGroundUnderAZenithSunIsNotSelfShadowed) {
    TerrainRenderer renderer(*context_, kColorFormat);
    renderer.SetHeightfield(Plane(0.0f));
    TerrainDrawParams params = FramedView(renderer);
    params.colorMode = TerrainColorMode::Hillshade;
    params.ambient = 0.0f;
    params.sunDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    params.shadows.enabled = false;
    const int unshadowed = CentreLuminance(Render(renderer, params));
    EXPECT_GT(unshadowed, 700);
    params.shadows.enabled = true;
    EXPECT_NEAR(CentreLuminance(Render(renderer, params)), unshadowed, 3);
}
