#include "ernie/gpu/swapchain.h"

#include <algorithm>
#include <stdexcept>

namespace ernie {

Swapchain::Swapchain(const VulkanContext& context, VkSurfaceKHR surface, std::uint32_t width,
                     std::uint32_t height)
    : context_(context), surface_(surface) {
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(context.PhysicalDevice(), context.QueueFamily(), surface,
                                         &supported);
    if (supported == VK_FALSE) {
        vkDestroySurfaceKHR(context.Instance(), surface, nullptr);
        throw std::runtime_error(context.DeviceName() + " cannot present to this window");
    }
    Create(width, height);
}

Swapchain::~Swapchain() {
    vkDeviceWaitIdle(context_.Device());
    Destroy();
    vkDestroySwapchainKHR(context_.Device(), swapchain_, nullptr);
    vkDestroySurfaceKHR(context_.Instance(), surface_, nullptr);
}

void Swapchain::Resize(std::uint32_t width, std::uint32_t height) {
    vkDeviceWaitIdle(context_.Device());
    Destroy();
    Create(width, height);
}

void Swapchain::Destroy() {
    for (VkImageView view : views_) {
        vkDestroyImageView(context_.Device(), view, nullptr);
    }
    for (VkSemaphore semaphore : rendered_) {
        vkDestroySemaphore(context_.Device(), semaphore, nullptr);
    }
    views_.clear();
    rendered_.clear();
    images_.clear();
}

void Swapchain::Create(std::uint32_t width, std::uint32_t height) {
    VkPhysicalDevice physical = context_.PhysicalDevice();
    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &formatCount, formats.data());
    if (formats.empty()) {
        throw std::runtime_error("The window surface offers no formats");
    }
    VkSurfaceFormatKHR chosen = formats.front();
    for (const VkSurfaceFormatKHR& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_UNORM ||
             format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = format;
            break;
        }
    }
    format_ = chosen.format;
    if (capabilities.currentExtent.width != UINT32_MAX) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_ = {
            std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(height, capabilities.minImageExtent.height,
                       capabilities.maxImageExtent.height)};
    }
    minImageCount_ = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) {
        minImageCount_ = std::min(minImageCount_, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = minImageCount_;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    VkSwapchainKHR previous = swapchain_;
    info.oldSwapchain = previous;
    CheckVk(vkCreateSwapchainKHR(context_.Device(), &info, nullptr, &swapchain_),
            "vkCreateSwapchainKHR");
    if (previous != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context_.Device(), previous, nullptr);
    }

    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(context_.Device(), swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(context_.Device(), swapchain_, &count, images_.data());
    for (VkImage image : images_) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView imageView = VK_NULL_HANDLE;
        CheckVk(vkCreateImageView(context_.Device(), &view, nullptr, &imageView),
                "vkCreateImageView");
        views_.push_back(imageView);
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        CheckVk(vkCreateSemaphore(context_.Device(), &semaphoreInfo, nullptr, &semaphore),
                "vkCreateSemaphore");
        rendered_.push_back(semaphore);
    }
}

std::optional<Swapchain::Frame> Swapchain::Acquire(VkSemaphore acquired) {
    std::uint32_t index = 0;
    const VkResult result = vkAcquireNextImageKHR(context_.Device(), swapchain_, UINT64_MAX,
                                                  acquired, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return std::nullopt;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
        CheckVk(result, "vkAcquireNextImageKHR");
    }
    return Frame{index, images_[index], views_[index], acquired, rendered_[index]};
}

bool Swapchain::Present(const Frame& frame) {
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &frame.rendered;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_;
    info.pImageIndices = &frame.index;
    const VkResult result = vkQueuePresentKHR(context_.Queue(), &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    CheckVk(result, "vkQueuePresentKHR");
    return true;
}

}  // namespace ernie
