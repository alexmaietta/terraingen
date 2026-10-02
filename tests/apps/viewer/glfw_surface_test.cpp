#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/swapchain.h"
#include "ernie/gpu/vulkan_context.h"
#include "glfw_surface.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

using ernie::Swapchain;
using ernie::VulkanContext;
using ernie::VulkanContextOptions;
using terraingen::viewer::CreateGlfwSurface;
using terraingen::viewer::GlfwInstanceExtensions;

class GlfwSurface : public testing::Test {
  protected:
    void SetUp() override {
        try {
            glfwInitVulkanLoader(ernie::LoadVulkanLoader());
        } catch (const std::runtime_error&) {
            GTEST_SKIP() << "Vulkan is not available";
        }
        if (glfwInit() == GLFW_FALSE) {
            GTEST_SKIP() << "No display available";
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window_ = glfwCreateWindow(64, 48, "terraingen test", nullptr, nullptr);
        if (window_ == nullptr) {
            GTEST_SKIP() << "Could not create a window";
        }
    }

    void TearDown() override {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        glfwTerminate();
    }

    GLFWwindow* window_ = nullptr;
};

TEST_F(GlfwSurface, PresentsFramesThroughASwapchain) {
    VulkanContextOptions options;
    options.validation = true;
    options.instanceExtensions = GlfwInstanceExtensions();
    options.deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const auto gpu = VulkanContext::Create(options);
    Swapchain swapchain(*gpu, CreateGlfwSurface(gpu->Instance(), window_), 64, 48);
    EXPECT_NE(swapchain.Format(), VK_FORMAT_UNDEFINED);
    EXPECT_GE(swapchain.ImageCount(), swapchain.MinImageCount());
    EXPECT_GT(swapchain.Extent().width, 0u);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore acquired = VK_NULL_HANDLE;
    ASSERT_EQ(vkCreateSemaphore(gpu->Device(), &semaphoreInfo, nullptr, &acquired), VK_SUCCESS);
    for (int round = 0; round < 2; ++round) {
        const auto frame = swapchain.Acquire(acquired);
        ASSERT_TRUE(frame.has_value());
        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = frame->acquired;
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = frame->rendered;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        gpu->Submit([&](VkCommandBuffer commands) {
            ernie::TransitionImage(commands, frame->image, VK_IMAGE_ASPECT_COLOR_BIT,
                                   VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        });
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        ASSERT_EQ(vkQueueSubmit2(gpu->Queue(), 1, &submit, VK_NULL_HANDLE), VK_SUCCESS);
        swapchain.Present(*frame);
        vkQueueWaitIdle(gpu->Queue());
        swapchain.Resize(64, 48);
    }
    vkDestroySemaphore(gpu->Device(), acquired, nullptr);
    for (const std::string& message : gpu->TakeValidationMessages()) {
        ADD_FAILURE() << "Vulkan validation: " << message;
    }
}
