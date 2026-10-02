#include "render/depth_buffer.h"

namespace terraingen::render {

DepthBuffer::DepthBuffer(WGPUDevice device, WGPUTextureFormat format)
    : device_(device), format_(format) {}

DepthBuffer::~DepthBuffer() {
    Release();
}

void DepthBuffer::Resize(std::uint32_t width, std::uint32_t height) {
    if (view_ != nullptr && width == width_ && height == height_) {
        return;
    }
    Release();
    WGPUTextureDescriptor descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    descriptor.usage = WGPUTextureUsage_RenderAttachment;
    descriptor.dimension = WGPUTextureDimension_2D;
    descriptor.size = {width, height, 1};
    descriptor.format = format_;
    texture_ = wgpuDeviceCreateTexture(device_, &descriptor);
    view_ = wgpuTextureCreateView(texture_, nullptr);
    width_ = width;
    height_ = height;
}

void DepthBuffer::Release() {
    if (view_ != nullptr) {
        wgpuTextureViewRelease(view_);
        view_ = nullptr;
    }
    if (texture_ != nullptr) {
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }
}

}  // namespace terraingen::render
