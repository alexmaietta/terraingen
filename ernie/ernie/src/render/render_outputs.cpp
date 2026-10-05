#include "ernie/render/render_outputs.h"

#include "product_frame.h"

#include <stdexcept>
#include <utility>

namespace ernie {

RenderVar::RenderVar(std::shared_ptr<ProductFrame> frame, Path path, std::string name)
    : frame_(std::move(frame)), path_(std::move(path)), name_(std::move(name)) {}

namespace {

struct TexelLayout {
    DType type;
    std::int64_t channels;
};

TexelLayout LayoutOf(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        return {DType::UInt8, 4};
    case VK_FORMAT_R16G16B16A16_UINT:
        return {DType::UInt16, 4};
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return {DType::Float32, 4};
    case VK_FORMAT_R32_SFLOAT:
        return {DType::Float32, 1};
    case VK_FORMAT_R32_UINT:
        return {DType::UInt32, 1};
    default:
        throw std::invalid_argument("Render var with an unsupported format");
    }
}

/// (layers, rows, cols) for layered images, (rows, cols) for one channel, else (rows, cols,
/// channels).
std::vector<std::int64_t> ShapeOf(const GpuImage& image) {
    const ImageDesc& desc = image.Desc();
    const std::int64_t channels = LayoutOf(desc.format).channels;
    if (desc.layers > 1) {
        return {desc.layers, desc.height, desc.width};
    }
    if (channels == 1) {
        return {desc.height, desc.width};
    }
    return {desc.height, desc.width, channels};
}

/// Records a tightly packed copy of every layer of `image` into the start of `buffer`, leaving
/// the image in the general layout.
void RecordCopy(VkCommandBuffer commands, GpuImage& image, GpuBuffer& buffer) {
    const ImageDesc& desc = image.Desc();
    const VkDeviceSize layerBytes =
        VkDeviceSize{desc.width} * desc.height * GpuImage::TexelBytes(desc.format);
    image.Transition(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    std::vector<VkBufferImageCopy> regions;
    for (std::uint32_t layer = 0; layer < desc.layers; ++layer) {
        VkBufferImageCopy region{};
        region.bufferOffset = layer * layerBytes;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
        region.imageExtent = {desc.width, desc.height, 1};
        regions.push_back(region);
    }
    vkCmdCopyImageToBuffer(commands, image.Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer.Handle(), static_cast<std::uint32_t>(regions.size()),
                           regions.data());
    image.Transition(commands, VK_IMAGE_LAYOUT_GENERAL);
}

}  // namespace

std::uint32_t RenderVar::Width() const {
    return frame_->images->Extent().width;
}

std::uint32_t RenderVar::Height() const {
    return frame_->images->Extent().height;
}

bool RenderVar::IsMapped() const {
    return frame_ != nullptr && frame_->arrayMaps > 0;
}

std::vector<std::int64_t> RenderVar::Shape() const {
    return ShapeOf(Image());
}

DType RenderVar::Type() const {
    return LayoutOf(Image().Desc().format).type;
}

GpuImage& RenderVar::Image() const {
    if (frame_ == nullptr) {
        throw std::invalid_argument("Empty render var");
    }
    GpuImage* image = frame_->images->Var(name_);
    if (image == nullptr) {
        throw std::invalid_argument("The frame has no render var " + name_);
    }
    return *image;
}

MappedRenderVar RenderVar::Map(MapDevice device, std::uintptr_t syncStream) {
    if (frame_ == nullptr) {
        throw std::invalid_argument("RenderVar::Map on an empty render var");
    }
    ProductFrame& frame = *frame_;
    GpuImage& image = Image();
    const std::vector<std::int64_t> shape = ShapeOf(image);
    const DType type = LayoutOf(image.Desc().format).type;
    const VkDeviceSize bytes = VkDeviceSize{image.Desc().width} * image.Desc().height *
                               image.Desc().layers * GpuImage::TexelBytes(image.Desc().format);
    MappedRenderVar mapped;
    mapped.device = device;
    if (device == MapDevice::Cpu) {
        frame.context->WaitForSubmission(frame.submission);
        GpuBuffer readback(*frame.context, bytes, 0, BufferMemory::Readback);
        frame.context->Submit(
            [&](VkCommandBuffer commands) { RecordCopy(commands, image, readback); });
        auto owner = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(bytes));
        readback.Read(*owner);
        mapped.tensor = Tensor(owner->data(), type, shape, {}, {}, owner);
        return mapped;
    }
    if (frame.cuda == nullptr) {
        throw std::invalid_argument("Mapping a render var to CUDA needs CUDA interop");
    }
    if (device == MapDevice::CudaArray) {
        if (image.Desc().layers > 1) {
            throw std::invalid_argument("Render var " + name_ +
                                        " has several layers; map it to Device.CUDA instead");
        }
        mapped.array = frame.cuda->MapImage(image);
        frame.cuda->WaitForVulkan(syncStream);
        ++frame.arrayMaps;
        return mapped;
    }
    std::shared_ptr<GpuBuffer> buffer;
    for (const std::shared_ptr<GpuBuffer>& candidate : *frame.linearPool) {
        if (candidate.use_count() == 1 && candidate->Size() >= bytes) {
            buffer = candidate;
            break;
        }
    }
    if (buffer == nullptr) {
        buffer = std::make_shared<GpuBuffer>(
            *frame.context, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemory::Shared);
        frame.linearPool->push_back(buffer);
    }
    const TimelinePoint signal = frame.cuda->NextVulkanSignal();
    frame.context->SubmitAsync(
        [&](VkCommandBuffer commands) { RecordCopy(commands, image, *buffer); }, {},
        std::span(&signal, 1));
    frame.cuda->WaitForVulkan(syncStream);
    mapped.tensor = frame.cuda->View(buffer, type, shape);
    return mapped;
}

void RenderVar::Unmap(std::uintptr_t stream) {
    if (frame_ == nullptr || frame_->arrayMaps == 0) {
        return;
    }
    frame_->cuda->SignalVulkan(stream);
    --frame_->arrayMaps;
}

StepOperation::StepOperation(const VulkanContext& context, std::uint64_t submission,
                             RenderProductSetOutputs outputs)
    : context_(&context), submission_(submission), outputs_(std::move(outputs)) {}

bool StepOperation::Wait(std::chrono::nanoseconds timeout) {
    const auto nanoseconds =
        timeout == std::chrono::nanoseconds::max()
            ? UINT64_MAX
            : static_cast<std::uint64_t>(std::max<std::int64_t>(timeout.count(), 0));
    return context_->WaitForSubmission(submission_, nanoseconds);
}

RenderProductSetOutputs StepOperation::Fetch() {
    Wait();
    return outputs_;
}

}  // namespace ernie
