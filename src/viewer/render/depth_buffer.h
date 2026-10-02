#pragma once

#include <webgpu/webgpu.h>

#include <cstdint>

namespace terraingen::render {

/// A depth texture sized to a render target, recreated when the target size changes.
class DepthBuffer {
  public:
    explicit DepthBuffer(WGPUDevice device,
                         WGPUTextureFormat format = WGPUTextureFormat_Depth32Float);
    ~DepthBuffer();
    DepthBuffer(const DepthBuffer&) = delete;
    DepthBuffer& operator=(const DepthBuffer&) = delete;

    /// Ensures the texture is `width` × `height` pixels, recreating it if the size changed.
    void Resize(std::uint32_t width, std::uint32_t height);

    /// View of the current texture, or null before the first `Resize`.
    WGPUTextureView View() const {
        return view_;
    }

    WGPUTextureFormat Format() const {
        return format_;
    }

    std::uint32_t Width() const {
        return width_;
    }

    std::uint32_t Height() const {
        return height_;
    }

  private:
    void Release();

    WGPUDevice device_;
    WGPUTextureFormat format_;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView view_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

}  // namespace terraingen::render
