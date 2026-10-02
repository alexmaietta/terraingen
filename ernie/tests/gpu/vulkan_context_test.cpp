#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/shader_compiler.h"
#include "ernie/gpu/vulkan_context.h"
#include "support/vulkan_fixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

using ernie::BufferMemory;
using ernie::ComputePipeline;
using ernie::GpuBuffer;
using ernie::ShaderCompiler;
using ernie::ShaderModuleSource;
using ernie::VulkanContext;
using ernie::VulkanContextOptions;

namespace {

class VulkanContextTest : public ernie::testing::VulkanTest {};

std::span<const std::byte> Bytes(const std::vector<std::uint32_t>& values) {
    return std::as_bytes(std::span(values));
}

}  // namespace

TEST_F(VulkanContextTest, CreatesADeviceWithItsIdentity) {
    EXPECT_NE(context_->Instance(), VK_NULL_HANDLE);
    EXPECT_NE(context_->Device(), VK_NULL_HANDLE);
    EXPECT_NE(context_->Queue(), VK_NULL_HANDLE);
    EXPECT_NE(context_->Allocator(), VK_NULL_HANDLE);
    EXPECT_FALSE(context_->DeviceName().empty());
    const auto& uuid = context_->DeviceUuid();
    EXPECT_TRUE(std::any_of(uuid.begin(), uuid.end(), [](std::uint8_t b) { return b != 0; }));
    EXPECT_GT(context_->Limits().maxImageDimension2D, 0u);
    RecordProperty("device", context_->DeviceName());
    RecordProperty("rayQuery", context_->SupportsRayQuery() ? "yes" : "no");
    RecordProperty("validation", context_->ValidationEnabled() ? "on" : "off");
}

TEST_F(VulkanContextTest, PrefersTheNamedDevice) {
    VulkanContextOptions options;
    options.preferredDevice = "llvmpipe";
    const auto software = VulkanContext::Create(options);
    if (software->DeviceName().find("llvmpipe") == std::string::npos) {
        GTEST_SKIP() << "No llvmpipe device installed";
    }
    EXPECT_FALSE(software->ValidationEnabled());
}

TEST_F(VulkanContextTest, RejectsMissingDeviceExtensions) {
    VulkanContextOptions options;
    options.deviceExtensions = {"VK_NOT_A_real_extension"};
    EXPECT_THROW(VulkanContext::Create(options), std::runtime_error);
}

TEST_F(VulkanContextTest, BuffersRoundTripThroughEveryMemoryKind) {
    std::vector<std::uint32_t> values(1000);
    std::iota(values.begin(), values.end(), 7u);
    for (const BufferMemory memory :
         {BufferMemory::Device, BufferMemory::Upload, BufferMemory::Readback}) {
        GpuBuffer buffer(*context_, values.size() * 4 + 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         memory);
        EXPECT_NE(buffer.Handle(), VK_NULL_HANDLE);
        EXPECT_EQ(buffer.Size(), values.size() * 4 + 16);
        buffer.Write(Bytes(values), 16);
        std::vector<std::uint32_t> back(values.size());
        buffer.Read(std::as_writable_bytes(std::span(back)), 16);
        EXPECT_EQ(back, values);
        EXPECT_THROW(buffer.Write(Bytes(values), 17), std::out_of_range);
        EXPECT_THROW(buffer.Read(std::as_writable_bytes(std::span(back)), buffer.Size() + 1),
                     std::out_of_range);
        buffer.Write({});
        buffer.Read({});
    }
}

TEST_F(VulkanContextTest, BuffersMoveWithoutDoubleFreeing) {
    GpuBuffer first(*context_, 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Device);
    const VkBuffer handle = first.Handle();
    GpuBuffer second(std::move(first));
    EXPECT_EQ(second.Handle(), handle);
    EXPECT_EQ(first.Handle(), VK_NULL_HANDLE);
    GpuBuffer third(*context_, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Upload);
    third = std::move(second);
    EXPECT_EQ(third.Handle(), handle);
    EXPECT_EQ(third.Size(), 64u);
}

TEST_F(VulkanContextTest, ComputePipelinesRunSlangShaders) {
    const ShaderModuleSource doubler{"doubler", R"(
[[vk::binding(0, 0)]] StructuredBuffer<uint> input;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    output[id.x] = 2 * input[id.x] + 1;
}
)"};
    const ShaderCompiler compiler;
    const std::vector<std::uint32_t> spirv = compiler.CompileSpirv({&doubler, 1}, "main");
    const ComputePipeline pipeline(*context_, spirv, 2);

    std::vector<std::uint32_t> values(256);
    std::iota(values.begin(), values.end(), 0u);
    GpuBuffer input(*context_, values.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    BufferMemory::Device);
    GpuBuffer output(*context_, values.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     BufferMemory::Readback);
    input.Write(Bytes(values));
    const std::array<const GpuBuffer*, 2> buffers = {&input, &output};
    context_->Submit([&](VkCommandBuffer commands) { pipeline.Dispatch(commands, buffers, 4); });
    std::vector<std::uint32_t> result(values.size());
    output.Read(std::as_writable_bytes(std::span(result)));
    for (std::size_t i = 0; i < values.size(); ++i) {
        ASSERT_EQ(result[i], 2 * values[i] + 1) << i;
    }

    const std::array<const GpuBuffer*, 1> tooFew = {&input};
    context_->Submit([&](VkCommandBuffer commands) {
        EXPECT_THROW(pipeline.Dispatch(commands, tooFew, 1), std::invalid_argument);
    });
}

TEST_F(VulkanContextTest, AsyncSubmissionsSignalTimelinesAndRecycle) {
    VkSemaphore timeline = context_->CreateTimelineSemaphore(5);
    ernie::GpuBuffer buffer(*context_, 16, 0, ernie::BufferMemory::Readback);
    const ernie::TimelinePoint wait{timeline, 5};
    const ernie::TimelinePoint signal{timeline, 9};
    for (int i = 0; i < 3; ++i) {
        context_->SubmitAsync(
            [&](VkCommandBuffer commands) {
                vkCmdFillBuffer(commands, buffer.Handle(), 0, VK_WHOLE_SIZE,
                                static_cast<std::uint32_t>(i));
            },
            std::span(&wait, 1),
            i == 2 ? std::span(&signal, 1) : std::span<ernie::TimelinePoint>());
    }
    context_->WaitForSubmissions();
    EXPECT_FALSE(context_->HasPendingSubmissions());
    std::uint64_t value = 0;
    vkGetSemaphoreCounterValue(context_->Device(), timeline, &value);
    EXPECT_EQ(value, 9u);
    std::array<std::uint32_t, 4> read{};
    buffer.Read(std::as_writable_bytes(std::span(read)));
    EXPECT_EQ(read[3], 2u) << "submissions ran in order";
    context_->Submit([](VkCommandBuffer) {}, std::span(&signal, 1));
    vkDestroySemaphore(context_->Device(), timeline, nullptr);
}
