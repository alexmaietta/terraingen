#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/shader_compiler.h"
#include "support/vulkan_fixture.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

using ernie::BeginRendering;
using ernie::BufferMemory;
using ernie::DescriptorBinding;
using ernie::DescriptorResource;
using ernie::GpuBuffer;
using ernie::GpuImage;
using ernie::GraphicsPipeline;
using ernie::GraphicsPipelineDesc;
using ernie::OffscreenTarget;
using ernie::PipelineLayout;
using ernie::Sampler;
using ernie::SamplerDesc;
using ernie::ShaderCompiler;
using ernie::ShaderModuleSource;

namespace {

// Two quads without vertex buffers: a red one at depth 0.25 over the left half of the target and
// the upper half (y > 0), and a green one at depth 0.75 covering everything. The shade comes from a
// texture fetched with the sampler, scaled by a push constant.
constexpr char kQuads[] = R"(
struct Constants {
    float scale;
}

[[vk::push_constant]] ConstantBuffer<Constants> constants;
[[vk::binding(0, 0)]] Texture2D<float4> shade;
[[vk::binding(1, 0)]] SamplerState linear;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertex : SV_VertexID) {
    let corners = float2[6](float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 0), float2(1, 1),
                            float2(0, 1));
    let corner = corners[vertex % 6];
    VertexOutput out;
    if (vertex < 6) {
        out.position = float4(lerp(float2(-1, 0), float2(0, 1), corner), 0.25, 1.0);
        out.color = float3(1, 0, 0);
    } else {
        out.position = float4(lerp(float2(-1, -1), float2(1, 1), corner), 0.75, 1.0);
        out.color = float3(0, 1, 0);
    }
    return out;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput in) : SV_Target {
    let s = shade.SampleLevel(linear, float2(0.5, 0.5), 0).r * constants.scale;
    return float4(in.color * s, 1.0);
}
)";

class RasterPipelineTest : public ernie::testing::VulkanTest {};

}  // namespace

TEST_F(RasterPipelineTest, DrawsWithDepthTestingAndAnUpwardNdcY) {
    const ShaderCompiler compiler;
    const std::vector<ShaderModuleSource> modules = {{"quads", kQuads}};
    const std::vector<std::uint32_t> vertex = compiler.CompileSpirv(modules, "vertexMain");
    const std::vector<std::uint32_t> fragment = compiler.CompileSpirv(modules, "fragmentMain");

    const std::array<DescriptorBinding, 2> bindings = {
        DescriptorBinding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT},
        DescriptorBinding{1, VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT}};
    const PipelineLayout layout(*context_, bindings, sizeof(float));
    OffscreenTarget target(*context_, 32, 32);
    GraphicsPipelineDesc desc;
    desc.vertexSpirv = vertex;
    desc.fragmentSpirv = fragment;
    desc.vertexEntry = "main";
    desc.fragmentEntry = "main";
    desc.colorFormat = target.ColorFormat();
    desc.depthFormat = target.DepthFormat();
    const GraphicsPipeline pipeline(*context_, layout, desc);

    GpuImage shade(*context_, {2, 2, 1, 2, VK_FORMAT_R8G8B8A8_UNORM});
    const std::vector<std::uint8_t> mip0(2 * 2 * 4, 128);
    const std::vector<std::uint8_t> mip1(4, 128);
    const std::array<std::span<const std::byte>, 2> mips = {std::as_bytes(std::span(mip0)),
                                                            std::as_bytes(std::span(mip1))};
    shade.Upload(mips);
    const Sampler sampler(*context_, SamplerDesc{});

    context_->Submit([&](VkCommandBuffer commands) {
        BeginRendering(commands, target.Prepare(commands, {{0.0f, 0.0f, 1.0f, 1.0f}}));
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Handle());
        const std::array<DescriptorResource, 2> resources = {DescriptorResource::Of(shade.View()),
                                                             DescriptorResource::Of(sampler)};
        layout.Push(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, resources);
        const float scale = 2.0f;
        layout.PushConstants(commands, &scale, sizeof(scale));
        vkCmdDraw(commands, 12, 1, 0, 0);
        vkCmdEndRendering(commands);
    });
    const std::vector<std::uint8_t> pixels = target.ReadColor();
    auto at = [&](std::size_t x, std::size_t y) { return &pixels[(y * 32 + x) * 4]; };
    EXPECT_EQ(at(4, 4)[1], 0);
    EXPECT_GT(at(4, 28)[1], 250) << "lower-left shows the far green quad";
    EXPECT_GT(at(28, 4)[1], 250) << "right half shows the far green quad";
    EXPECT_EQ(at(28, 4)[2], 0) << "the background is covered everywhere";

    EXPECT_THROW(layout.Push(VK_NULL_HANDLE, VK_PIPELINE_BIND_POINT_GRAPHICS, {}),
                 std::invalid_argument);
    EXPECT_THROW(shade.LayerView(1), std::out_of_range);
    EXPECT_EQ(shade.LayerView(0), shade.View());
}

TEST_F(RasterPipelineTest, ImagesCheckUploadsAndTrackLayers) {
    GpuImage image(*context_, {4, 2, 1, 1, VK_FORMAT_R32_SFLOAT});
    const std::vector<float> wrong(3, 1.0f);
    const std::array<std::span<const std::byte>, 1> tooSmall = {std::as_bytes(std::span(wrong))};
    EXPECT_THROW(image.Upload(tooSmall), std::invalid_argument);
    EXPECT_THROW(image.Upload({}), std::invalid_argument);
    EXPECT_EQ(image.Layout(), VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_GE(image.Bytes(), 4u * 2u * 4u);

    GpuImage array(*context_,
                   {8, 8, 3, 1, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT});
    EXPECT_NE(array.LayerView(2), array.View());
    EXPECT_NE(array.LayerView(0), array.LayerView(1));
    EXPECT_EQ(ernie::AspectOf(VK_FORMAT_D32_SFLOAT), VK_IMAGE_ASPECT_DEPTH_BIT);
    EXPECT_EQ(ernie::AspectOf(VK_FORMAT_R8G8B8A8_SRGB), VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_THROW(GpuImage::TexelBytes(VK_FORMAT_BC1_RGB_UNORM_BLOCK), std::invalid_argument);

    GpuImage moved(std::move(array));
    EXPECT_EQ(array.Handle(), VK_NULL_HANDLE);
    image = std::move(moved);
    EXPECT_EQ(image.Desc().layers, 3u);

    const Sampler comparison(*context_,
                             SamplerDesc{VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                         VK_SAMPLER_ADDRESS_MODE_REPEAT, 16.0f, true});
    EXPECT_NE(comparison.Handle(), VK_NULL_HANDLE);
}
