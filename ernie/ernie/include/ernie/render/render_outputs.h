#pragma once

#include "ernie/gpu/gpu_image.h"
#include "ernie/interop/cuda_interop.h"
#include "ernie/interop/tensor.h"
#include "ernie/scene/path.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ernie {

/// Where `RenderVar::Map` delivers an output.
enum class MapDevice {
    Cpu,        ///< A host tensor; the texels are copied to the host.
    Cuda,       ///< A linear CUDA tensor; the GPU detiles the image into it, without the host.
    CudaArray,  ///< The rendered image itself as a CUDA array: no copy at all.
};

/// A mapped render var: a tensor for `Cpu` and `Cuda`, a CUDA array for `CudaArray`.
struct MappedRenderVar {
    MapDevice device = MapDevice::Cpu;
    Tensor tensor;
    CudaArrayView array;
};

/// The images a backend renders one frame of a render product into, by render var name: what
/// `RenderVar` reads, whatever the backend.
class FrameImages {
  public:
    virtual ~FrameImages() = default;

    virtual VkExtent2D Extent() const = 0;

    /// The image of render var `name`, or null if the frame has none.
    virtual GpuImage* Var(std::string_view name) = 0;
};

/// Internal state of one rendered frame of a render product.
struct ProductFrame;

/// One render var of one rendered frame, such as `LdrColor`, named after UsdRender's RenderVar.
///
/// Outputs stay valid while any `RenderVar` of the frame lives, and keep its image from being
/// rendered over: drop them (after unmapping) so later steps can reuse the frame. They must not
/// outlive the delegate that rendered them.
class RenderVar {
  public:
    RenderVar() = default;
    RenderVar(std::shared_ptr<ProductFrame> frame, Path path, std::string name);

    /// The RenderVar prim's path, or the product's path plus the var's name for default vars.
    const Path& GetPath() const {
        return path_;
    }

    /// The var's `sourceName`, such as `LdrColor`.
    const std::string& Name() const {
        return name_;
    }

    std::uint32_t Width() const;
    std::uint32_t Height() const;

    /// Shape of the mapped tensor: (rows, cols, channels), (rows, cols) for one channel, or
    /// (bands, rows, cols) for SpectralRadiance.
    std::vector<std::int64_t> Shape() const;

    /// Element type of the mapped tensor.
    DType Type() const;

    /// Maps the output to `device`. CUDA mappings make CUDA stream `syncStream` (DLPack
    /// numbering: 1 legacy default, 2 per-thread default) wait on the GPU for the render, without
    /// blocking the host; a CPU mapping waits on the host. `Cuda` and `Cpu` tensors own their
    /// texels; a `CudaArray` is the frame's image, which must be unmapped before the frame is
    /// dropped, and is not available for the layered SpectralRadiance.
    /// @throws std::invalid_argument for a CUDA device without CUDA interop, or a format CUDA
    /// arrays do not support.
    MappedRenderVar Map(MapDevice device, std::uintptr_t syncStream = 1);

    /// Ends a mapping. For a `CudaArray`, Vulkan waits for the work queued on CUDA stream `stream`
    /// before rendering into the image again.
    void Unmap(std::uintptr_t stream = 1);

    /// Whether the var is mapped as a CUDA array.
    bool IsMapped() const;

  private:
    /// The var's image in the frame.
    /// @throws std::invalid_argument for an empty var or one the frame lacks.
    GpuImage& Image() const;

    std::shared_ptr<ProductFrame> frame_;
    Path path_;
    std::string name_;
};

/// The outputs of one render product in a step: its render vars by name.
struct RenderProductOutput {
    Path product;
    std::map<std::string, RenderVar> vars;
};

/// Outputs of a step by render product path.
using RenderProductSetOutputs = std::map<Path, RenderProductOutput>;

/// A submitted step: poll or wait for its GPU work, then fetch its
/// outputs.
class StepOperation {
  public:
    StepOperation(const VulkanContext& context, std::uint64_t submission,
                  RenderProductSetOutputs outputs);

    /// Waits up to `timeout` (zero polls) for the step's GPU work; true once it is complete.
    bool Wait(std::chrono::nanoseconds timeout = std::chrono::nanoseconds::max());

    /// Whether the step's GPU work is complete, without waiting.
    bool Done() {
        return Wait(std::chrono::nanoseconds::zero());
    }

    /// The step's outputs, waiting for its GPU work first.
    RenderProductSetOutputs Fetch();

    /// The step's outputs without waiting: mapping them to CUDA orders the consumer after the
    /// step on the GPU, and mapping them to the CPU waits on the host.
    const RenderProductSetOutputs& Outputs() const {
        return outputs_;
    }

  private:
    const VulkanContext* context_;
    std::uint64_t submission_;
    RenderProductSetOutputs outputs_;
};

}  // namespace ernie
