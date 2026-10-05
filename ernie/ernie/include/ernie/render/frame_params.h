#pragma once

#include "ernie/camera/camera.h"
#include "ernie/camera/sensor_filters.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
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

/// A light besides the sun. In the rasterizer, distant lights get one orthographic shadow map over
/// the scene; sphere lights a perspective map when their cone is at most `kMaxSpotConeDegrees`,
/// else six (a cube); rect and disk lights a cube at their centre.
///
/// The rasterizer shades rect and disk lights with their exact Lambertian irradiance, the vector
/// form factor of the emitter clipped to the receiver's horizon (Lambert's polygon formula; Arvo
/// 1994), and the receiver's BRDF evaluated along that vector. Disks are integrated as 16-gons of
/// the same area.
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

/// Per-frame parameters of a render, whatever the backend: the camera, the sun, the other lights,
/// the sensor filters and the frame's place in the random streams. `shadows` tunes the
/// rasterizer's shadow maps; tracers ignore it.
struct FrameParams {
    /// The camera: pose, projection, spectral bands, exposure, lens and sensor. Its `width` and
    /// `height` are the image size, which the render targets must have.
    Camera camera;
    /// How much wider than the camera's the raster view's field of view is, so that with lens
    /// distortion it covers the undistorted rays of every sensor pixel (`OverscanOf`).
    double overscan = 1.0;
    bool sun = true;  ///< Off lights the scene with `lights` alone (no sun, ambient or cascades).
    glm::vec3 sunDirection{0.0f, 0.0f, 1.0f};  ///< Unit vector pointing towards the sun.
    /// Angular diameter of the sun's disk in degrees, which widens shadow penumbrae with the
    /// distance from the occluder (percentage-closer soft shadows); 0 keeps `shadows.softness`.
    float sunAngle = 0.53f;
    /// The sun's spectral irradiance at normal incidence per band of `camera.bands`, in
    /// W m^-2 nm^-1. Empty means a flat spectrum scaled so a white Lambertian surface facing the
    /// sun has a luminance of 1 cd/m^2 (relative units).
    std::vector<float> sunIrradiance;
    float ambient = 0.15f;  ///< Fraction of sunlight reaching surfaces without shading.
    std::vector<Light>
        lights;  ///< Other lights; the rasterizer takes `RasterRenderer::kMaxLights`.
    std::optional<DomeLight> dome;
    float exposure = 1.0f;   ///< Extra scale on the photographic output.
    bool wireframe = false;  ///< Draw triangle edges over every part.
    ShadowSettings shadows;
    /// The sensor filters to run (`CheckFilterChain`); empty runs `DefaultFilterChain`.
    std::vector<SensorFilter> filters;
    glm::vec3 clearColor{0.0f};  ///< Linear display colour of the background in LdrColor.
    /// The frame number temporal noise derives from (`FrameSeed`): rendering the same frame again
    /// gives the same image, and consecutive frames independent noise.
    std::uint64_t frame = 0;
    /// Decorrelates images rendered with the same seed and frame, such as the products of one
    /// step: `StreamOf` the render product's path.
    std::uint32_t stream = 0;

    /// The raster view the passes draw with, `camera.RasterView(overscan)`.
    CameraView View() const {
        return camera.RasterView(overscan);
    }
};

/// Axis-aligned world bounds, low and high corners.
using Bounds3 = std::pair<glm::vec3, glm::vec3>;

}  // namespace ernie
