#include "terraingen/gpu/vulkan.h"

#include "terraingen/util/log.h"

#include <volk.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace terraingen {

namespace {

void Check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("Vulkan ") + what + " failed (" +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

/// A pipeline for one kernel and buffer count.
struct Pipeline {
    VkShaderModule module = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

/// A host-visible storage buffer.
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

}  // namespace

struct VulkanCompute::State {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    std::uint32_t maxPushConstants = 0;
    std::string name;
    std::mutex mutex;
    /// Pipelines keyed by kernel code, binding count and push-constant size, which all shape the
    /// pipeline layout.
    std::map<std::tuple<const unsigned char*, std::size_t, std::size_t>, Pipeline> pipelines;

    std::uint32_t MemoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) != 0 && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
        throw std::runtime_error("No host-visible, coherent Vulkan memory type");
    }

    Buffer MakeBuffer(std::size_t size) {
        Buffer buffer;
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = std::max<std::size_t>(size, 4);
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Check(vkCreateBuffer(device, &info, nullptr, &buffer.buffer), "buffer creation");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex =
            MemoryType(requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory), "memory allocation");
        Check(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0), "memory binding");
        Check(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped),
              "memory mapping");
        return buffer;
    }

    void Free(Buffer& buffer) {
        if (buffer.memory != VK_NULL_HANDLE) {
            vkUnmapMemory(device, buffer.memory);
            vkFreeMemory(device, buffer.memory, nullptr);
        }
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer.buffer, nullptr);
        }
        buffer = {};
    }

    const Pipeline& PipelineFor(const ComputeKernel& kernel, std::size_t bindings,
                                std::size_t pushSize) {
        const auto key = std::make_tuple(kernel.spirv, bindings, pushSize);
        auto found = pipelines.find(key);
        if (found != pipelines.end()) {
            return found->second;
        }
        Pipeline p;
        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = kernel.size;
        moduleInfo.pCode = reinterpret_cast<const std::uint32_t*>(kernel.spirv);
        Check(vkCreateShaderModule(device, &moduleInfo, nullptr, &p.module), "shader module");
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings(bindings);
        for (std::size_t i = 0; i < bindings; ++i) {
            layoutBindings[i] = {static_cast<std::uint32_t>(i), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        }
        VkDescriptorSetLayoutCreateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setInfo.bindingCount = static_cast<std::uint32_t>(bindings);
        setInfo.pBindings = layoutBindings.data();
        Check(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &p.setLayout),
              "descriptor set layout");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                  static_cast<std::uint32_t>(std::max<std::size_t>(pushSize, 4))};
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &p.setLayout;
        layoutInfo.pushConstantRangeCount = pushSize > 0 ? 1u : 0u;
        layoutInfo.pPushConstantRanges = &range;
        Check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &p.layout), "pipeline layout");
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = {};
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = p.module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = p.layout;
        Check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                       &p.pipeline),
              "compute pipeline");
        return pipelines.emplace(key, p).first->second;
    }
};

namespace {

std::once_flag gOnce;
VulkanCompute* gCompute = nullptr;
std::string gReason;

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

VulkanCompute::VulkanCompute(State* state) : state_(state) {}

VulkanCompute::~VulkanCompute() {
    State& s = *state_;
    if (s.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(s.device);
        for (auto& [key, p] : s.pipelines) {
            vkDestroyPipeline(s.device, p.pipeline, nullptr);
            vkDestroyPipelineLayout(s.device, p.layout, nullptr);
            vkDestroyDescriptorSetLayout(s.device, p.setLayout, nullptr);
            vkDestroyShaderModule(s.device, p.module, nullptr);
        }
        vkDestroyCommandPool(s.device, s.pool, nullptr);
        vkDestroyDevice(s.device, nullptr);
    }
    if (s.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(s.instance, nullptr);
    }
    delete state_;
}

VulkanCompute* VulkanCompute::Get() {
    std::call_once(gOnce, [] {
        auto* state = new State;
        try {
            if (volkInitialize() != VK_SUCCESS) {
                throw std::runtime_error("no Vulkan loader found");
            }
            VkApplicationInfo app{};
            app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app.pApplicationName = "TerrainGen";
            app.apiVersion = VK_API_VERSION_1_1;
            VkInstanceCreateInfo instanceInfo{};
            instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            instanceInfo.pApplicationInfo = &app;
#ifdef __APPLE__
            const char* portability = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
            instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            instanceInfo.enabledExtensionCount = 1;
            instanceInfo.ppEnabledExtensionNames = &portability;
#endif
            Check(vkCreateInstance(&instanceInfo, nullptr, &state->instance), "instance creation");
            volkLoadInstance(state->instance);
            std::uint32_t count = 0;
            vkEnumeratePhysicalDevices(state->instance, &count, nullptr);
            std::vector<VkPhysicalDevice> devices(count);
            vkEnumeratePhysicalDevices(state->instance, &count, devices.data());
            if (devices.empty()) {
                throw std::runtime_error("no Vulkan devices");
            }
            std::size_t chosen = 0;
            if (const char* wanted = std::getenv("TERRAINGEN_DEVICE"); wanted && *wanted) {
                chosen = static_cast<std::size_t>(std::strtoul(wanted, nullptr, 10));
                if (chosen >= devices.size()) {
                    throw std::runtime_error("TERRAINGEN_DEVICE " + std::string(wanted) +
                                             " is past the " + std::to_string(devices.size()) +
                                             " Vulkan devices");
                }
            } else {
                int best = 99;
                for (std::size_t i = 0; i < devices.size(); ++i) {
                    VkPhysicalDeviceProperties properties;
                    vkGetPhysicalDeviceProperties(devices[i], &properties);
                    if (DeviceRank(properties.deviceType) < best) {
                        best = DeviceRank(properties.deviceType);
                        chosen = i;
                    }
                }
            }
            state->physical = devices[chosen];
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(state->physical, &properties);
            state->name = properties.deviceName;
            state->maxPushConstants = properties.limits.maxPushConstantsSize;
            vkGetPhysicalDeviceMemoryProperties(state->physical, &state->memory);
            std::uint32_t families = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(state->physical, &families, nullptr);
            std::vector<VkQueueFamilyProperties> familyProperties(families);
            vkGetPhysicalDeviceQueueFamilyProperties(state->physical, &families,
                                                     familyProperties.data());
            bool found = false;
            for (std::uint32_t i = 0; i < families && !found; ++i) {
                if ((familyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
                    state->family = i;
                    found = true;
                }
            }
            if (!found) {
                throw std::runtime_error("the Vulkan device has no compute queue");
            }
            const float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = state->family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            VkDeviceCreateInfo deviceInfo{};
            deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            Check(vkCreateDevice(state->physical, &deviceInfo, nullptr, &state->device),
                  "device creation");
            volkLoadDevice(state->device);
            vkGetDeviceQueue(state->device, state->family, 0, &state->queue);
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = state->family;
            Check(vkCreateCommandPool(state->device, &poolInfo, nullptr, &state->pool),
                  "command pool");
            gCompute = new VulkanCompute(state);
            LogInfo("gpu", "Vulkan compute on ", state->name);
        } catch (const std::exception& error) {
            gReason = error.what();
            if (state->device != VK_NULL_HANDLE) {
                vkDestroyDevice(state->device, nullptr);
            }
            if (state->instance != VK_NULL_HANDLE) {
                vkDestroyInstance(state->instance, nullptr);
            }
            delete state;
            LogInfo("gpu", "Vulkan unavailable: ", gReason);
        }
    });
    return gCompute;
}

std::string VulkanCompute::Reason() {
    Get();
    return gReason;
}

const std::string& VulkanCompute::DeviceName() const {
    return state_->name;
}

void VulkanCompute::Run(const ComputeKernel& kernel,
                        const std::vector<std::span<std::byte>>& buffers,
                        std::span<const std::byte> pushConstants,
                        std::array<std::uint32_t, 3> groups) {
    State& s = *state_;
    if (pushConstants.size() > s.maxPushConstants) {
        throw std::runtime_error("Push constants exceed the device's limit");
    }
    const std::lock_guard lock(s.mutex);
    const Pipeline& pipeline = s.PipelineFor(kernel, buffers.size(), pushConstants.size());
    std::vector<Buffer> device(buffers.size());
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    const auto release = [&] {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(s.device, fence, nullptr);
        }
        if (command != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(s.device, s.pool, 1, &command);
        }
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(s.device, descriptorPool, nullptr);
        }
        for (Buffer& buffer : device) {
            s.Free(buffer);
        }
    };
    try {
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            device[i] = s.MakeBuffer(buffers[i].size());
            std::memcpy(device[i].mapped, buffers[i].data(), buffers[i].size());
        }
        VkDescriptorPoolSize poolSize{
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            static_cast<std::uint32_t>(std::max<std::size_t>(buffers.size(), 1))};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        Check(vkCreateDescriptorPool(s.device, &poolInfo, nullptr, &descriptorPool),
              "descriptor pool");
        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = descriptorPool;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &pipeline.setLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        Check(vkAllocateDescriptorSets(s.device, &setInfo, &set), "descriptor set");
        std::vector<VkDescriptorBufferInfo> infos(buffers.size());
        std::vector<VkWriteDescriptorSet> writes(buffers.size());
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            infos[i] = {device[i].buffer, 0, VK_WHOLE_SIZE};
            writes[i] = {};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = static_cast<std::uint32_t>(i);
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(s.device, static_cast<std::uint32_t>(writes.size()), writes.data(),
                               0, nullptr);
        VkCommandBufferAllocateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandInfo.commandPool = s.pool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(s.device, &commandInfo, &command), "command buffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(command, &begin), "command recording");
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, 1,
                                &set, 0, nullptr);
        if (!pushConstants.empty()) {
            vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               static_cast<std::uint32_t>(pushConstants.size()),
                               pushConstants.data());
        }
        vkCmdDispatch(command, groups[0], groups[1], groups[2]);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        Check(vkEndCommandBuffer(command), "command recording");
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Check(vkCreateFence(s.device, &fenceInfo, nullptr, &fence), "fence");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        Check(vkQueueSubmit(s.queue, 1, &submit, fence), "submission");
        Check(vkWaitForFences(s.device, 1, &fence, VK_TRUE, UINT64_MAX), "wait");
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            std::memcpy(buffers[i].data(), device[i].mapped, buffers[i].size());
        }
    } catch (...) {
        release();
        throw;
    }
    release();
}

}  // namespace terraingen
