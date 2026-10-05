#pragma once

#include "ernie/camera/orbit_camera.h"
#include "ernie/camera/physical_camera.h"
#include "ernie/gpu/pipeline.h"
#include "ernie/raster/render_targets.h"
#include "ernie/raster/shadow_cascades.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ernie {

/// Cascaded shadow map options.
struct ShadowSettings {
    bool enabled = true;
    int cascades = 3;                      ///< Number of cascades, 1 to 4.
    std::uint32_t resolution = 2048;       ///< Texels along each side of every cascade's map.
    float softness = 1.5f;                 ///< Poisson filter radius, in shadow map texels.
    float normalOffset = 1.5f;             ///< Receiver offset along the normal, in texels.
    float splitLambda = 0.8f;              ///< Logarithmic weight of the practical split scheme.
    bool showCascades = false;             ///< Tint fragments by cascade, for tuning.
    std::uint32_t lightResolution = 1024;  ///< Texels along each side of other lights' maps.
};

/// Kinds of `Light`.
enum class LightType : std::uint32_t {
    Distant = 0,  ///< Parallel light from a direction, such as Earthshine (UsdLux DistantLight).
    Sphere = 1,   ///< A point or sphere, optionally limited to a cone (UsdLux SphereLight with
                  ///< ShapingAPI): lamps and spot lights.
    Rect = 2,     ///< A one-sided Lambertian rectangle (UsdLux RectLight): panels, windows.
    Disk = 3,     ///< A one-sided Lambertian disk (UsdLux DiskLight).
};

/// A light besides the sun. Distant lights get one orthographic shadow map over the scene; sphere
/// lights a perspective map when their cone is at most `kMaxSpotConeDegrees`, else six (a cube);
/// rect and disk lights a cube at their centre.
///
/// Rect and disk lights are shaded with their exact Lambertian irradiance, the vector form factor
/// of the emitter clipped to the receiver's horizon (Lambert's polygon formula; Arvo 1994), and the
/// receiver's BRDF evaluated along that vector. Disks are integrated as 16-gons of the same area.
///
/// Reference: J. Arvo, "The Irradiance Jacobian for Partially Occluded Polyhedral Sources",
/// SIGGRAPH '94, 343-350, 1994. https://doi.org/10.1145/192161.192250
struct Light {
    LightType type = LightType::Sphere;
    glm::vec3 position{0.0f};  ///< Sphere, rect and disk: world centre.
    /// Distant: unit vector towards the light. Others: unit axis the light emits along (its -z):
    /// the cone's axis, or the normal of the emitting face.
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
    /// Rect and disk: unit vector along the emitter's width, the light's +x; its height runs along
    /// the light's +y, `tangent` x `direction`.
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};
    glm::vec2 size{1.0f, 1.0f};  ///< Rect: width and height.
    /// One value per band of the camera. Distant: spectral irradiance at normal incidence,
    /// W m^-2 nm^-1. Sphere: spectral radiant intensity, W sr^-1 nm^-1, the same in every
    /// direction inside the cone. Rect and disk: spectral radiance of the emitting face,
    /// W m^-2 sr^-1 nm^-1.
    std::vector<float> spectrum;
    /// Sphere radius (irradiance follows the inverse square law outside it and is capped at its
    /// surface value inside), or disk radius.
    float radius = 0.0f;
    float coneAngle = 180.0f;   ///< Degrees off the axis that are lit; 180 lights every direction.
    float coneSoftness = 0.0f;  ///< Fraction of the cone, at its edge, over which light fades out.
    bool shadows = true;

    /// Cones up to this half angle (degrees) get one perspective shadow map, wider ones a cube.
    static constexpr float kMaxSpotConeDegrees = 75.0f;
};

/// An environment light at infinity (UsdLux DomeLight): radiance from every direction, the
/// spectrum `radiance` (W m^-2 sr^-1 nm^-1 per camera band) times a relative RGB map whose
/// cosine-convolved spherical harmonics are `sh`. It lights surfaces diffusely, with the BRDF's
/// albedo, and is not occluded.
///
/// Reference: R. Ramamoorthi and P. Hanrahan, "An Efficient Representation for Irradiance
/// Environment Maps", SIGGRAPH 2001, 497-500. https://doi.org/10.1145/383259.383317
struct DomeLight {
    std::vector<float> radiance;
    /// Irradiance over pi of the RGB map as order-2 spherical harmonics in world space
    /// (`DomeIrradianceSh`): a uniform map of 1 has sh[0] = sqrt(4 pi) and zeros elsewhere.
    std::array<glm::vec3, 9> sh{};
};

/// Per-frame parameters every part shares: the view, the camera, the sun and the shadows.
struct FrameParams {
    /// The raster view. With lens distortion its projection covers the undistorted rays of every
    /// sensor pixel: its focal length is the sensor's divided by `overscan`.
    CameraView camera;
    /// The camera's spectral bands, exposure, lens and sensor. Its `width` and `height` are the
    /// image size, which the render targets must have.
    PhysicalCamera physical;
    double overscan = 1.0;
    bool sun = true;  ///< Off lights the scene with `lights` alone (no sun, ambient or cascades).
    glm::vec3 sunDirection{0.0f, 0.0f, 1.0f};  ///< Unit vector pointing towards the sun.
    /// Angular diameter of the sun's disk in degrees, which widens shadow penumbrae with the
    /// distance from the occluder (percentage-closer soft shadows); 0 keeps `shadows.softness`.
    float sunAngle = 0.53f;
    /// The sun's spectral irradiance at normal incidence per band of `physical.bands`, in
    /// W m^-2 nm^-1. Empty means a flat spectrum scaled so a white Lambertian surface facing the
    /// sun has a luminance of 1 cd/m^2 (relative units).
    std::vector<float> sunIrradiance;
    float ambient = 0.15f;      ///< Fraction of sunlight reaching surfaces without shading.
    std::vector<Light> lights;  ///< Other lights, at most `RasterRenderer::kMaxLights`.
    std::optional<DomeLight> dome;
    float exposure = 1.0f;   ///< Extra scale on the photographic output.
    bool wireframe = false;  ///< Draw triangle edges over every part.
    ShadowSettings shadows;
    /// The sensor filters to run (`CheckFilterChain`); empty runs `DefaultFilterChain`.
    std::vector<SensorFilter> filters;
    glm::vec3 clearColor{0.0f};  ///< Linear display colour of the background in LdrColor.
};

/// Axis-aligned world bounds, low and high corners.
using Bounds3 = std::pair<glm::vec3, glm::vec3>;

/// The per-frame resources every raster pass binds (`frame.slang`): view and light uniforms,
/// shadow uniforms, cascaded shadow maps, their comparison sampler and a clamping linear sampler,
/// at bindings 0 to 4.
struct RasterFrame {
    DescriptorResource uniforms;
    DescriptorResource shadows;
    DescriptorResource shadowMap;
    DescriptorResource shadowSampler;
    DescriptorResource clampSampler;
};

/// Something a `RasterRenderer` draws: terrain, meshes, and in future volumes or points. Each
/// frame the renderer asks every part for its bounds, fits the shadow views around them (the
/// sun's cascades, then the views of the other lights' shadow maps), lets each part prepare for
/// the final view and shadow views, and then has each record its draws into every shadow view and
/// into the main pass. Parts bind the frame's resources at bindings 0 to
/// 4, a material's at 5 to 9 (`RasterRenderer::Material`) and their own from 10.
class RasterPart {
  public:
    virtual ~RasterPart() = default;

    /// World bounds of what the part draws, for fitting shadow cascades; none if nothing.
    virtual std::optional<Bounds3> Bounds() const = 0;

    /// Prepares the next frame once the view and shadow views are final, for example selecting
    /// levels of detail for the main view and each shadow view (orthographic or perspective; only
    /// their `lightViewProjection` applies to every view). Called by `RasterRenderer::Update`,
    /// only while no submitted frame is reading the part's buffers.
    virtual void Prepare(const FrameParams& params, std::span<const ShadowCascade> cascades) = 0;

    /// Records depth-only draws for shadow view `cascade` (an index into the views given to
    /// `Prepare`) into the open rendering pass.
    virtual void RecordShadow(VkCommandBuffer commands, const RasterFrame& frame,
                              std::size_t cascade, const glm::mat4& lightViewProjection) const = 0;

    /// Records colour draws into the open main rendering pass.
    virtual void RecordColor(VkCommandBuffer commands, const RasterFrame& frame) const = 0;
};

}  // namespace ernie
