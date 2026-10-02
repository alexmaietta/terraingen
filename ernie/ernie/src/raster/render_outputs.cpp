#include "ernie/raster/render_outputs.h"

#include "product_frame.h"

#include <stdexcept>
#include <utility>

namespace ernie {

RenderVar::RenderVar(std::shared_ptr<ProductFrame> frame, Path path, std::string name)
    : frame_(std::move(frame)), path_(std::move(path)), name_(std::move(name)) {}

std::uint32_t RenderVar::Width() const {
    return frame_->target->Extent().width;
}

std::uint32_t RenderVar::Height() const {
    return frame_->target->Extent().height;
}

bool RenderVar::IsMapped() const {
    return frame_ != nullptr && frame_->arrayMaps > 0;
}

MappedRenderVar RenderVar::Map(MapDevice device, std::uintptr_t syncStream) {
    if (frame_ == nullptr) {
        throw std::invalid_argument("RenderVar::Map on an empty render var");
    }
    ProductFrame& frame = *frame_;
    const std::vector<std::int64_t> shape{static_cast<std::int64_t>(Height()),
                                          static_cast<std::int64_t>(Width()), 4};
    MappedRenderVar mapped;
    mapped.device = device;
    if (device == MapDevice::Cpu) {
        frame.context->WaitForSubmission(frame.submission);
        std::vector<std::uint8_t> bytes = frame.target->ReadColor();
        if (frame.bgra) {
            for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) {
                std::swap(bytes[i], bytes[i + 2]);
            }
        }
        auto owner = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
        mapped.tensor = Tensor(owner->data(), DType::UInt8, shape, {}, {}, owner);
        return mapped;
    }
    if (frame.cuda == nullptr) {
        throw std::invalid_argument("Mapping a render var to CUDA needs CUDA interop");
    }
    if (frame.bgra) {
        throw std::invalid_argument("CUDA mappings need an RGBA colour format");
    }
    if (device == MapDevice::CudaArray) {
        mapped.array = frame.cuda->MapImage(frame.target->Color());
        frame.cuda->WaitForVulkan(syncStream);
        ++frame.arrayMaps;
        return mapped;
    }
    std::shared_ptr<GpuBuffer> buffer;
    for (const std::shared_ptr<GpuBuffer>& candidate : *frame.linearPool) {
        if (candidate.use_count() == 1 && candidate->Size() >= frame.target->ColorBytes()) {
            buffer = candidate;
            break;
        }
    }
    if (buffer == nullptr) {
        buffer =
            std::make_shared<GpuBuffer>(*frame.context, frame.target->ColorBytes(),
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemory::Shared);
        frame.linearPool->push_back(buffer);
    }
    const TimelinePoint signal = frame.cuda->NextVulkanSignal();
    frame.context->SubmitAsync(
        [&](VkCommandBuffer commands) { frame.target->RecordCopyColor(commands, *buffer); }, {},
        std::span(&signal, 1));
    frame.cuda->WaitForVulkan(syncStream);
    mapped.tensor = frame.cuda->View(buffer, DType::UInt8, shape);
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
