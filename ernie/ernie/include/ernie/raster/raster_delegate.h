#pragma once

#include "ernie/core/image.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/interop/tensor.h"
#include "ernie/raster/terrain_renderer.h"
#include "ernie/scene/scene.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace ernie {

/// The prims a render of `scene` uses, resolved as `RasterDelegate` does:
/// - `settings`: the given RenderSettings prim, or the first one when the given path is empty;
///   empty if there is none, which leaves every option at its default.
/// - `camera`: the settings' `camera` relationship, or else the first Camera.
/// - `terrain`: the first visible Terrain.
/// - `material`: the terrain's `material:binding` when it names a Material.
/// - `light`: the first visible DistantLight; without one Ernie lights the scene with a default
///   sun overhead, as Hydra's Storm adds a default light to unlit scenes.
struct RasterPrims {
    Path settings;
    Path camera;
    Path terrain;
    Path material;
    Path light;
};

/// Resolves the prims a render of `scene` with the RenderSettings prim `settings` uses.
/// @throws std::invalid_argument if `settings` is not empty and not a RenderSettings prim.
RasterPrims ResolveRasterPrims(const Scene& scene, const Path& settings);

/// The terrain renderer's per-frame parameters for `prims` of `scene`, for an image with aspect
/// ratio `aspect`:
/// - camera from the Camera (`CameraViewOf`);
/// - sun direction from the light's transform (`TowardsLight`), and the surface exposure from its
///   `inputs:intensity` times 2^`inputs:exposure`, divided by 2^ the camera's `exposure`;
///   shadows from its `inputs:shadow:enable`;
/// - vertical exaggeration from the terrain's `ernie:exaggeration`;
/// - BRDF, its `inputs:<parameter>` values, albedo and detail options from the material;
/// - ambient, colour mode, wireframe, shadow and LOD options from the render settings.
///
/// The light's colour is not used yet.
/// @throws std::invalid_argument if there is no camera, the camera is invalid (`CameraViewOf`), or
/// the material names a BRDF that is not in `brdfs`.
TerrainDrawParams DrawParamsOf(const Scene& scene, const RasterPrims& prims,
                               const BrdfRegistry& brdfs, float aspect);

/// A detail texture tensor as an `Image`: float32 values are linear; uint8 values are sRGB-encoded
/// when `encoding` is `Srgb` and linear otherwise. Grey replicates into rgb and a missing alpha
/// reads as 1.
/// @throws std::invalid_argument for a tensor that is not (rows, cols) or (rows, cols, 1, 3 or 4)
/// float32 or uint8 on the CPU.
Image ImageOfTensor(const Tensor& tensor, ImageEncoding encoding);

/// Ernie's rasterizer as a render delegate for a `Scene`: syncs the scene's terrain, material,
/// light, camera and render settings into a `TerrainRenderer` and renders them.
///
/// `Sync` re-uploads GPU data only when the attributes behind it changed
/// (`Scene::AttributeVersion`), so editing display options or moving the camera costs nothing
/// beyond the per-frame update. Interactive applications call `Update` and `Record` into their own
/// command buffer each frame; headless ones call `Render`, which returns the `LdrColor` render var
/// as a tensor.
class RasterDelegate {
  public:
    /// @throws std::runtime_error if the renderer's pipelines cannot be created.
    explicit RasterDelegate(const VulkanContext& context,
                            VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
                            VkFormat depthFormat = VK_FORMAT_D32_SFLOAT,
                            BrdfRegistry brdfs = BrdfRegistry::WithBuiltins());

    /// Uploads the heightfield, albedo map and detail textures that changed since the last call.
    /// An upload that fails is not retried until its attributes change again.
    /// @throws std::invalid_argument naming the prim and attribute of a tensor that cannot be
    /// uploaded.
    void Sync(const Scene& scene);

    /// Syncs, then prepares the renderer to draw the scene as `settings` describes into an image
    /// of `extent`. Call only while no submitted frame is still reading the renderer.
    /// @throws std::invalid_argument as `Sync`, `ResolveRasterPrims` and `DrawParamsOf`.
    void Update(const Scene& scene, const Path& settings, VkExtent2D extent);

    /// Records the frame prepared by the last `Update` into `target`, clearing to the render
    /// settings' `ernie:clearColor` unless `target.loadColor` is set.
    void Record(VkCommandBuffer commands, RenderingTarget target);

    /// Renders the scene at the settings' `resolution` (default 2048 x 1080, as UsdRender) and
    /// returns its `LdrColor` render var: a uint8 (rows, cols, 4) RGBA tensor, rows top to bottom.
    /// @throws std::invalid_argument as `Update`, or if the colour format is not 8-bit RGBA or
    /// BGRA.
    Tensor Render(const Scene& scene, const Path& settings = {});

    /// The prims the last `Update` drew.
    const RasterPrims& Prims() const {
        return prims_;
    }

    TerrainRenderer& Renderer() {
        return renderer_;
    }

    const TerrainRenderer& Renderer() const {
        return renderer_;
    }

  private:
    /// Path and attribute versions behind an upload.
    using Key = std::pair<Path, std::array<std::uint64_t, 4>>;

    void SyncHeightfield(const Scene& scene, const Path& terrain);
    void SyncAlbedoMap(const Scene& scene, const Path& material);
    void SyncDetail(const Scene& scene, const Path& material, bool normal);

    const VulkanContext& context_;
    TerrainRenderer renderer_;
    VkFormat colorFormat_;
    VkFormat depthFormat_;
    std::unique_ptr<OffscreenTarget> offscreen_;
    ChangeCursor cursor_;
    bool synced_ = false;
    RasterPrims prims_;
    glm::vec3 clearColor_{0.0f};
    std::optional<Key> heightfield_;
    std::optional<Key> albedoMap_;
    std::optional<Key> detailAlbedo_;
    std::optional<Key> detailNormal_;
};

}  // namespace ernie
