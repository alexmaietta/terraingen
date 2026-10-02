#pragma once

#include "ernie/gpu/vulkan_context.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ernie::testing {

/// A Vulkan context with validation enabled where the layer is installed; skips the test when no
/// Vulkan device is available, and fails it on any validation message.
class VulkanTest : public ::testing::Test {
  protected:
    void SetUp() override {
        try {
            VulkanContextOptions options;
            options.validation = true;
            context_ = VulkanContext::Create(options);
        } catch (const std::runtime_error& error) {
            GTEST_SKIP() << "No Vulkan device: " << error.what();
        }
    }

    void TearDown() override {
        if (context_ != nullptr) {
            for (const std::string& message : context_->TakeValidationMessages()) {
                ADD_FAILURE() << "Vulkan validation: " << message;
            }
        }
    }

    std::unique_ptr<VulkanContext> context_;
};

}  // namespace ernie::testing
