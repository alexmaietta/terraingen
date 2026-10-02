#include "ernie/gpu/gpu_buffer.h"
#include "ernie/interop/cuda_interop.h"
#include "support/vulkan_fixture.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <vector>

using ernie::BufferMemory;
using ernie::CudaInterop;
using ernie::DeviceKind;
using ernie::DType;
using ernie::GpuBuffer;
using ernie::Tensor;

namespace {

class CudaInteropTest : public ernie::testing::VulkanTest {
  protected:
    void SetUp() override {
        VulkanTest::SetUp();
        if (context_ == nullptr) {
            return;
        }
        std::string why;
        cuda_ = CudaInterop::Create(*context_, &why);
        if (cuda_ == nullptr) {
            GTEST_SKIP() << "No CUDA interop: " << why;
        }
    }

    void TearDown() override {
        cuda_.reset();
        VulkanTest::TearDown();
    }

    std::unique_ptr<CudaInterop> cuda_;
};

}  // namespace

TEST_F(CudaInteropTest, UploadAndDownloadRoundTrip) {
    const std::array<float, 6> values{1, 2, 3, 4, 5, 6};
    const Tensor device = cuda_->Upload(Tensor::Copy<float>(values, {2, 3}));
    EXPECT_EQ(device.Location().kind, DeviceKind::Cuda);
    EXPECT_EQ(device.Location().id, cuda_->Device());
    EXPECT_EQ(device.Shape(), (std::vector<std::int64_t>{2, 3}));
    const Tensor host = cuda_->Download(device);
    EXPECT_EQ(std::vector<float>(host.As<float>(), host.As<float>() + 6),
              std::vector<float>(values.begin(), values.end()));
    EXPECT_THROW(cuda_->Upload(device), std::invalid_argument);
    EXPECT_THROW(cuda_->Download(host), std::invalid_argument);
}

TEST_F(CudaInteropTest, CudaWritesReachVulkanBuffersWithoutTheHost) {
    GpuBuffer buffer(*context_, 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Shared);
    const std::array<std::uint32_t, 4> values{7, 8, 9, 10};
    cuda_->CopyToBuffer(cuda_->Upload(Tensor::Copy<std::uint32_t>(values, {4})), buffer, 16);
    GpuBuffer readback(*context_, 16, 0, BufferMemory::Readback);
    const ernie::TimelinePoint wait = cuda_->VulkanWait();
    ASSERT_NE(wait.semaphore, VK_NULL_HANDLE);
    context_->Submit(
        [&](VkCommandBuffer commands) {
            const VkBufferCopy region{16, 0, 16};
            vkCmdCopyBuffer(commands, buffer.Handle(), readback.Handle(), 1, &region);
        },
        std::span(&wait, 1));
    std::array<std::uint32_t, 4> read{};
    readback.Read(std::as_writable_bytes(std::span(read)));
    EXPECT_EQ(read, values) << "Vulkan waited on the GPU for the queued CUDA copy";

    EXPECT_THROW(
        cuda_->CopyToBuffer(cuda_->Upload(Tensor::Copy<std::uint32_t>(values, {4})), buffer, 56),
        std::invalid_argument);
    GpuBuffer plain(*context_, 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemory::Device);
    EXPECT_THROW(
        cuda_->CopyToBuffer(cuda_->Upload(Tensor::Copy<std::uint32_t>(values, {4})), plain, 0),
        std::invalid_argument);
    EXPECT_THROW(cuda_->CopyToBuffer(Tensor::Copy<std::uint32_t>(values, {4}), buffer, 0),
                 std::invalid_argument)
        << "a host tensor";
}

TEST_F(CudaInteropTest, VulkanWritesAreVisibleThroughCudaViews) {
    auto buffer = std::make_shared<GpuBuffer>(*context_, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              BufferMemory::Shared);
    const std::array<float, 8> values{0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f};
    buffer->Write(std::as_bytes(std::span(values)));
    const Tensor view = cuda_->View(buffer, DType::Float32, {2, 4});
    EXPECT_EQ(view.Location().kind, DeviceKind::Cuda);
    const Tensor host = cuda_->Download(view);
    EXPECT_EQ(host.As<float>()[7], 7.5f);
    EXPECT_EQ(cuda_->View(buffer, DType::Float32, {2, 4}).Data(), view.Data()) << "imported once";
    EXPECT_THROW(cuda_->View(buffer, DType::Float64, {8}), std::invalid_argument);
}

TEST(CudaInteropAvailability, ExplainsWhyItIsUnavailable) {
    std::unique_ptr<ernie::VulkanContext> context;
    try {
        context = ernie::VulkanContext::Create();
    } catch (const std::runtime_error&) {
        GTEST_SKIP() << "No Vulkan device";
    }
    std::string why;
    const auto cuda = CudaInterop::Create(*context, &why);
    EXPECT_EQ(cuda == nullptr, !why.empty());
}

TEST_F(CudaInteropTest, CudaWaitsOnTheGpuForVulkanWrites) {
    auto buffer = std::make_shared<GpuBuffer>(*context_, 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              BufferMemory::Shared);
    const std::array<std::uint32_t, 16> zeros{};
    buffer->Write(std::as_bytes(std::span(zeros)));
    const Tensor view = cuda_->View(buffer, DType::UInt32, {16});

    // Vulkan fills the buffer only once the host releases `gate`, and signals CUDA after.
    VkSemaphore gate = context_->CreateTimelineSemaphore();
    const ernie::TimelinePoint release{gate, 1};
    const ernie::TimelinePoint signal = cuda_->NextVulkanSignal();
    context_->SubmitAsync(
        [&](VkCommandBuffer commands) {
            vkCmdFillBuffer(commands, buffer->Handle(), 0, VK_WHOLE_SIZE, 42u);
        },
        std::span(&release, 1), std::span(&signal, 1));
    EXPECT_TRUE(context_->HasPendingSubmissions());

    std::future<Tensor> download =
        std::async(std::launch::async, [&] { return cuda_->Download(view); });
    EXPECT_EQ(download.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout)
        << "CUDA must wait for the gated Vulkan fill";
    VkSemaphoreSignalInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    signalInfo.semaphore = gate;
    signalInfo.value = 1;
    vkSignalSemaphore(context_->Device(), &signalInfo);
    const Tensor host = download.get();
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(host.As<std::uint32_t>()[i], 42u);
    }
    context_->WaitForSubmissions();
    EXPECT_FALSE(context_->HasPendingSubmissions());
    vkDestroySemaphore(context_->Device(), gate, nullptr);
}
