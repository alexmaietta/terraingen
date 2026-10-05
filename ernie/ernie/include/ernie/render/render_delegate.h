#pragma once

#include "ernie/gpu/pipeline.h"
#include "ernie/gpu/vulkan_context.h"
#include "ernie/interop/cuda_interop.h"
#include "ernie/interop/tensor.h"
#include "ernie/material/brdf.h"
#include "ernie/render/frame_params.h"
#include "ernie/render/render_outputs.h"
#include "ernie/scene/scene.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ernie {

class RenderDelegate;

/// A Mesh's `points` or a PointInstancer's `positions`, `orientations` or `scales` bound for CUDA
/// to write in place, without copies: `Map` returns the renderer's own GPU array as a writable
/// float32 CUDA tensor (n, 3 or 4), and the next steps draw whatever was written there by
/// `Unmap`.
///
/// Bound arrays are double-buffered, so a simulation writes step N+1 while step N renders: `Map`
/// returns the array the next step reads, and makes CUDA stream `stream` wait on the GPU only for
/// the step before last, the last one that read it; `Unmap` makes the next step wait on the GPU
/// for the work queued on `stream` and read what was written. Neither blocks the host. Mapping
/// again before the next step returns the same array. The array holds what was written two steps
/// before (or the scene's data at first), so a writer fills all of it each step. The tensor is
/// valid until `Unmap`. Bound data persists until the scene's own attribute (or a mesh's
/// topology) changes, which takes over again. Bounds come from the prim's `extent` as of `Bind`;
/// without one they stay those of the last synced data.
class AttributeBinding {
  public:
    AttributeBinding() = default;

    const Path& GetPath() const {
        return path_;
    }

    const std::string& Attribute() const {
        return attribute_;
    }

    /// The prim's `extent` when it was bound, if it had one.
    const std::optional<Bounds3>& Extent() const {
        return extent_;
    }

    /// @throws std::invalid_argument if CUDA interop is unavailable or the prim is gone.
    Tensor Map(std::uintptr_t stream = 1);

    void Unmap(std::uintptr_t stream = 1);

    bool IsMapped() const {
        return mapped_;
    }

  private:
    friend class RenderDelegate;

    RenderDelegate* delegate_ = nullptr;
    Path path_;
    std::string attribute_;
    std::optional<Bounds3> extent_;
    bool mapped_ = false;
};

/// What a render backend is and does, so applications, settings panels and bindings can adapt.
struct RendererInfo {
    std::string name;  ///< The backend's name, as `CreateRenderDelegate` takes it.
    /// Whether consecutive steps of an unchanged scene refine one image, as a path tracer's samples
    /// accumulate, rather than each step rendering a complete image.
    bool progressive = false;
};

/// What a delegate holds, for statistics panels.
struct RenderStats {
    std::uint64_t gpuBytes = 0;   ///< GPU memory the delegate holds.
    std::uint64_t triangles = 0;  ///< Triangles drawn or traced by the last frame.
    /// Backend-specific counts by name, such as the rasterizer's terrain patches.
    std::vector<std::pair<std::string, std::uint64_t>> counters;
};

/// Where an image sits in the deterministic random streams of `sampling.h`.
struct FrameId {
    std::uint64_t frame = 0;
    std::uint32_t stream = 0;
};

/// A render backend for a `Scene`, after Hydra's HdRenderDelegate: the rasterizer, and in future
/// ray and path tracers, behind one interface that applications, the GUI viewer and the Python
/// bindings drive without knowing which backend renders.
///
/// The scene is the single source of truth. A delegate pulls its changes on `Sync` (each delegate
/// keeps its own `ChangeCursor`) and uploads only what changed, so there is no separate render
/// index. Every backend reads the scene with the same resolution rules (`resolve.h`) and writes
/// the same render vars through the camera's sensor filters (`sensor_filters.h`).
///
/// There are two ways to render:
/// - headless and in simulation: `StepAsync` (or `Step` and `Render`) renders the
///   UsdRender products of a scene and returns their render vars;
/// - interactively: `Update` then `Record` into the application's own command buffer each frame,
///   submitted after `SubmitWaits`.
///
/// Rendering is deterministic: an image's noise and samples derive from the camera's
/// `ernie:sensor:seed`, its render product's path and its frame number (`NextFrame`), so the same
/// scene renders the same images on any machine and whatever else renders alongside.
class RenderDelegate {
  public:
    virtual ~RenderDelegate() = default;
    RenderDelegate(const RenderDelegate&) = delete;
    RenderDelegate& operator=(const RenderDelegate&) = delete;

    virtual const RendererInfo& Info() const = 0;

    /// The reflectance models materials may name in `ernie:brdf`.
    virtual const BrdfRegistry& Brdfs() const = 0;

    /// Uploads what changed in `scene` since the last call, or everything when `scene` is not the
    /// scene of the last call. An upload that fails is not retried until its attributes change
    /// again.
    /// @throws std::invalid_argument naming the prim and attribute of data that cannot be
    /// uploaded.
    virtual void Sync(const Scene& scene) = 0;

    /// Syncs, then prepares to draw the scene as the RenderSettings prim `settings` describes into
    /// an image of `extent`, for `Record`.
    /// @throws std::invalid_argument as `Sync` and the scene's resolution (`resolve.h`).
    virtual void Update(const Scene& scene, const Path& settings, VkExtent2D extent) = 0;

    /// Semaphore waits the submission of commands from `Record` needs, for CUDA copies into the
    /// delegate's buffers that may still be running; empty without CUDA inputs.
    virtual std::vector<TimelinePoint> SubmitWaits() const = 0;

    /// Records the frame prepared by the last `Update` and draws its LdrColor into
    /// `target.color` (in the colour attachment layout, of the delegate's present format).
    virtual void Record(VkCommandBuffer commands, const RenderingTarget& target) = 0;

    /// Renders `products` of `scene` (see `ResolveRenderProducts`) and returns at once: wait for
    /// or poll the operation, then fetch the outputs and map their
    /// render vars. Each product renders into a frame of its own that later steps reuse once its
    /// outputs are dropped and unmapped.
    /// @throws std::invalid_argument as `ResolveRenderProducts` and `Update`.
    virtual StepOperation StepAsync(const Scene& scene, std::span<const Path> products = {},
                                    const Path& settings = {}) = 0;

    /// `StepAsync(...).Fetch()`.
    RenderProductSetOutputs Step(const Scene& scene, std::span<const Path> products = {},
                                 const Path& settings = {});

    /// Renders the first product of the settings and returns its `LdrColor`: a uint8 (rows, cols,
    /// 4) RGBA tensor, rows top to bottom, on the CPU, or with a CUDA `device` a linear CUDA
    /// tensor that the GPU fills without the host. A CUDA result returns without waiting for the
    /// render; CUDA work reading it must first wait with `CudaInterop::WaitForVulkan` (Python's
    /// `__dlpack__` does so on the consumer's stream; `CudaInterop::Download` does so itself). It
    /// stays valid while the tensor lives, but not beyond the delegate's Vulkan context.
    /// @throws std::invalid_argument as `StepAsync` and `RenderVar::Map`, or for a CUDA device
    /// other than the delegate's.
    Tensor Render(const Scene& scene, const Path& settings = {}, Device device = {});

    /// The CUDA interop of the delegate's device, connected on first use; null if unavailable,
    /// with the reason in `why` if given.
    virtual CudaInterop* Cuda(std::string* why = nullptr) = 0;

    /// Syncs `scene`, then binds `attribute` of the prim at `path` for CUDA to write in place:
    /// `points` of a Mesh, or `positions`, `orientations` or `scales` of a PointInstancer.
    /// @throws std::invalid_argument for other prims or attributes, a prim that failed to sync,
    /// or without CUDA interop.
    virtual AttributeBinding Bind(const Scene& scene, const Path& path,
                                  std::string_view attribute) = 0;

    /// The prim path of a PrimId render var value, or the empty path for 0 or an unknown id.
    virtual Path PrimPath(std::uint32_t id) const = 0;

    virtual RenderStats Stats() const = 0;

    /// World bounds of what the delegate draws as of its last `Sync` or `Update`, for framing
    /// cameras; none if nothing has finite bounds (an infinite ground plane does not count).
    virtual std::optional<Bounds3> SceneBounds() const = 0;

  protected:
    RenderDelegate() = default;

    /// The frame of the next image of `product` (the settings' path for the interactive view):
    /// the `ernie:frame` of the resolved RenderSettings prim `settings` when authored, else how
    /// many images of the product this delegate rendered before; and the product's stream,
    /// `StreamOf` its path.
    FrameId NextFrame(const Scene& scene, const Path& settings, const Path& product);

    /// A binding of `attribute` of `path` to this delegate.
    AttributeBinding MakeBinding(Path path, std::string attribute, std::optional<Bounds3> extent);

    /// `AttributeBinding::Map` and `Unmap` of a binding made by `MakeBinding`.
    virtual Tensor MapBinding(const AttributeBinding& binding, std::uintptr_t stream) = 0;
    virtual void UnmapBinding(const AttributeBinding& binding, std::uintptr_t stream) = 0;

  private:
    friend class AttributeBinding;

    std::map<Path, std::uint64_t> frameCounts_;
};

/// The backends `CreateRenderDelegate` knows: "raster".
std::vector<std::string> RenderBackends();

/// A delegate of `backend` on `context`, drawing `Record`'s LdrColor into colour attachments of
/// `presentFormat`, with the reflectance models in `brdfs`.
/// @throws std::invalid_argument for an unknown backend, or std::runtime_error if the backend
/// cannot start on `context`.
std::unique_ptr<RenderDelegate>
CreateRenderDelegate(std::string_view backend, const VulkanContext& context,
                     VkFormat presentFormat = VK_FORMAT_R8G8B8A8_UNORM,
                     BrdfRegistry brdfs = BrdfRegistry::WithBuiltins());

}  // namespace ernie
