#pragma once

#include "ernie/camera/camera.h"
#include "ernie/camera/sensor_filters.h"
#include "ernie/core/image.h"
#include "ernie/core/spectrum.h"
#include "ernie/geometry/mesh_data.h"
#include "ernie/gpu/vulkan_context.h"
#include "ernie/interop/tensor.h"
#include "ernie/material/brdf.h"
#include "ernie/material/material_settings.h"
#include "ernie/render/frame_params.h"
#include "ernie/scene/scene.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// How every render delegate reads a `Scene`: which prims a render uses, its render products, and
/// the camera, lights, materials, meshes and instances as plain data, so backends differ only in
/// how they draw.
namespace ernie {

/// The prims a render of `scene` uses, as every render delegate resolves them:
/// - `settings`: the given RenderSettings prim, or the first one outside `tokens::kSessionRoot`
///   when the given path is empty; empty if there is none, which leaves every option at its
///   default.
/// - `camera`: the settings' `camera` relationship, or else the first Camera outside the session
///   root.
/// - `terrain`: the first visible Terrain.
/// - `material`: the terrain's `material:binding` when it names a Material.
/// - the lights, from the settings' `ernie:lights` relationship when it is set (its visible light
///   prims), or else from every visible light outside the session root: `light` is the first
///   DistantLight by path, the sun, with cascaded shadows; `lights` every other DistantLight,
///   SphereLight, RectLight and DiskLight, sorted by path; `dome` the first DomeLight. A scene
///   without lights renders black.
/// - `ground`: the first visible GroundPlane.
struct RenderPrims {
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
/// `orderedVars` RenderVar prims by `sourceName` (any of `kRenderVarNames`), or `LdrColor`
/// alone; its sensor filters are the SensorFilter prims of its `ernie:filters`, by
/// `ernie:filter:type`.
/// @throws std::invalid_argument for a path that is not a RenderProduct, a var that is not a
/// RenderVar, a filter that is not a SensorFilter or of an unknown type, an invalid filter chain
/// (`CheckFilterChain`), or a var the chain does not write.
std::vector<RenderProductSpec>
ResolveRenderProducts(const Scene& scene, std::span<const Path> products, const Path& settings);

/// Resolves the prims a render of `scene` with the RenderSettings prim `settings` uses.
/// @throws std::invalid_argument if `settings` is not empty and not a RenderSettings prim.
RenderPrims ResolveRenderPrims(const Scene& scene, const Path& settings);

/// The frame number the RenderSettings prim `settings` authors in `ernie:frame`, if it does: the
/// frame an image's noise and samples derive from (`FrameSeed`), so a simulation can render any
/// frame again without the ones before it.
/// @throws std::invalid_argument for a negative or non-integer frame.
std::optional<std::uint64_t> FrameOf(const Scene& scene, const Path& settings);

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
///   its `inputs:shadow:enable`; without a sun, no sunlight and no ambient;
/// - the other lights (`LightOf`) and the dome (`DomeOf`);
/// - ambient, wireframe, clear colour (linear) and shadow options from the render settings.
/// @throws std::invalid_argument if there is no camera, it is not perspective, or it or the light
/// are invalid.
FrameParams FrameParamsOf(const Scene& scene, const RenderPrims& prims, std::uint32_t width,
                          std::uint32_t height);

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

}  // namespace ernie
