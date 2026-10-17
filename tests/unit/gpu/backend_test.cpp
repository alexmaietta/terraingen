#include "terraingen/gpu/backend.h"
#include "terraingen/gpu/vulkan.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>

using terraingen::Backend;

namespace {

/// Restores the default backend and `TERRAINGEN_BACKEND` after each test.
class BackendTest : public ::testing::Test {
  protected:
    void TearDown() override {
        unsetenv("TERRAINGEN_BACKEND");
        terraingen::SetDefaultBackend(Backend::Cpu);
    }
};

}  // namespace

TEST(BackendNamed, ReadsEachNameInAnyCase) {
    EXPECT_EQ(terraingen::BackendNamed("auto"), Backend::Auto);
    EXPECT_EQ(terraingen::BackendNamed("CPU"), Backend::Cpu);
    EXPECT_EQ(terraingen::BackendNamed("Vulkan"), Backend::Vulkan);
    EXPECT_FALSE(terraingen::BackendNamed("cuda").has_value());
    EXPECT_FALSE(terraingen::BackendNamed("").has_value());
    for (const Backend backend : {Backend::Auto, Backend::Cpu, Backend::Vulkan}) {
        EXPECT_EQ(terraingen::BackendNamed(terraingen::BackendName(backend)), backend);
    }
    EXPECT_EQ(terraingen::BackendName(static_cast<Backend>(7)), "unknown");
}

TEST_F(BackendTest, DefaultsFollowTheEnvironmentUntilSetInCode) {
    unsetenv("TERRAINGEN_BACKEND");
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
    setenv("TERRAINGEN_BACKEND", "", 1);
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
    setenv("TERRAINGEN_BACKEND", "auto", 1);
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Auto);
    setenv("TERRAINGEN_BACKEND", "cpu", 1);
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
    setenv("TERRAINGEN_BACKEND", "VULKAN", 1);
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Vulkan);
    setenv("TERRAINGEN_BACKEND", "metal", 1);
    EXPECT_THROW(terraingen::DefaultBackend(), std::invalid_argument);
    terraingen::SetDefaultBackend(Backend::Cpu);
    EXPECT_EQ(terraingen::DefaultBackend(), Backend::Cpu);
}

TEST(Backends, ReportTheCpuAndWhetherVulkanRuns) {
    const auto statuses = terraingen::Backends();
    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_EQ(statuses[0].backend, Backend::Cpu);
    EXPECT_TRUE(statuses[0].available);
    EXPECT_EQ(statuses[1].backend, Backend::Vulkan);
    const terraingen::VulkanCompute* compute = terraingen::VulkanCompute::Get();
    EXPECT_EQ(statuses[1].available, compute != nullptr);
    if (compute != nullptr) {
        EXPECT_EQ(statuses[1].device, compute->DeviceName());
        EXPECT_FALSE(statuses[1].device.empty());
        EXPECT_TRUE(statuses[1].reason.empty());
    } else {
        EXPECT_FALSE(statuses[1].reason.empty());
    }
}

TEST(ResolveBackend, PicksVulkanWhenItRunsAndOtherwiseTheCpu) {
    EXPECT_EQ(terraingen::ResolveBackend(Backend::Cpu), Backend::Cpu);
    if (terraingen::VulkanCompute::Get() != nullptr) {
        EXPECT_EQ(terraingen::ResolveBackend(Backend::Auto), Backend::Vulkan);
        EXPECT_EQ(terraingen::ResolveBackend(Backend::Vulkan), Backend::Vulkan);
    } else {
        EXPECT_EQ(terraingen::ResolveBackend(Backend::Auto), Backend::Cpu);
        EXPECT_THROW(terraingen::ResolveBackend(Backend::Vulkan), std::runtime_error);
    }
}
