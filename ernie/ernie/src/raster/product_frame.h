#pragma once

#include "ernie/gpu/gpu_buffer.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/interop/cuda_interop.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace ernie {

/// A rendered frame of a render product: its target and how to reach it.
struct ProductFrame {
    const VulkanContext* context = nullptr;
    CudaInterop* cuda = nullptr;  ///< Null without CUDA interop.
    std::unique_ptr<OffscreenTarget> target;
    std::uint64_t submission = 0;  ///< The `SubmitAsync` submission that rendered it.
    bool bgra = false;             ///< The colour format stores blue first.
    int arrayMaps = 0;             ///< Outstanding `MapDevice::CudaArray` mappings.
    /// Linear CUDA outputs, owned by the delegate; a buffer is reused once no tensor holds it.
    std::vector<std::shared_ptr<GpuBuffer>>* linearPool = nullptr;
};

}  // namespace ernie
