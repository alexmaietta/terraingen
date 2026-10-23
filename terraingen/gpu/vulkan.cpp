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

/// Throws std::runtime_error naming `what` and the result unless `result` is VK_SUCCESS.
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

/// Destroys what `p` holds; Vulkan ignores null handles, so a partly made pipeline is fine.
void DestroyPipeline(VkDevice device, const Pipeline& p) {
    vkDestroyPipeline(device, p.pipeline, nullptr);
    vkDestroyPipelineLayout(device, p.layout, nullptr);
    vkDestroyDescriptorSetLayout(device, p.setLayout, nullptr);
    vkDestroyShaderModule(device, p.module, nullptr);
}

VkShaderModule CreateShaderModule(VkDevice device, Span<const unsigned char> spirv) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spirv.size();
    info.pCode = reinterpret_cast<const std::uint32_t*>(spirv.data());
    VkShaderModule module = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(device, &info, nullptr, &module), "shader module");
    return module;
}

/// A layout of `bindings` storage buffers, bound in order.
VkDescriptorSetLayout CreateSetLayout(VkDevice device, std::size_t bindings) {
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings(bindings);
    for (std::size_t i = 0; i < bindings; ++i) {
        layoutBindings[i] = {static_cast<std::uint32_t>(i), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                             VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    }
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<std::uint32_t>(bindings);
    info.pBindings = layoutBindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    Check(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout), "descriptor set layout");
    return layout;
}

VkPipelineLayout CreatePipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout,
                                      std::size_t pushSize) {
    const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                    static_cast<std::uint32_t>(std::max<std::size_t>(pushSize, 4))};
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &setLayout;
    info.pushConstantRangeCount = pushSize > 0 ? 1u : 0u;
    info.pPushConstantRanges = &range;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    Check(vkCreatePipelineLayout(device, &info, nullptr, &layout), "pipeline layout");
    return layout;
}

/// The pipeline running `spirv` with `bindings` storage buffers and `pushSize` bytes of push
/// constants; nothing is left behind when a step fails.
Pipeline CreatePipeline(VkDevice device, Span<const unsigned char> spirv, std::size_t bindings,
                        std::size_t pushSize) {
    Pipeline p;
    try {
        p.module = CreateShaderModule(device, spirv);
        p.setLayout = CreateSetLayout(device, bindings);
        p.layout = CreatePipelineLayout(device, p.setLayout, pushSize);
        VkComputePipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = p.module;
        info.stage.pName = "main";
        info.layout = p.layout;
        Check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &p.pipeline),
              "compute pipeline");
    } catch (...) {
        DestroyPipeline(device, p);
        throw;
    }
    return p;
}

/// A host-visible storage buffer.
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

/// The first memory type allowed by `bits` that has all of `flags`.
std::uint32_t MemoryType(const VkPhysicalDeviceMemoryProperties& memory, std::uint32_t bits,
                         VkMemoryPropertyFlags flags) {
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0 && (memory.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    throw std::runtime_error("No host-visible, coherent Vulkan memory type");
}

/// The memory type for buffers the host writes and reads back: host-cached where the device has
/// one, since reading results back from uncached memory is many times slower, otherwise any
/// host-visible, coherent type.
std::uint32_t HostMemoryType(const VkPhysicalDeviceMemoryProperties& memory, std::uint32_t bits) {
    constexpr VkMemoryPropertyFlags kHost =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    try {
        return MemoryType(memory, bits, kHost | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    } catch (const std::runtime_error&) {
        return MemoryType(memory, bits, kHost);
    }
}

/// Frees `buffer` and resets it, which may be partly created.
void FreeBuffer(VkDevice device, Buffer& buffer) {
    if (buffer.mapped != nullptr) {
        vkUnmapMemory(device, buffer.memory);
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
    }
    buffer = {};
}

/// Creates a mapped, host-coherent storage buffer of `size` bytes into `buffer`, which the caller
/// frees even when this throws.
void MakeBuffer(VkDevice device, const VkPhysicalDeviceMemoryProperties& memory, std::size_t size,
                Buffer& buffer) {
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
    allocate.memoryTypeIndex = HostMemoryType(memory, requirements.memoryTypeBits);
    Check(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory), "memory allocation");
    Check(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0), "memory binding");
    Check(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped),
          "memory mapping");
}

/// The kind of a device the driver reports as `type`.
DeviceKind ToDeviceKind(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return DeviceKind::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return DeviceKind::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return DeviceKind::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return DeviceKind::Cpu;
    default:
        return DeviceKind::Other;
    }
}

/// Opens the Vulkan loader and creates an instance with its functions loaded.
VkInstance CreateInstance() {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("no Vulkan loader found; " + VulkanInstallHint());
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
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult created = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (created == VK_ERROR_INCOMPATIBLE_DRIVER) {
        throw std::runtime_error("no Vulkan driver found; " + VulkanInstallHint());
    }
    Check(created, "instance creation");
    volkLoadInstance(instance);
    return instance;
}

/// The device `TERRAINGEN_DEVICE` names, otherwise the one `PickDevice` picks.
VkPhysicalDevice ChooseDevice(VkInstance instance) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (devices.empty()) {
        throw std::runtime_error("no Vulkan devices; " + VulkanInstallHint());
    }
    if (const char* wanted = std::getenv("TERRAINGEN_DEVICE"); wanted && *wanted) {
        const auto chosen = static_cast<std::size_t>(std::strtoul(wanted, nullptr, 10));
        if (chosen >= devices.size()) {
            throw std::runtime_error("TERRAINGEN_DEVICE " + std::string(wanted) + " is past the " +
                                     std::to_string(devices.size()) + " Vulkan devices");
        }
        return devices[chosen];
    }
    std::vector<DeviceKind> kinds(devices.size());
    std::string names;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        kinds[i] = ToDeviceKind(properties.deviceType);
        names += (i == 0 ? "" : ", ") + std::string(properties.deviceName);
    }
    if (const std::optional<std::size_t> chosen = PickDevice(kinds)) {
        return devices[*chosen];
    }
    throw std::runtime_error("only software Vulkan (" + names +
                             "), which is slower than the CPU backend; set TERRAINGEN_DEVICE to "
                             "its index to use it anyway, or " +
                             VulkanInstallHint());
}

/// The first queue family of `physical` that supports compute.
std::uint32_t ComputeQueueFamily(VkPhysicalDevice physical) {
    std::uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
    std::vector<VkQueueFamilyProperties> properties(families);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, properties.data());
    for (std::uint32_t i = 0; i < families; ++i) {
        if ((properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
            return i;
        }
    }
    throw std::runtime_error("the Vulkan device has no compute queue");
}

/// A logical device with one queue of `family`, with its functions loaded.
VkDevice CreateDevice(VkPhysicalDevice physical, std::uint32_t family) {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    Check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "device creation");
    volkLoadDevice(device);
    return device;
}

VkCommandPool CreateCommandPool(VkDevice device, std::uint32_t family) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    VkCommandPool pool = VK_NULL_HANDLE;
    Check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "command pool");
    return pool;
}

/// The objects of one `Run`, released when it goes out of scope however the run ends.
struct RunResources {
    VkDevice device;
    VkCommandPool pool;
    std::vector<Buffer> buffers;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    RunResources(VkDevice owner, VkCommandPool commandPool, std::size_t count)
        : device(owner), pool(commandPool), buffers(count) {}
    RunResources(const RunResources&) = delete;
    RunResources& operator=(const RunResources&) = delete;

    ~RunResources() {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (command != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, pool, 1, &command);
        }
        if (descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        }
        for (Buffer& buffer : buffers) {
            FreeBuffer(device, buffer);
        }
    }
};

/// Creates a device buffer for each host buffer and copies the host data into it.
void UploadBuffers(Span<const DeviceBuffer> host, const VkPhysicalDeviceMemoryProperties& memory,
                   RunResources& run) {
    for (std::size_t i = 0; i < host.size(); ++i) {
        MakeBuffer(run.device, memory, host[i].bytes.size(), run.buffers[i]);
        std::memcpy(run.buffers[i].mapped, host[i].bytes.data(), host[i].bytes.size());
    }
}

/// A descriptor set binding the run's buffers as storage buffers 0, 1, ...
VkDescriptorSet BindBuffers(const Pipeline& pipeline, RunResources& run) {
    VkDescriptorPoolSize poolSize{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        static_cast<std::uint32_t>(std::max<std::size_t>(run.buffers.size(), 1))};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    Check(vkCreateDescriptorPool(run.device, &poolInfo, nullptr, &run.descriptorPool),
          "descriptor pool");
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = run.descriptorPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &pipeline.setLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    Check(vkAllocateDescriptorSets(run.device, &setInfo, &set), "descriptor set");
    std::vector<VkDescriptorBufferInfo> infos(run.buffers.size());
    std::vector<VkWriteDescriptorSet> writes(run.buffers.size());
    for (std::size_t i = 0; i < run.buffers.size(); ++i) {
        infos[i] = {run.buffers[i].buffer, 0, VK_WHOLE_SIZE};
        writes[i] = {};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = static_cast<std::uint32_t>(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(run.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);
    return set;
}

/// Records the dispatch, then a barrier making its writes visible to the host.
void RecordDispatch(const Pipeline& pipeline, VkDescriptorSet set,
                    Span<const std::byte> pushConstants, std::array<std::uint32_t, 3> groups,
                    RunResources& run) {
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = run.pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(run.device, &commandInfo, &run.command), "command buffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(run.command, &begin), "command recording");
    vkCmdBindPipeline(run.command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline);
    vkCmdBindDescriptorSets(run.command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, 1,
                            &set, 0, nullptr);
    if (!pushConstants.empty()) {
        vkCmdPushConstants(run.command, pipeline.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           static_cast<std::uint32_t>(pushConstants.size()), pushConstants.data());
    }
    vkCmdDispatch(run.command, groups[0], groups[1], groups[2]);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(run.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    Check(vkEndCommandBuffer(run.command), "command recording");
}

void SubmitAndWait(VkQueue queue, RunResources& run) {
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    Check(vkCreateFence(run.device, &fenceInfo, nullptr, &run.fence), "fence");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &run.command;
    Check(vkQueueSubmit(queue, 1, &submit, run.fence), "submission");
    Check(vkWaitForFences(run.device, 1, &run.fence, VK_TRUE, UINT64_MAX), "wait");
}

/// Copies the output buffers back to the host.
void DownloadOutputs(const RunResources& run, Span<const DeviceBuffer> host) {
    for (std::size_t i = 0; i < host.size(); ++i) {
        if (host[i].output != nullptr) {
            std::memcpy(host[i].output, run.buffers[i].mapped, host[i].bytes.size());
        }
    }
}

}  // namespace

/// The instance, device and cached pipelines; destroys whatever was created, so a partly opened
/// device is released as a fully opened one is.
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

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    ~State() {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            for (const auto& [key, p] : pipelines) {
                DestroyPipeline(device, p);
            }
            if (pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device, pool, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }

    /// Opens the device: instance, physical device, queue, logical device and command pool.
    void Open() {
        instance = CreateInstance();
        physical = ChooseDevice(instance);
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical, &properties);
        name = properties.deviceName;
        maxPushConstants = properties.limits.maxPushConstantsSize;
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        family = ComputeQueueFamily(physical);
        device = CreateDevice(physical, family);
        vkGetDeviceQueue(device, family, 0, &queue);
        pool = CreateCommandPool(device, family);
    }

    /// The pipeline of `spirv` with `bindings` buffers and `pushSize` bytes of push constants,
    /// created on first use.
    const Pipeline& CachedPipeline(Span<const unsigned char> spirv, std::size_t bindings,
                                   std::size_t pushSize) {
        const auto key = std::make_tuple(spirv.data(), bindings, pushSize);
        auto found = pipelines.find(key);
        if (found != pipelines.end()) {
            return found->second;
        }
        return pipelines.emplace(key, CreatePipeline(device, spirv, bindings, pushSize))
            .first->second;
    }
};

std::optional<std::size_t> PickDevice(Span<const DeviceKind> kinds) {
    for (const DeviceKind kind :
         {DeviceKind::Discrete, DeviceKind::Integrated, DeviceKind::Virtual, DeviceKind::Other}) {
        const auto found = std::find(kinds.begin(), kinds.end(), kind);
        if (found != kinds.end()) {
            return static_cast<std::size_t>(found - kinds.begin());
        }
    }
    return std::nullopt;
}

std::string VulkanInstallHint() {
#if defined(__APPLE__)
    return "install MoltenVK (brew install molten-vk) or the Vulkan SDK from vulkan.lunarg.com";
#elif defined(_WIN32)
    return "install or update the GPU driver, which provides the Vulkan loader (vulkan-1.dll)";
#else
    return "install the Vulkan loader and a GPU driver (on Debian and Ubuntu: sudo apt install "
           "libvulkan1, with the NVIDIA driver or mesa-vulkan-drivers); in Docker on an NVIDIA "
           "GPU, include graphics in NVIDIA_DRIVER_CAPABILITIES";
#endif
}

VulkanCompute::VulkanCompute(std::unique_ptr<State> state) : state_(std::move(state)) {}

VulkanCompute::~VulkanCompute() = default;

std::unique_ptr<VulkanCompute> VulkanCompute::Open() {
    auto state = std::make_unique<State>();
    state->Open();
    return std::unique_ptr<VulkanCompute>(new VulkanCompute(std::move(state)));
}

const std::string& VulkanCompute::DeviceName() const {
    return state_->name;
}

bool VulkanCompute::CanRun(const KernelProgram& program) const {
    return !program.spirv.empty();
}

void VulkanCompute::Run(const KernelProgram& program, Span<const DeviceBuffer> buffers,
                        Span<const std::byte> pushConstants, const Dispatch& dispatch) {
    State& s = *state_;
    if (!CanRun(program)) {
        throw std::runtime_error("The kernel has no SPIR-V for Vulkan");
    }
    if (pushConstants.size() > s.maxPushConstants) {
        throw std::runtime_error("Push constants exceed the device's limit");
    }
    const std::lock_guard lock(s.mutex);
    const Pipeline& pipeline =
        s.CachedPipeline(program.spirv, buffers.size(), pushConstants.size());
    RunResources run(s.device, s.pool, buffers.size());
    UploadBuffers(buffers, s.memory, run);
    const VkDescriptorSet set = BindBuffers(pipeline, run);
    RecordDispatch(pipeline, set, pushConstants, dispatch.groups, run);
    SubmitAndWait(s.queue, run);
    DownloadOutputs(run, buffers);
}

}  // namespace terraingen
