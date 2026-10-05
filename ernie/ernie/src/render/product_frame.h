#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/interop/cuda_interop.h"
#include "ernie/render/render_outputs.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ernie {

/// A rendered frame of a render product: its render targets and how to reach them.
struct ProductFrame {
    const VulkanContext* context = nullptr;
    CudaInterop* cuda = nullptr;          ///< Null without CUDA interop.
    std::unique_ptr<FrameImages> images;  ///< What the backend rendered the frame into.
    std::uint64_t submission = 0;         ///< The `SubmitAsync` submission that rendered it.
    int arrayMaps = 0;                    ///< Outstanding `MapDevice::CudaArray` mappings.
    /// Linear CUDA outputs, owned by the delegate; a buffer is reused once no tensor holds it.
    std::vector<std::shared_ptr<GpuBuffer>>* linearPool = nullptr;
};

}  // namespace ernie
