#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/gpu_image.h"
#include "ernie/gpu/vulkan_context.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ernie {

/// One binding of a pipeline's push-descriptor set (set 0).
struct DescriptorBinding {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    VkShaderStageFlags stages = VK_SHADER_STAGE_ALL;
};

/// What a binding points at when descriptors are pushed.
struct DescriptorResource {
    VkDescriptorBufferInfo buffer{};
    VkDescriptorImageInfo image{};

    static DescriptorResource Of(const GpuBuffer& buffer);
    static DescriptorResource Of(VkImageView view,
                                 VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    static DescriptorResource Of(const Sampler& sampler);
};

/// A pipeline layout with one push-descriptor set and an optional push-constant block shared by
/// all stages.
///
/// Every pass binds its resources by pushing descriptors when it records, so there are no
/// descriptor pools or sets to manage.
class PipelineLayout {
  public:
    /// @throws std::runtime_error if the layout cannot be created.
    PipelineLayout(const VulkanContext& context, std::span<const DescriptorBinding> bindings,
                   std::uint32_t pushConstantBytes = 0);
    ~PipelineLayout();
    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout& operator=(const PipelineLayout&) = delete;

    VkPipelineLayout Handle() const {
        return layout_;
    }

    /// Pushes `resources`, one per binding in the order the layout was created with.
    /// @throws std::invalid_argument if the number of resources does not match.
    void Push(VkCommandBuffer commands, VkPipelineBindPoint point,
              std::span<const DescriptorResource> resources) const;

    /// Sets `bytes` of push constants at offset 0.
    void PushConstants(VkCommandBuffer commands, const void* data, std::uint32_t bytes) const;

  private:
    const VulkanContext& context_;
    std::vector<DescriptorBinding> bindings_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

/// A compute pipeline over a `PipelineLayout`.
class ComputePipeline {
  public:
    /// @throws std::runtime_error if the pipeline cannot be created.
    ComputePipeline(const VulkanContext& context, const PipelineLayout& layout,
                    std::span<const std::uint32_t> spirv, const std::string& entryPoint = "main");

    /// A compute pipeline whose shader reads and writes `storageBuffers` storage buffers at
    /// bindings 0, 1, ...; it owns its layout.
    ComputePipeline(const VulkanContext& context, std::span<const std::uint32_t> spirv,
                    std::uint32_t storageBuffers, const std::string& entryPoint = "main");
    ~ComputePipeline();
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    /// Pushes `resources` and dispatches `groupsX` x `groupsY` x `groupsZ` workgroups.
    /// @throws std::invalid_argument if the number of resources does not match the layout.
    void Dispatch(VkCommandBuffer commands, std::span<const DescriptorResource> resources,
                  std::uint32_t groupsX, std::uint32_t groupsY = 1,
                  std::uint32_t groupsZ = 1) const;

    /// `Dispatch` with storage buffers, for pipelines made from a buffer count.
    void Dispatch(VkCommandBuffer commands, std::span<const GpuBuffer* const> buffers,
                  std::uint32_t groupsX, std::uint32_t groupsY = 1,
                  std::uint32_t groupsZ = 1) const;

  private:
    void Create(std::span<const std::uint32_t> spirv, const std::string& entryPoint);

    const VulkanContext& context_;
    std::unique_ptr<PipelineLayout> ownedLayout_;
    const PipelineLayout* layout_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

/// Fixed-function state of a graphics pipeline that draws triangle lists without vertex buffers.
struct GraphicsPipelineDesc {
    std::span<const std::uint32_t> vertexSpirv;
    std::span<const std::uint32_t> fragmentSpirv;  ///< Empty for depth-only passes.
    std::string vertexEntry = "main";
    std::string fragmentEntry = "main";
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;  ///< Undefined for depth-only passes.
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkCompareOp depthCompare = VK_COMPARE_OP_LESS;
    float depthBiasSlope = 0.0f;  ///< Slope-scaled depth bias, e.g. for shadow maps.
};

/// A graphics pipeline for dynamic rendering, with dynamic viewport and scissor and no culling.
class GraphicsPipeline {
  public:
    /// @throws std::runtime_error if the pipeline cannot be created.
    GraphicsPipeline(const VulkanContext& context, const PipelineLayout& layout,
                     const GraphicsPipelineDesc& desc);
    ~GraphicsPipeline();
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline Handle() const {
        return pipeline_;
    }

  private:
    const VulkanContext& context_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

/// Attachments of one dynamic-rendering pass.
struct RenderingTarget {
    VkImageView color = VK_NULL_HANDLE;  ///< Null for depth-only passes.
    VkImageView depth = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkClearColorValue clearColor{};
    bool loadColor = false;  ///< Keep the colour attachment's contents instead of clearing it.
};

/// Records a barrier moving every subresource of `image` from `from` to `to`; waits for and makes
/// visible all earlier work. For images Ernie does not own, such as swapchain images.
void TransitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to);

/// Begins dynamic rendering into `target`, clearing depth to 1, and sets a viewport flipped so
/// that normalised device y points up (row 0 is y = +1), as in WebGPU and OpenGL.
void BeginRendering(VkCommandBuffer commands, const RenderingTarget& target);

}  // namespace ernie
