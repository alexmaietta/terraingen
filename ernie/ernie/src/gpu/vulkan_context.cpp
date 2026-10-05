#include "ernie/gpu/vulkan_context.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace ernie {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool HasLayer(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

std::vector<VkExtensionProperties> InstanceExtensions() {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return extensions;
}

std::vector<VkExtensionProperties> DeviceExtensions(VkPhysicalDevice device) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    return extensions;
}

bool Has(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

void LoadVulkan() {
    if (volkGetInstanceVersion() != 0) {
        return;
    }
    if (volkInitialize() == VK_SUCCESS) {
        return;
    }
#if defined(__APPLE__)
    // Without a Vulkan loader installed, load MoltenVK directly.
    for (const char* path : {"libMoltenVK.dylib", ERNIE_MOLTENVK_PATH}) {
        if (void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
            if (auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                    dlsym(library, "vkGetInstanceProcAddr"))) {
                volkInitializeCustom(getProc);
                return;
            }
        }
    }
#endif
    throw std::runtime_error("Vulkan is not available: no Vulkan loader or driver was found");
}

VKAPI_ATTR VkBool32 VKAPI_CALL OnDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* user) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        const std::string message = data->pMessage != nullptr ? data->pMessage : "";
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            std::fprintf(stderr, "Vulkan validation: %s\n", message.c_str());
        }
        static_cast<std::function<void(std::string)>*>(user)->operator()(message);
    }
    return VK_FALSE;
}

std::string ResultName(VkResult result) {
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    default:
        return "VkResult " + std::to_string(static_cast<int>(result));
    }
}

int DeviceRank(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return 1;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 2;
    default:
        return 3;
    }
}

}  // namespace

PFN_vkGetInstanceProcAddr LoadVulkanLoader() {
    LoadVulkan();
    return vkGetInstanceProcAddr;
}

void CheckVk(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: " + ResultName(result));
    }
}

std::unique_ptr<VulkanContext> VulkanContext::Create(const VulkanContextOptions& options) {
    LoadVulkan();
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
        throw std::runtime_error("Ernie needs Vulkan 1.3; the installed loader is older");
    }
    std::unique_ptr<VulkanContext> context(new VulkanContext());
    VulkanContext& c = *context;

    const std::vector<VkExtensionProperties> available = InstanceExtensions();
    std::vector<const char*> extensions;
    for (const std::string& extension : options.instanceExtensions) {
        extensions.push_back(extension.c_str());
    }
    VkInstanceCreateFlags flags = 0;
    if (Has(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    std::vector<const char*> layers;
    c.validation_ = options.validation && HasLayer(kValidationLayer) &&
                    Has(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (c.validation_) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Ernie";
    app.pEngineName = "Ernie";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.flags = flags;
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    CheckVk(vkCreateInstance(&instanceInfo, nullptr, &c.instance_), "vkCreateInstance");
    // Device functions resolve to the loader's dispatching entry points rather than one device's
    // driver, so several contexts (and devices) can be alive at once.
    volkLoadInstance(c.instance_);

    if (c.validation_) {
        c.sink_ = std::make_unique<std::function<void(std::string)>>(
            [&c](std::string message) { c.Report(std::move(message)); });
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = OnDebugMessage;
        messengerInfo.pUserData = c.sink_.get();
        const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(c.instance_, "vkCreateDebugUtilsMessengerEXT"));
        c.destroyMessenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(c.instance_, "vkDestroyDebugUtilsMessengerEXT"));
        CheckVk(createMessenger(c.instance_, &messengerInfo, nullptr, &c.messenger_),
                "vkCreateDebugUtilsMessengerEXT");
    }

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(c.instance_, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(c.instance_, &deviceCount, devices.data());

    struct Candidate {
        VkPhysicalDevice device;
        std::uint32_t family;
        int rank;
    };
    std::vector<Candidate> candidates;
    std::string rejected;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        const std::vector<VkExtensionProperties> deviceExtensions = DeviceExtensions(device);
        VkPhysicalDeviceVulkan13Features features13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features features12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        features12.pNext = &features13;
        VkPhysicalDeviceVulkan11Features features11{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        features11.pNext = &features12;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features11;
        vkGetPhysicalDeviceFeatures2(device, &features);

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
        std::uint32_t family = familyCount;
        for (std::uint32_t f = 0; f < familyCount; ++f) {
            if ((families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                family = f;
                break;
            }
        }
        bool extensionsPresent = Has(deviceExtensions, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        for (const std::string& extension : options.deviceExtensions) {
            extensionsPresent = extensionsPresent && Has(deviceExtensions, extension.c_str());
        }
        const bool suitable = properties.apiVersion >= VK_API_VERSION_1_3 && family < familyCount &&
                              features13.dynamicRendering && features13.synchronization2 &&
                              features13.shaderDemoteToHelperInvocation &&
                              features12.timelineSemaphore && features11.shaderDrawParameters &&
                              extensionsPresent;
        if (!suitable) {
            rejected += std::string(" ") + properties.deviceName + ";";
            continue;
        }
        int rank = DeviceRank(properties.deviceType);
        if (!options.preferredDevice.empty() &&
            std::string(properties.deviceName).find(options.preferredDevice) != std::string::npos) {
            rank = -1;
        }
        candidates.push_back({device, family, rank});
    }
    if (candidates.empty()) {
        throw std::runtime_error("No Vulkan 1.3 device with dynamic rendering, synchronization2, "
                                 "demote to helper invocation, timeline semaphores, shader draw "
                                 "parameters and push "
                                 "descriptors; rejected:" +
                                 rejected);
    }
    const Candidate best =
        *std::min_element(candidates.begin(), candidates.end(),
                          [](const Candidate& a, const Candidate& b) { return a.rank < b.rank; });
    c.physicalDevice_ = best.device;
    c.queueFamily_ = best.family;

    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &ids;
    vkGetPhysicalDeviceProperties2(c.physicalDevice_, &properties);
    c.deviceName_ = properties.properties.deviceName;
    c.limits_ = properties.properties.limits;
    std::copy(std::begin(ids.deviceUUID), std::end(ids.deviceUUID), c.deviceUuid_.begin());

    const std::vector<VkExtensionProperties> deviceExtensions = DeviceExtensions(c.physicalDevice_);
    std::vector<const char*> enabled = {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    for (const std::string& extension : options.deviceExtensions) {
        enabled.push_back(extension.c_str());
    }
    if (Has(deviceExtensions, "VK_KHR_portability_subset")) {
        enabled.push_back("VK_KHR_portability_subset");
    }
    const bool externalMemory = Has(deviceExtensions, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME) &&
                                Has(deviceExtensions, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    if (externalMemory) {
        enabled.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        enabled.push_back(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    }

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationSupport{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuerySupport{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    const bool rayExtensions =
        Has(deviceExtensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        Has(deviceExtensions, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
        Has(deviceExtensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    VkPhysicalDeviceVulkan12Features supported12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supported.pNext = &supported12;
    if (rayExtensions) {
        supported12.pNext = &accelerationSupport;
        accelerationSupport.pNext = &rayQuerySupport;
    }
    vkGetPhysicalDeviceFeatures2(c.physicalDevice_, &supported);
    c.rayQuery_ = rayExtensions && accelerationSupport.accelerationStructure &&
                  rayQuerySupport.rayQuery && supported12.bufferDeviceAddress;
    if (c.rayQuery_) {
        enabled.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        enabled.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        enabled.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }

    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    rayQuery.rayQuery = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    acceleration.accelerationStructure = VK_TRUE;
    acceleration.pNext = &rayQuery;
    VkPhysicalDeviceVulkan13Features features13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;
    features13.pNext = c.rayQuery_ ? &acceleration : nullptr;
    VkPhysicalDeviceVulkan12Features features12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.timelineSemaphore = VK_TRUE;
    features12.bufferDeviceAddress = supported12.bufferDeviceAddress;
    features12.pNext = &features13;
    VkPhysicalDeviceVulkan11Features features11{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features11.shaderDrawParameters = VK_TRUE;
    features11.pNext = &features12;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.features.samplerAnisotropy = supported.features.samplerAnisotropy;
    c.anisotropy_ = supported.features.samplerAnisotropy == VK_TRUE;
    features.features.fillModeNonSolid = supported.features.fillModeNonSolid;
    features.pNext = &features11;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = c.queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    deviceInfo.ppEnabledExtensionNames = enabled.data();
    CheckVk(vkCreateDevice(c.physicalDevice_, &deviceInfo, nullptr, &c.device_), "vkCreateDevice");
    vkGetDeviceQueue(c.device_, c.queueFamily_, 0, &c.queue_);
    c.pushDescriptorSet_ = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
        vkGetDeviceProcAddr(c.device_, "vkCmdPushDescriptorSetKHR"));
    if (externalMemory) {
        c.getMemoryFd_ = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
            vkGetDeviceProcAddr(c.device_, "vkGetMemoryFdKHR"));
        c.getSemaphoreFd_ = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
            vkGetDeviceProcAddr(c.device_, "vkGetSemaphoreFdKHR"));
    }

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags =
        supported12.bufferDeviceAddress ? VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT : 0;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = c.physicalDevice_;
    allocatorInfo.device = c.device_;
    allocatorInfo.instance = c.instance_;
    allocatorInfo.pVulkanFunctions = &functions;
    CheckVk(vmaCreateAllocator(&allocatorInfo, &c.allocator_), "vmaCreateAllocator");

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = c.queueFamily_;
    CheckVk(vkCreateCommandPool(c.device_, &poolInfo, nullptr, &c.commandPool_),
            "vkCreateCommandPool");
    c.submissions_ = c.CreateTimelineSemaphore();
    return context;
}

VulkanContext::~VulkanContext() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (submissions_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, submissions_, nullptr);
        }
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        vmaDestroyAllocator(allocator_);
        vkDestroyDevice(device_, nullptr);
    }
    if (messenger_ != VK_NULL_HANDLE) {
        destroyMessenger_(instance_, messenger_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

std::vector<std::string> VulkanContext::TakeValidationMessages() {
    std::lock_guard lock(messagesMutex_);
    return std::exchange(messages_, {});
}

void VulkanContext::Report(std::string message) {
    std::lock_guard lock(messagesMutex_);
    messages_.push_back(std::move(message));
}

std::uint32_t VulkanContext::DeviceLocalMemoryType(std::uint32_t typeBits) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            return i;
        }
    }
    throw std::runtime_error("No device-local memory type");
}

VkSemaphore VulkanContext::CreateTimelineSemaphore(std::uint64_t initial, bool exportable) const {
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type.initialValue = initial;
    VkExportSemaphoreCreateInfo exported{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    exported.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    if (exportable) {
        if (!SupportsExternalMemory()) {
            throw std::runtime_error("The Vulkan device cannot export semaphores");
        }
        type.pNext = &exported;
    }
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    info.pNext = &type;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    CheckVk(vkCreateSemaphore(device_, &info, nullptr, &semaphore), "vkCreateSemaphore");
    return semaphore;
}

VkCommandBuffer VulkanContext::Record(const std::function<void(VkCommandBuffer)>& record) const {
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    CheckVk(vkAllocateCommandBuffers(device_, &allocateInfo, &commands),
            "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commands, &begin);
    record(commands);
    // Make every write visible to the host and to later submissions.
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
    vkEndCommandBuffer(commands);
    return commands;
}

namespace {

std::vector<VkSemaphoreSubmitInfo> SubmitInfos(std::span<const TimelinePoint> points) {
    std::vector<VkSemaphoreSubmitInfo> infos;
    for (const TimelinePoint& point : points) {
        if (point.semaphore == VK_NULL_HANDLE) {
            continue;
        }
        VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        info.semaphore = point.semaphore;
        info.value = point.value;
        info.stageMask = point.stages;
        infos.push_back(info);
    }
    return infos;
}

}  // namespace

void VulkanContext::Submit(const std::function<void(VkCommandBuffer)>& record,
                           std::span<const TimelinePoint> waits,
                           std::span<const TimelinePoint> signals) const {
    std::lock_guard lock(submitMutex_);
    RecyclePending();
    VkCommandBuffer commands = Record(record);
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    CheckVk(vkCreateFence(device_, &fenceInfo, nullptr, &fence), "vkCreateFence");
    const std::vector<VkSemaphoreSubmitInfo> waitInfos = SubmitInfos(waits);
    const std::vector<VkSemaphoreSubmitInfo> signalInfos = SubmitInfos(signals);
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commands;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(waitInfos.size());
    submit.pWaitSemaphoreInfos = waitInfos.data();
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signalInfos.size());
    submit.pSignalSemaphoreInfos = signalInfos.data();
    const VkResult result = vkQueueSubmit2(queue_, 1, &submit, fence);
    if (result == VK_SUCCESS) {
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    }
    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, commandPool_, 1, &commands);
    CheckVk(result, "vkQueueSubmit2");
}

std::uint64_t VulkanContext::SubmitAsync(const std::function<void(VkCommandBuffer)>& record,
                                         std::span<const TimelinePoint> waits,
                                         std::span<const TimelinePoint> signals) const {
    std::lock_guard lock(submitMutex_);
    RecyclePending();
    VkCommandBuffer commands = Record(record);
    std::vector<TimelinePoint> allSignals(signals.begin(), signals.end());
    allSignals.push_back({submissions_, submitted_ + 1});
    const std::vector<VkSemaphoreSubmitInfo> waitInfos = SubmitInfos(waits);
    const std::vector<VkSemaphoreSubmitInfo> signalInfos = SubmitInfos(allSignals);
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = commands;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = static_cast<std::uint32_t>(waitInfos.size());
    submit.pWaitSemaphoreInfos = waitInfos.data();
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signalInfos.size());
    submit.pSignalSemaphoreInfos = signalInfos.data();
    const VkResult result = vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &commands);
        CheckVk(result, "vkQueueSubmit2");
    }
    pending_.emplace_back(commands, ++submitted_);
    return submitted_;
}

bool VulkanContext::WaitForSubmission(std::uint64_t submission, std::uint64_t timeoutNs) const {
    VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &submissions_;
    wait.pValues = &submission;
    return vkWaitSemaphores(device_, &wait, timeoutNs) == VK_SUCCESS;
}

void VulkanContext::RecyclePending() const {
    if (pending_.empty()) {
        return;
    }
    std::uint64_t done = 0;
    vkGetSemaphoreCounterValue(device_, submissions_, &done);
    auto finished = std::partition(pending_.begin(), pending_.end(),
                                   [done](const auto& entry) { return entry.second > done; });
    for (auto it = finished; it != pending_.end(); ++it) {
        vkFreeCommandBuffers(device_, commandPool_, 1, &it->first);
    }
    pending_.erase(finished, pending_.end());
}

void VulkanContext::WaitForSubmissions() const {
    std::lock_guard lock(submitMutex_);
    if (pending_.empty()) {
        return;
    }
    VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    wait.semaphoreCount = 1;
    wait.pSemaphores = &submissions_;
    wait.pValues = &submitted_;
    vkWaitSemaphores(device_, &wait, UINT64_MAX);
    RecyclePending();
}

bool VulkanContext::HasPendingSubmissions() const {
    std::lock_guard lock(submitMutex_);
    RecyclePending();
    return !pending_.empty();
}

}  // namespace ernie
