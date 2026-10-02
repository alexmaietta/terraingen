#include "ernie/gpu_context.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using ernie::GpuContext;
using ernie::GpuContextOptions;

namespace {

std::unique_ptr<GpuContext> CreateHeadlessOrSkip() {
    try {
        return GpuContext::Create();
    } catch (const std::runtime_error& error) {
        return nullptr;
    }
}

}  // namespace

TEST(GpuContext, CreatesHeadlessDevice) {
    const auto gpu = CreateHeadlessOrSkip();
    if (!gpu) {
        GTEST_SKIP() << "No WebGPU adapter available";
    }
    EXPECT_NE(gpu->Instance(), nullptr);
    EXPECT_NE(gpu->Adapter(), nullptr);
    EXPECT_NE(gpu->Device(), nullptr);
    EXPECT_NE(gpu->Queue(), nullptr);
    EXPECT_EQ(gpu->Surface(), nullptr);
    EXPECT_EQ(gpu->SurfaceFormat(), WGPUTextureFormat_Undefined);
    EXPECT_FALSE(gpu->AdapterDescription().empty());
    EXPECT_THROW(gpu->ConfigureSurface(64, 64), std::logic_error);
}

TEST(GpuContext, RoundTripsABufferThroughTheDevice) {
    const auto gpu = CreateHeadlessOrSkip();
    if (!gpu) {
        GTEST_SKIP() << "No WebGPU adapter available";
    }
    const std::array<std::uint32_t, 4> input{1u, 2u, 0xDEADBEEFu, 42u};
    constexpr std::uint64_t kSize = sizeof(input);

    WGPUBufferDescriptor sourceDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    sourceDescriptor.size = kSize;
    sourceDescriptor.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer source = wgpuDeviceCreateBuffer(gpu->Device(), &sourceDescriptor);

    WGPUBufferDescriptor readbackDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    readbackDescriptor.size = kSize;
    readbackDescriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(gpu->Device(), &readbackDescriptor);

    wgpuQueueWriteBuffer(gpu->Queue(), source, 0, input.data(), kSize);
    WGPUCommandEncoderDescriptor encoderDescriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->Device(), &encoderDescriptor);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 0, kSize);
    WGPUCommandBufferDescriptor commandsDescriptor = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &commandsDescriptor);
    wgpuQueueSubmit(gpu->Queue(), 1, &commands);

    struct MapResult {
        bool done = false;
        bool success = false;
    } map;
    WGPUBufferMapCallbackInfo mapInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mapInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    mapInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
        auto* out = static_cast<MapResult*>(userdata1);
        out->done = true;
        out->success = status == WGPUMapAsyncStatus_Success;
    };
    mapInfo.userdata1 = &map;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, kSize, mapInfo);
    gpu->WaitUntil([&map] { return map.done; });
    ASSERT_TRUE(map.success);

    std::array<std::uint32_t, 4> output{};
    std::memcpy(output.data(), wgpuBufferGetConstMappedRange(readback, 0, kSize), kSize);
    wgpuBufferUnmap(readback);
    EXPECT_EQ(output, input);

    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(encoder);
    wgpuBufferRelease(readback);
    wgpuBufferRelease(source);
}

TEST(GpuContext, ThrowsWhenTheSurfaceFactoryFails) {
    EXPECT_THROW(GpuContext::Create([](WGPUInstance) { return static_cast<WGPUSurface>(nullptr); }),
                 std::runtime_error);
}

TEST(GpuContext, ThrowsWhenTheRequestedBackendIsUnavailable) {
    GpuContextOptions options;
#if defined(__APPLE__)
    options.backend = WGPUBackendType_D3D12;
#else
    options.backend = WGPUBackendType_Metal;
#endif
    EXPECT_THROW(GpuContext::Create({}, options), std::runtime_error);
}

TEST(GpuContext, ReportsUncapturedErrorsOnStderr) {
    const auto gpu = CreateHeadlessOrSkip();
    if (!gpu) {
        GTEST_SKIP() << "No WebGPU adapter available";
    }
    WGPUBufferDescriptor invalid = WGPU_BUFFER_DESCRIPTOR_INIT;
    invalid.size = 16;
    invalid.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite;

    testing::internal::CaptureStderr();
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(gpu->Device(), &invalid);
    bool polled = false;
    gpu->WaitUntil([&polled] { return std::exchange(polled, true); });
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_NE(output.find("WebGPU error"), std::string::npos) << output;
    wgpuBufferRelease(buffer);
}

TEST(GpuContext, StaysQuietWhenTheDeviceIsDestroyed) {
    const auto gpu = CreateHeadlessOrSkip();
    if (!gpu) {
        GTEST_SKIP() << "No WebGPU adapter available";
    }
    testing::internal::CaptureStderr();
    wgpuDeviceDestroy(gpu->Device());
    bool polled = false;
    gpu->WaitUntil([&polled] { return std::exchange(polled, true); });
    EXPECT_EQ(testing::internal::GetCapturedStderr().find("device lost"), std::string::npos);
}
