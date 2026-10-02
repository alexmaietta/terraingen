#include "ernie/gpu/pipeline.h"

#include <array>
#include <stdexcept>

namespace ernie {

namespace {

VkShaderModule CreateModule(VkDevice device, std::span<const std::uint32_t> spirv) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size_bytes();
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    CheckVk(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

std::vector<DescriptorBinding> StorageBindings(std::uint32_t count) {
    std::vector<DescriptorBinding> bindings(count);
    for (std::uint32_t b = 0; b < count; ++b) {
        bindings[b] = {b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT};
    }
    return bindings;
}

}  // namespace

DescriptorResource DescriptorResource::Of(const GpuBuffer& buffer) {
    DescriptorResource resource;
    resource.buffer = {buffer.Handle(), 0, VK_WHOLE_SIZE};
    return resource;
}

DescriptorResource DescriptorResource::Of(VkImageView view, VkImageLayout layout) {
    DescriptorResource resource;
    resource.image = {VK_NULL_HANDLE, view, layout};
    return resource;
}

DescriptorResource DescriptorResource::Of(const Sampler& sampler) {
    DescriptorResource resource;
    resource.image = {sampler.Handle(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    return resource;
}

PipelineLayout::PipelineLayout(const VulkanContext& context,
                               std::span<const DescriptorBinding> bindings,
                               std::uint32_t pushConstantBytes)
    : context_(context), bindings_(bindings.begin(), bindings.end()) {
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    for (const DescriptorBinding& binding : bindings) {
        layoutBindings.push_back({binding.binding, binding.type, 1, binding.stages, nullptr});
    }
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    setInfo.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
    setInfo.pBindings = layoutBindings.data();
    CheckVk(vkCreateDescriptorSetLayout(context.Device(), &setInfo, nullptr, &setLayout_),
            "vkCreateDescriptorSetLayout");
    const VkPushConstantRange range{VK_SHADER_STAGE_ALL, 0, pushConstantBytes};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    layoutInfo.pushConstantRangeCount = pushConstantBytes > 0 ? 1 : 0;
    layoutInfo.pPushConstantRanges = &range;
    CheckVk(vkCreatePipelineLayout(context.Device(), &layoutInfo, nullptr, &layout_),
            "vkCreatePipelineLayout");
}

PipelineLayout::~PipelineLayout() {
    vkDestroyPipelineLayout(context_.Device(), layout_, nullptr);
    vkDestroyDescriptorSetLayout(context_.Device(), setLayout_, nullptr);
}

void PipelineLayout::Push(VkCommandBuffer commands, VkPipelineBindPoint point,
                          std::span<const DescriptorResource> resources) const {
    if (resources.size() != bindings_.size()) {
        throw std::invalid_argument("PipelineLayout::Push got the wrong number of resources");
    }
    std::vector<VkWriteDescriptorSet> writes(resources.size());
    for (std::size_t i = 0; i < resources.size(); ++i) {
        VkWriteDescriptorSet& write = writes[i];
        write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstBinding = bindings_[i].binding;
        write.descriptorCount = 1;
        write.descriptorType = bindings_[i].type;
        switch (bindings_[i].type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            write.pBufferInfo = &resources[i].buffer;
            break;
        default:
            write.pImageInfo = &resources[i].image;
            break;
        }
    }
    context_.PushDescriptorSet()(commands, point, layout_, 0,
                                 static_cast<std::uint32_t>(writes.size()), writes.data());
}

void PipelineLayout::PushConstants(VkCommandBuffer commands, const void* data,
                                   std::uint32_t bytes) const {
    vkCmdPushConstants(commands, layout_, VK_SHADER_STAGE_ALL, 0, bytes, data);
}

ComputePipeline::ComputePipeline(const VulkanContext& context, const PipelineLayout& layout,
                                 std::span<const std::uint32_t> spirv,
                                 const std::string& entryPoint)
    : context_(context), layout_(&layout) {
    Create(spirv, entryPoint);
}

ComputePipeline::ComputePipeline(const VulkanContext& context, std::span<const std::uint32_t> spirv,
                                 std::uint32_t storageBuffers, const std::string& entryPoint)
    : context_(context),
      ownedLayout_(std::make_unique<PipelineLayout>(context, StorageBindings(storageBuffers))),
      layout_(ownedLayout_.get()) {
    Create(spirv, entryPoint);
}

void ComputePipeline::Create(std::span<const std::uint32_t> spirv, const std::string& entryPoint) {
    VkShaderModule module = CreateModule(context_.Device(), spirv);
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = entryPoint.c_str();
    info.layout = layout_->Handle();
    const VkResult result =
        vkCreateComputePipelines(context_.Device(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_);
    vkDestroyShaderModule(context_.Device(), module, nullptr);
    CheckVk(result, "vkCreateComputePipelines");
}

ComputePipeline::~ComputePipeline() {
    vkDestroyPipeline(context_.Device(), pipeline_, nullptr);
}

void ComputePipeline::Dispatch(VkCommandBuffer commands,
                               std::span<const DescriptorResource> resources, std::uint32_t groupsX,
                               std::uint32_t groupsY, std::uint32_t groupsZ) const {
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    layout_->Push(commands, VK_PIPELINE_BIND_POINT_COMPUTE, resources);
    vkCmdDispatch(commands, groupsX, groupsY, groupsZ);
}

void ComputePipeline::Dispatch(VkCommandBuffer commands, std::span<const GpuBuffer* const> buffers,
                               std::uint32_t groupsX, std::uint32_t groupsY,
                               std::uint32_t groupsZ) const {
    std::vector<DescriptorResource> resources;
    for (const GpuBuffer* buffer : buffers) {
        resources.push_back(DescriptorResource::Of(*buffer));
    }
    Dispatch(commands, resources, groupsX, groupsY, groupsZ);
}

GraphicsPipeline::GraphicsPipeline(const VulkanContext& context, const PipelineLayout& layout,
                                   const GraphicsPipelineDesc& desc)
    : context_(context) {
    VkDevice device = context.Device();
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<VkShaderModule> modules;
    auto addStage = [&](std::span<const std::uint32_t> spirv, VkShaderStageFlagBits stage,
                        const std::string& entry) {
        modules.push_back(CreateModule(device, spirv));
        VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage = stage;
        info.module = modules.back();
        info.pName = entry.c_str();
        stages.push_back(info);
    };
    addStage(desc.vertexSpirv, VK_SHADER_STAGE_VERTEX_BIT, desc.vertexEntry);
    if (!desc.fragmentSpirv.empty()) {
        addStage(desc.fragmentSpirv, VK_SHADER_STAGE_FRAGMENT_BIT, desc.fragmentEntry);
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = desc.depthBiasSlope != 0.0f ? VK_TRUE : VK_FALSE;
    raster.depthBiasSlopeFactor = desc.depthBiasSlope;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = desc.depthCompare;
    std::vector<VkFormat> colorFormats = desc.colorFormats;
    if (colorFormats.empty() && desc.colorFormat != VK_FORMAT_UNDEFINED) {
        colorFormats.push_back(desc.colorFormat);
    }
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    const std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(colorFormats.size(),
                                                                            blendAttachment);
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();
    const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                         VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    rendering.depthAttachmentFormat = desc.depthFormat;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout.Handle();
    const VkResult result =
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_);
    for (VkShaderModule module : modules) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    CheckVk(result, "vkCreateGraphicsPipelines");
}

GraphicsPipeline::~GraphicsPipeline() {
    vkDestroyPipeline(context_.Device(), pipeline_, nullptr);
}

void TransitionImage(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);
}

void BeginRendering(VkCommandBuffer commands, const RenderingTarget& target) {
    std::vector<VkRenderingAttachmentInfo> colors;
    const auto addColor = [&](VkImageView view, VkClearColorValue clear) {
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = target.loadColor ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = clear;
        colors.push_back(color);
    };
    if (!target.colors.empty()) {
        for (std::size_t i = 0; i < target.colors.size(); ++i) {
            addColor(target.colors[i],
                     i < target.clearColors.size() ? target.clearColors[i] : VkClearColorValue{});
        }
    } else if (target.color != VK_NULL_HANDLE) {
        addColor(target.color, target.clearColor);
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = target.depth;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = target.loadDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, target.extent};
    info.layerCount = 1;
    info.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
    info.pColorAttachments = colors.data();
    info.pDepthAttachment = target.depth != VK_NULL_HANDLE ? &depth : nullptr;
    vkCmdBeginRendering(commands, &info);

    const auto width = static_cast<float>(target.extent.width);
    const auto height = static_cast<float>(target.extent.height);
    const VkViewport viewport{0.0f, height, width, -height, 0.0f, 1.0f};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, target.extent};
    vkCmdSetScissor(commands, 0, 1, &scissor);
}

}  // namespace ernie
