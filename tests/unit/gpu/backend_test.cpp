// SPDX-License-Identifier: BSD-3-Clause

#include "gpu/cuda.h"
#include "gpu/kernel.h"
#include "gpu/metal.h"
#include "gpu/vulkan.h"
#include "terraingen/gpu/backend.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

using terraingen::Backend;

namespace {

/// Sets the environment variable `name` to `value`, or clears it for null.
void SetEnvironment(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

/// Restores the default backend and `TERRAINGEN_BACKEND` after each test.
class BackendTest : public ::testing::Test {
  protected:
    void TearDown() override {
        SetEnvironment("TERRAINGEN_BACKEND", nullptr);
        terraingen::SetDefaultBackend(Backend::Cpu);
    }
};

}  // namespace

TEST(BackendNamed, ReadsEachNameInAnyCase) {
    EXPECT_EQ(terraingen::BackendNamed("auto"), Backend::Auto);
    EXPECT_EQ(terraingen::BackendNamed("CPU"), Backend::Cpu);
    EXPECT_EQ(terraingen::BackendNamed("Vulkan"), Backend::Vulkan);
    EXPECT_EQ(terraingen::BackendNamed("gpu"), Backend::Gpu);
    EXPECT_EQ(terraingen::BackendNamed("CUDA"), Backend::Cuda);
    EXPECT_EQ(terraingen::BackendNamed("metal"), Backend::Metal);
    EXPECT_FALSE(terraingen::BackendNamed("opencl").has_value());
    EXPECT_FALSE(terraingen::BackendNamed("").has_value());
    for (const Backend backend : {Backend::Auto, Backend::Cpu, Backend::Vulkan, Backend::Gpu,
                                  Backend::Cuda, Backend::Metal}) {
        EXPECT_EQ(terraingen::BackendNamed(terraingen::BackendName(backend)), backend);
    }
    EXPECT_EQ(terraingen::BackendName(static_cast<Backend>(7)), "unknown");
}

TEST(BackendNameList, ListsEveryNameBackendNamedReads) {
    EXPECT_EQ(terraingen::BackendNameList(), "auto, cpu, gpu, vulkan, cuda or metal");
    for (const std::string_view name : terraingen::kBackendNames) {
        ASSERT_TRUE(terraingen::BackendNamed(name).has_value()) << name;
        EXPECT_EQ(terraingen::BackendName(*terraingen::BackendNamed(name)), name);
    }
}

TEST(GpuDevice, IsTheSharedDeviceOfEachGpuBackend) {
    EXPECT_EQ(terraingen::GpuDevice(Backend::Cpu), nullptr);
    EXPECT_EQ(terraingen::GpuDevice(Backend::Auto), nullptr);
    EXPECT_EQ(terraingen::GpuDevice(Backend::Gpu), nullptr);
    EXPECT_EQ(terraingen::GpuDevice(Backend::Vulkan), terraingen::VulkanCompute::Get());
    EXPECT_EQ(terraingen::GpuDevice(Backend::Cuda), terraingen::CudaCompute::Get());
    EXPECT_EQ(terraingen::GpuDevice(Backend::Metal), terraingen::MetalCompute::Get());
}

TEST(RunKernel, LeavesTheGridAloneWithoutAGpuForm) {
    terraingen::Raster<float> grid(4, 3, 1.0f);
    for (const Backend backend :
         {Backend::Cpu, Backend::Auto, Backend::Vulkan, Backend::Cuda, Backend::Metal}) {
        EXPECT_FALSE(terraingen::RunKernel(backend, {}, {}, grid, {0.0, 0.0}));
    }
    EXPECT_EQ(grid.Values()[0], 1.0f);
}

TEST_F(BackendTest, DefaultsFollowTheEnvironmentUntilSetInCode) {
    SetEnvironment("TERRAINGEN_BACKEND", nullptr);
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
    SetEnvironment("TERRAINGEN_BACKEND", "");
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
    SetEnvironment("TERRAINGEN_BACKEND", "auto");
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Auto);
    SetEnvironment("TERRAINGEN_BACKEND", "cpu");
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
    SetEnvironment("TERRAINGEN_BACKEND", "VULKAN");
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Vulkan);
    SetEnvironment("TERRAINGEN_BACKEND", "gpu");
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Gpu);
    SetEnvironment("TERRAINGEN_BACKEND", "opencl");
    EXPECT_THROW(terraingen::DefaultBackend(), std::invalid_argument);
    terraingen::SetDefaultBackend(Backend::Cpu);
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
}

TEST(Backends, ReportTheCpuThenEachGpuBackendWithItsDeviceOrWhyNot) {
    const auto statuses = terraingen::Backends();
    ASSERT_EQ(statuses.size(), 4u);
    EXPECT_EQ(statuses[0].backend, Backend::Cpu);
    EXPECT_TRUE(statuses[0].available);
    EXPECT_EQ(statuses[1].backend, Backend::Vulkan);
    EXPECT_EQ(statuses[2].backend, Backend::Cuda);
    EXPECT_EQ(statuses[3].backend, Backend::Metal);
    const terraingen::VulkanCompute* vulkan = terraingen::VulkanCompute::Get();
    EXPECT_EQ(statuses[1].available, vulkan != nullptr);
    if (vulkan != nullptr) {
        EXPECT_EQ(statuses[1].device, vulkan->DeviceName());
    }
    if (const terraingen::CudaCompute* cuda = terraingen::CudaCompute::Get();
        statuses[2].available) {
        ASSERT_NE(cuda, nullptr);
        EXPECT_EQ(statuses[2].device, cuda->DeviceName());
    }
#if !defined(__APPLE__)
    EXPECT_FALSE(statuses[3].available);
    EXPECT_EQ(statuses[3].reason, "Metal runs only on Apple platforms");
#endif
    for (const terraingen::BackendStatus& status : statuses) {
        EXPECT_EQ(status.device.empty(), !status.available)
            << terraingen::BackendName(status.backend);
        EXPECT_EQ(status.reason.empty(), status.available)
            << terraingen::BackendName(status.backend);
    }
}

TEST(ResolveBackend, PicksTheBestGpuBackendAndOtherwiseTheCpu) {
    EXPECT_EQ(terraingen::ResolveBackend(Backend::Cpu), Backend::Cpu);
    std::optional<Backend> best;
    for (const Backend backend : {Backend::Metal, Backend::Cuda, Backend::Vulkan}) {
        const auto statuses = terraingen::Backends();
        const auto status = std::find_if(statuses.begin(), statuses.end(),
                                         [&](const auto& s) { return s.backend == backend; });
        if (status->available) {
            EXPECT_EQ(terraingen::ResolveBackend(backend), backend);
            best = best.value_or(backend);
        } else {
            EXPECT_THROW(terraingen::ResolveBackend(backend), std::runtime_error);
        }
    }
    if (best) {
        EXPECT_EQ(terraingen::ResolveBackend(Backend::Gpu), *best);
        EXPECT_EQ(terraingen::ResolveBackend(Backend::Auto), *best);
    } else {
        EXPECT_THROW(terraingen::ResolveBackend(Backend::Gpu), std::runtime_error);
        EXPECT_EQ(terraingen::ResolveBackend(Backend::Auto), Backend::Cpu);
    }
}

TEST(DescribeBackend, NamesTheVulkanDeviceWhenThereIsOne) {
    EXPECT_EQ(terraingen::DescribeBackend(Backend::Cpu), "cpu");
    EXPECT_EQ(terraingen::DescribeBackend(Backend::Auto), "auto");
    EXPECT_EQ(terraingen::DescribeBackend(Backend::Gpu), "gpu");
    const terraingen::VulkanCompute* compute = terraingen::VulkanCompute::Get();
    EXPECT_EQ(terraingen::DescribeBackend(Backend::Vulkan),
              compute != nullptr ? "vulkan (" + compute->DeviceName() + ")" : "vulkan");
}

TEST(CudaCompute, OpensOnceAndExplainsWhyItIsUnavailable) {
    EXPECT_EQ(terraingen::CudaCompute::Get(), terraingen::CudaCompute::Get());
    EXPECT_EQ(terraingen::CudaCompute::Reason().empty(), terraingen::CudaCompute::Get() != nullptr);
}

TEST(MetalCompute, OpensOnlyOnApplePlatforms) {
#if defined(__APPLE__)
    EXPECT_EQ(terraingen::MetalCompute::Reason().empty(),
              terraingen::MetalCompute::Get() != nullptr);
#else
    EXPECT_EQ(terraingen::MetalCompute::Get(), nullptr);
    EXPECT_EQ(terraingen::MetalCompute::Reason(), "Metal runs only on Apple platforms");
#endif
}

TEST(MakeKernelBuffer, ViewsTheValuesBytesAndCountsTheElements) {
    const std::vector<float> values{1.0f, 2.0f, 3.0f};
    const terraingen::KernelBuffer whole = terraingen::MakeKernelBuffer(values);
    EXPECT_EQ(whole.bytes.data(), reinterpret_cast<const std::byte*>(values.data()));
    EXPECT_EQ(whole.bytes.size(), 3 * sizeof(float));
    EXPECT_EQ(whole.count, 3u);
    EXPECT_EQ(whole.perCell, 0u);
    EXPECT_EQ(terraingen::MakeKernelBuffer(values, 3).perCell, 3u);
}
