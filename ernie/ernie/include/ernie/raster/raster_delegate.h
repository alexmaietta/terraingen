#pragma once

#include "ernie/core/image.h"
#include "ernie/geometry/primitives.h"
#include "ernie/gpu/offscreen_target.h"
#include "ernie/interop/cuda_interop.h"
#include "ernie/interop/tensor.h"
#include "ernie/raster/ground_part.h"
#include "ernie/raster/mesh_part.h"
#include "ernie/raster/raster_renderer.h"
#include "ernie/raster/render_outputs.h"
#include "ernie/raster/terrain_part.h"
#include "ernie/scene/scene.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace ernie {

/// The prims a render of `scene` uses, resolved as `RasterDelegate` does:
/// - `settings`: the given RenderSettings prim, or the first one when the given path is empty;
///   empty if there is none, which leaves every option at its default.
/// - `camera`: the settings' `camera` relationship, or else the first Camera.
/// - `terrain`: the first visible Terrain.
/// - `material`: the terrain's `material:binding` when it names a Material.
/// - `light`: the first visible DistantLight, the sun, with cascaded shadows; without any light
///   Ernie lights the scene with a default sun overhead, as Hydra's Storm adds a default light to
///   unlit scenes.
/// - `lights`: every other visible DistantLight, SphereLight, RectLight and DiskLight, sorted by
///   path.
/// - `dome`: the first visible DomeLight.
/// - `ground`: the first visible GroundPlane.
struct RasterPrims {
    Path settings;
    Path camera;
    Path terrain;
    Path material;
    Path light;
    std::vector<Path> lights;
    Path dome;
    Path ground;
};

/// A render product to draw, resolved from UsdRender prims.
struct RenderProductSpec {
    Path product;  ///< The RenderProduct prim, or the settings' path for the default product.
    Path camera;   ///< Empty to use the settings' camera.
    VkExtent2D resolution{2048, 1080};
    /// Render vars as (path, sourceName).
    std::vector<std::pair<Path, std::string>> vars;
    /// The product's sensor filter chain; empty for `DefaultFilterChain`.
    std::vector<SensorFilter> filters;
};

/// The render products a step of `scene` draws, following UsdRender: the given RenderProduct
/// prims, or else the RenderSettings' `products`, or else one default product keyed by the
/// settings' path (the root without settings). Each product takes its `camera` and `resolution`
/// from its prim, falling back to the settings' and then 2048 x 1080; its vars are its
/// `orderedVars` RenderVar prims by `sourceName` (any of `RenderTargets::VarNames`), or `LdrColor`
/// alone; its sensor filters are the SensorFilter prims of its `ernie:filters`, by
/// `ernie:filter:type`.
/// @throws std::invalid_argument for a path that is not a RenderProduct, a var that is not a
/// RenderVar, a filter that is not a SensorFilter or of an unknown type, an invalid filter chain
/// (`CheckFilterChain`), or a var the rasterizer does not render or the chain does not write.
std::vector<RenderProductSpec>
ResolveRenderProducts(const Scene& scene, std::span<const Path> products, const Path& settings);

/// Resolves the prims a render of `scene` with the RenderSettings prim `settings` uses.
/// @throws std::invalid_argument if `settings` is not empty and not a RenderSettings prim.
RasterPrims ResolveRasterPrims(const Scene& scene, const Path& settings);

/// The Material prim bound to `prim` by `material:binding`, or the empty path.
Path BoundMaterial(const Scene& scene, const Path& prim);

/// The sun's spectral irradiance at normal incidence per band of `bands`, in W m^-2 nm^-1, from the
/// DistantLight at `light`:
/// - physical, when it has `ernie:irradiance` or `ernie:spectrum`: the spectrum binned (scaled to
///   the irradiance when both are set), or else a black body at the enabled colour temperature or
///   the Sun's 5772 K carrying the irradiance (default 1361 W/m^2, the solar constant);
/// - otherwise relative, following UsdLux: `inputs:intensity` times 2^`inputs:exposure` is the
///   luminance (cd/m^2) of a white Lambertian surface facing the light, with the spectrum's shape
///   from `inputs:color` (upsampled with `RgbToSigmoid`) times a black body at the colour
///   temperature when `inputs:enableColorTemperature` is set.
/// @throws std::invalid_argument for invalid bands or spectrum pairs.
std::vector<float> SunIrradianceOf(const Scene& scene, const Path& light,
                                   const SpectralBands& bands);

/// The DistantLight or SphereLight at `path` as a `Light` for `bands`:
/// - distant: towards the light along its +z axis, its spectrum as `SunIrradianceOf`;
/// - sphere: at its world position with its cone (`inputs:shaping:cone:angle` and `:softness`,
///   default 180 degrees: unshaped) around its -z axis, radius `inputs:radius` (default 0.5; 0 with
///   `treatAsPoint`). Physical with `ernie:power` or `ernie:spectrum` (`tokens::kLightPower`);
///   otherwise relative, following UsdLux: `inputs:intensity` times 2^`inputs:exposure` is the
///   luminance (cd/m^2) of its surface, so its luminous intensity is that times pi r^2 (times 1
///   for a point), with the shape of `inputs:color` and the enabled colour temperature.
/// - rect and disk: centred at its world position, emitting along its -z axis from a face of
///   `inputs:width` x `inputs:height` (default 1 x 1) or radius `inputs:radius` (default 0.5),
///   scaled by its transform. Physical with `ernie:power` or `ernie:spectrum` as a sphere;
///   otherwise relative: `inputs:intensity` times 2^`inputs:exposure` is its luminance (cd/m^2).
/// Shadows follow `inputs:shadow:enable`.
/// @throws std::invalid_argument for another prim type, a negative radius or power, an area light
/// without area, a degenerate transform, invalid bands or spectrum pairs.
Light LightOf(const Scene& scene, const Path& path, const SpectralBands& bands);

/// Diffuse irradiance over pi of a latitude-longitude RGB map (`tokens::kLightTexture` layout)
/// turned by `rotation` into the world, as order-2 spherical harmonics (Ramamoorthi and Hanrahan
/// 2001): the map's projection weighted by each texel's exact solid angle, convolved with the
/// clamped cosine.
/// @throws std::invalid_argument unless `latlong` is a float32 (rows, cols, 3 or 4) CPU array.
std::array<glm::vec3, 9> DomeIrradianceSh(const Tensor& latlong, const glm::dmat3& rotation);

/// The DomeLight at `path` for `bands`: radiance `ernie:spectrum` (W m^-2 sr^-1 nm^-1), or in
/// relative units `inputs:intensity` times 2^`inputs:exposure` as luminance (cd/m^2) with the
/// shape of `inputs:color` and the enabled colour temperature; its map `ernie:texture` (uniform
/// without one) rotated by its transform.
/// @throws std::invalid_argument for another prim type, an invalid map, bands or spectrum pairs.
DomeLight DomeOf(const Scene& scene, const Path& path, const SpectralBands& bands);

/// How much wider than the sensor's the raster image's field of view must be for every sensor
/// pixel's undistorted ray to land in it: 1 without distortion, else the largest ratio along the
/// sensor's border, plus one percent.
double OverscanOf(const Camera& camera);

/// The frame parameters for `prims` of `scene`, for an image of `width` x `height`:
/// - the camera from the Camera prim (`CameraOf`) and the overscan its lens distortion needs
///   (`OverscanOf`);
/// - sun direction from the light's transform (`TowardsLight`), its spectral irradiance
///   (`SunIrradianceOf`), angular diameter (`inputs:angle`, default 0.53 degrees) and shadows from
///   its `inputs:shadow:enable`; without any light, a white sun overhead in relative units; with
///   only other lights or a dome, no sun;
/// - the other lights (`LightOf`) and the dome (`DomeOf`);
/// - ambient, wireframe, clear colour (linear) and shadow options from the render settings.
/// @throws std::invalid_argument if there is no camera, it is not perspective, or it or the light
/// are invalid.
FrameParams FrameParamsOf(const Scene& scene, const RasterPrims& prims, std::uint32_t width,
                          std::uint32_t height);

/// The ground plane's options from the GroundPlane prim at `ground`: `ernie:height`, and its
/// checkerboard from `ernie:checker`, `ernie:checker:size` and `ernie:checker:color0` and `color1`.
/// The material and prim id are left at their defaults.
/// @throws std::invalid_argument for a checker size that is not positive and finite.
GroundParams GroundParamsOf(const Scene& scene, const Path& ground);

/// The terrain's options: vertical exaggeration from the terrain's `ernie:exaggeration`, and
/// colour mode and LOD options from the render settings. The material is left at the default.
TerrainParams TerrainParamsOf(const Scene& scene, const RasterPrims& prims);

/// The settings of the Material prim at `material` (empty for defaults): BRDF from `ernie:brdf`,
/// its parameters from `inputs:<parameter>`, albedo and detail options.
/// @throws std::invalid_argument if the material names a BRDF that is not in `brdfs`.
MaterialSettings MaterialSettingsOf(const Scene& scene, const Path& material,
                                    const BrdfRegistry& brdfs);

/// The geometry of the Mesh prim at `mesh`: `points`, `faceVertexCounts` and `faceVertexIndices`
/// (triangulated as fans), per-vertex `normals`, and per-vertex `primvars:displayColor` when it
/// has one colour per point. Points, normals and colours may be float32 or float64, shaped
/// (n, 3) or flat; indices int32, int64 or uint32. Normals and colours of other sizes (such as
/// face-varying ones) are ignored. Points on a CUDA device come back as zeros of the right
/// number, for the caller to copy device to device.
/// @throws std::invalid_argument naming the attribute for arrays of the wrong type, size or
/// device, or invalid topology.
/// For a UsdGeom primitive (Cube, Sphere, Cylinder, Cone, Capsule, Plane) the generated mesh,
/// `PrimitiveMeshOf`.
MeshData MeshDataOf(const Scene& scene, const Path& mesh);

/// Whether prims of `type` draw as meshes: Mesh and the UsdGeom primitives.
bool IsMeshType(std::string_view type);

/// The mesh of the UsdGeom primitive at `path` from its attributes and their UsdGeom defaults:
/// Cube `size` (2); Sphere `radius` (1); Cylinder and Cone `radius` (1), `height` (2) and `axis`
/// ("Z"); Capsule `radius` (0.5), `height` (1) and `axis`; Plane `width` (2), `length` (2) and
/// `axis`. See `BoxMesh` and the other generators.
/// @throws std::invalid_argument for another prim type, an unknown axis or invalid sizes.
MeshData PrimitiveMeshOf(const Scene& scene, const Path& path);

/// The per-instance arrays of the PointInstancer prim at `instancer`: `positions`, and optional
/// `orientations` (quaternions as x, y, z, w, imaginary part first as in USD's memory layout) and
/// `scales`.
/// @throws std::invalid_argument naming the attribute for arrays of the wrong type or size.
InstanceData InstanceDataOf(const Scene& scene, const Path& instancer);

/// The instances of each prototype of the PointInstancer at `instancer`: entry k lists the
/// instances whose `protoIndices` entry is k, for `prototypes` prototypes. Without
/// `protoIndices` every instance uses prototype 0; instances with an index out of range are
/// dropped.
/// @throws std::invalid_argument if `protoIndices` is not an integer array.
std::vector<std::vector<std::uint32_t>>
InstancesByPrototype(const Scene& scene, const Path& instancer, std::size_t prototypes);

/// The prim's `extent`: six numbers, low then high corner, as a float array value or a CPU array;
/// none if unset or malformed.
std::optional<Bounds3> ExtentOf(const Scene& scene, const Path& prim);

/// A detail texture tensor as an `Image`: float32 values are linear; uint8 values are sRGB-encoded
/// when `encoding` is `Srgb` and linear otherwise. Grey replicates into rgb and a missing alpha
/// reads as 1.
/// @throws std::invalid_argument for a tensor that is not (rows, cols) or (rows, cols, 1, 3 or 4)
/// float32 or uint8 on the CPU.
Image ImageOfTensor(const Tensor& tensor, ImageEncoding encoding);

class RasterDelegate;

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
/// topology) changes, which takes over again. Bounds for shadows come from the prim's `extent` as
/// of `Bind`; without one they stay those of the last synced data.
class AttributeBinding {
  public:
    AttributeBinding() = default;

    const Path& GetPath() const {
        return path_;
    }

    const std::string& Attribute() const {
        return attribute_;
    }

    /// @throws std::invalid_argument if CUDA interop is unavailable or the prim is gone.
    Tensor Map(std::uintptr_t stream = 1);

    void Unmap(std::uintptr_t stream = 1);

    bool IsMapped() const {
        return mapped_;
    }

  private:
    friend class RasterDelegate;

    RasterDelegate* delegate_ = nullptr;
    Path path_;
    std::string attribute_;
    std::optional<Bounds3> extent_;
    bool mapped_ = false;
};

/// Ernie's rasterizer as a render delegate for a `Scene`: syncs Material prims into the
/// renderer's material table, the first visible Terrain into a `TerrainPart`, and Mesh and
/// PointInstancer prims into a `MeshPart`, and renders them with the scene's camera, light and
/// render settings.
///
/// `Sync` re-uploads GPU data only when the attributes behind it changed
/// (`Scene::AttributeVersion`): a deforming mesh rewrites only its points, and moving instances
/// rewrites only the instance arrays. Arrays on a CUDA device (mesh `points`, instance
/// `positions`, `orientations` and `scales`, contiguous float32) are copied device to device
/// through `CudaInterop`, connected on first use; author `extent` on such prims to spare a copy
/// to the host for bounds. Meshes that are prototypes of a PointInstancer are drawn
/// only through it; prototypes must be Mesh prims. A mesh without a bound material uses the
/// default material tinted by its `primvars:displayColor` (0.5 grey without one). Interactive
/// applications call `Update` and `Record` into their own command buffer each frame; headless ones
/// call `Render`, which returns the `LdrColor` render var as a tensor.
class RasterDelegate {
  public:
    /// @throws std::runtime_error if the renderer's pipelines cannot be created.
    /// `presentFormat` is the colour format `Record` draws LdrColor into.
    explicit RasterDelegate(const VulkanContext& context,
                            VkFormat presentFormat = VK_FORMAT_R8G8B8A8_UNORM,
                            BrdfRegistry brdfs = BrdfRegistry::WithBuiltins());
    ~RasterDelegate();
    RasterDelegate(const RasterDelegate&) = delete;
    RasterDelegate& operator=(const RasterDelegate&) = delete;

    /// Uploads the materials, terrain, meshes and instances that changed since the last call, or
    /// all of them when `scene` is not the scene of the last call. An upload that fails is not
    /// retried until its attributes change again.
    /// @throws std::invalid_argument naming the prim and attribute of data that cannot be
    /// uploaded.
    void Sync(const Scene& scene);

    /// Syncs, then prepares the renderer to draw the scene as `settings` describes into an image
    /// of `extent`. Waits for earlier `StepAsync` work, then needs no submitted frame to still be
    /// reading the renderer.
    /// @throws std::invalid_argument as `Sync`, `ResolveRasterPrims` and `FrameParamsOf`.
    void Update(const Scene& scene, const Path& settings, VkExtent2D extent);

    /// Semaphore waits the submission of commands from `Record` needs, for CUDA copies into the
    /// renderer's buffers that may still be running; empty without CUDA inputs.
    std::vector<TimelinePoint> SubmitWaits() const;

    /// Records the frame prepared by the last `Update` into render targets of the delegate's own
    /// and draws its LdrColor into `target.color` (in the colour attachment layout, of the present
    /// format). Recreating those targets for a new size or band count waits for the device.
    void Record(VkCommandBuffer commands, const RenderingTarget& target);

    /// The prim path of a PrimId render var value, or the empty path for 0 or an unknown id.
    Path PrimPath(std::uint32_t id) const;

    /// Renders `products` of `scene` (see `ResolveRenderProducts`) and returns at once, as
    /// ovrtx's `step_async`: wait for or poll the operation, then fetch the outputs and map their
    /// render vars. Products render one after another, each into a frame of its own that later
    /// steps reuse once its outputs are dropped and unmapped. GPU work is ordered after CUDA
    /// copies into the renderer's buffers, and signals CUDA when done.
    /// @throws std::invalid_argument as `ResolveRenderProducts` and `Update`.
    StepOperation StepAsync(const Scene& scene, std::span<const Path> products = {},
                            const Path& settings = {});

    /// `StepAsync(...).Fetch()`, as ovrtx's `step`.
    RenderProductSetOutputs Step(const Scene& scene, std::span<const Path> products = {},
                                 const Path& settings = {});

    /// Renders the first product of the settings and returns its `LdrColor`: a uint8 (rows, cols,
    /// 4) RGBA tensor, rows top to bottom, on the CPU, or with a CUDA `device` a linear CUDA
    /// tensor that the GPU fills without the host. A CUDA result returns without waiting for the
    /// render; CUDA work reading it must first wait with `CudaInterop::WaitForVulkan` (Python's
    /// `__dlpack__` does so on the consumer's stream; `CudaInterop::Download` does so itself). It
    /// stays valid while the tensor lives, but not beyond the delegate's Vulkan context.
    /// @throws std::invalid_argument as `StepAsync` and `RenderVar::Map`, or for a CUDA device
    /// other than the renderer's.
    Tensor Render(const Scene& scene, const Path& settings = {}, Device device = {});

    /// The CUDA interop of the delegate's device, connected on first use; null if unavailable,
    /// with the reason in `why` if given.
    CudaInterop* Cuda(std::string* why = nullptr);

    /// Syncs `scene`, then binds `attribute` of the prim at `path` for CUDA to write in place:
    /// `points` of a Mesh, or `positions`, `orientations` or `scales` of a PointInstancer.
    /// @throws std::invalid_argument for other prims or attributes, a prim that failed to sync,
    /// or without CUDA interop.
    AttributeBinding Bind(const Scene& scene, const Path& path, std::string_view attribute);

    /// The prims the last `Update` drew.
    const RasterPrims& Prims() const {
        return prims_;
    }

    RasterRenderer& Renderer() {
        return renderer_;
    }

    const RasterRenderer& Renderer() const {
        return renderer_;
    }

    const TerrainPart& Terrain() const {
        return terrain_;
    }

    const GroundPart& Ground() const {
        return ground_;
    }

    const MeshPart& Meshes() const {
        return meshes_;
    }

    /// The renderer's material id for the Material prim at `path`, if it is synced.
    std::optional<std::uint32_t> MaterialId(const Path& path) const;

  private:
    /// Scene, path and attribute versions behind an upload.
    using Key = std::pair<Path, std::array<std::uint64_t, 12>>;

    struct SyncedMaterial {
        std::uint32_t id = 0;
        std::optional<Key> albedoMap;
        std::optional<Key> detailAlbedo;
        std::optional<Key> detailNormal;
    };

    struct SyncedMesh {
        std::uint32_t handle = 0;
        Key key;
        std::uint64_t points = 0;
        bool hasColors = false;
        glm::vec3 tint{0.5f};
    };

    struct SyncedInstancer {
        std::uint32_t handle = 0;
        Key key;
    };

    Key KeyOf(const Scene& scene, const Path& path,
              std::initializer_list<std::string_view> attributes) const;
    void SyncMaterials(const Scene& scene);
    void SyncMaterial(const Scene& scene, const Path& path, SyncedMaterial& material);
    void SyncHeightfield(const Scene& scene, const Path& terrain);
    void SyncMeshes(const Scene& scene);
    void SyncInstancers(const Scene& scene);
    std::uint32_t MaterialOf(const Scene& scene, const Path& prim) const;
    std::vector<MeshItem> Items(const Scene& scene) const;

    CudaInterop& RequireCuda();
    friend class AttributeBinding;
    Tensor MapBinding(const AttributeBinding& binding, std::uintptr_t stream);
    void UnmapBinding(const AttributeBinding& binding, std::uintptr_t stream);
    std::uint32_t BoundHandle(const AttributeBinding& binding) const;
    void UpdatePrims(const Scene& scene, const RasterPrims& prims, VkExtent2D extent,
                     std::span<const SensorFilter> filters = {});
    std::shared_ptr<ProductFrame> AcquireFrame(const Path& product, VkExtent2D extent, int bands);
    std::uint32_t PrimIdOf(const Path& path) const;
    void SetMeshPoints(const Scene& scene, const Path& path, std::uint32_t handle);

    const VulkanContext& context_;
    std::unique_ptr<CudaInterop> cuda_;
    bool cudaTried_ = false;
    std::string cudaWhy_;
    bool meshesOnCuda_ = false;
    RasterRenderer renderer_;
    TerrainPart terrain_;
    MeshPart meshes_;
    GroundPart ground_;
    std::unique_ptr<RenderTargets> viewTargets_;
    mutable std::map<Path, std::uint32_t> primIds_;
    mutable std::vector<Path> primPaths_;
    std::map<Path, std::vector<std::shared_ptr<ProductFrame>>> frames_;
    std::vector<std::shared_ptr<GpuBuffer>> linearPool_;
    ChangeCursor cursor_;
    std::uint64_t sceneId_ = 0;
    RasterPrims prims_;
    std::optional<Key> heightfield_;
    std::map<Path, SyncedMaterial> materials_;
    std::map<Path, SyncedMesh> syncedMeshes_;
    std::map<Path, SyncedInstancer> instancers_;
};

}  // namespace ernie
