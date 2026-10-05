#pragma once

#include "ernie/core/spectrum.h"
#include "ernie/scene/scene.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ernie {

/// The raster view of a camera for one frame, derived from a `Camera` by `Camera::RasterView`:
/// what the rasterizer's passes need.
struct CameraView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 forward{0.0f, 1.0f, 0.0f};  ///< Unit viewing direction.
    float verticalFov = 0.785398f;        ///< Radians.
    float aspect = 1.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    glm::mat4 ViewProjection() const {
        return projection * view;
    }
};

/// A lens: radial and tangential distortion (Brown-Conrady, in OpenCV's convention), natural
/// cos^4 vignetting and transmission.
///
/// Distortion maps undistorted normalised image coordinates (x, y) = (X/Z, Y/Z) to distorted ones:
///   x' = x (1 + k1 r^2 + k2 r^4 + k3 r^6) + 2 p1 x y + p2 (r^2 + 2 x^2)
///   y' = y (1 + k1 r^2 + k2 r^4 + k3 r^6) + p1 (r^2 + 2 y^2) + 2 p2 x y
///
/// References:
/// - D. C. Brown, "Decentering Distortion of Lenses", Photogrammetric Engineering 32(3):444-462,
///   1966.
/// - OpenCV, "Camera Calibration and 3D Reconstruction",
///   https://docs.opencv.org/4.x/d9/d0c/group__calib3d.html
struct LensModel {
    double k1 = 0.0;
    double k2 = 0.0;
    double k3 = 0.0;
    double p1 = 0.0;
    double p2 = 0.0;
    bool vignetting = false;    ///< Natural cos^4 falloff of irradiance off axis.
    double transmission = 1.0;  ///< Fraction of light the optics pass.

    bool HasDistortion() const {
        return k1 != 0.0 || k2 != 0.0 || k3 != 0.0 || p1 != 0.0 || p2 != 0.0;
    }

    glm::dvec2 Distort(glm::dvec2 undistorted) const;

    /// The inverse of `Distort` by fixed-point iteration, accurate for moderate distortion.
    glm::dvec2 Undistort(glm::dvec2 distorted) const;
};

/// One channel of a sensor: its quantum efficiency (electrons per photon) over wavelength.
struct SensorChannel {
    std::string name;
    Spectrum quantumEfficiency;
};

/// An image sensor: channels with quantum efficiency curves, full well, noise, gain and ADC.
///
/// Electrons accumulate as photons times quantum efficiency, plus dark current; the readout adds
/// photon shot noise (Poisson), read noise (Gaussian), and per-pixel fixed patterns PRNU
/// (relative gain) and DSNU (offset); electrons clip at the full well, and the ADC converts them
/// to digital numbers with the ISO-dependent gain, adds the black level and clips to its bit
/// depth. The presets are generic curve shapes, not any particular sensor: replace the channels
/// with measured curves for a specific camera.
///
/// Reference: EMVA Standard 1288, "Standard for Characterization of Image Sensors and Cameras",
/// release 4.0, 2021 (the linear camera model).
struct SensorModel {
    std::vector<SensorChannel> channels;
    double fullWellElectrons = 20000.0;
    double readNoiseElectrons = 3.0;      ///< Standard deviation.
    double darkCurrentElectrons = 0.1;    ///< Per second.
    double prnu = 0.005;                  ///< Standard deviation of the relative gain.
    double dsnuElectrons = 0.5;           ///< Standard deviation of the per-pixel offset.
    double electronsPerDnAtIso100 = 0.0;  ///< 0 means full well over the ADC range.
    int adcBits = 12;
    double blackLevelDn = 0.0;
    bool noise = true;
    std::uint32_t seed = 0;

    /// Bayer-like red, green and blue channels behind an infrared cut filter.
    static SensorModel Rgb();

    /// One panchromatic channel with a front-illuminated silicon response, like the monochrome
    /// pushbroom cameras of lunar orbiters.
    static SensorModel Mono();

    /// Electrons per digital number at `iso`.
    double ElectronsPerDn(double iso) const;
};

/// Ernie's camera: a physically based pinhole camera for one image, with UsdGeomCamera's pose,
/// projection and exposure attributes, a spectral range, a lens and a sensor.
///
/// The pose `transform` maps camera to world coordinates; as in USD the camera looks down its -z
/// axis with +y up. Interactive controls such as `OrbitController` write it.
///
/// Focal length and apertures are read in millimetres. The image fits the vertical aperture:
/// pixels are square with a pitch of `verticalAperture / height`, and the horizontal aperture
/// follows from the image's aspect. Aperture offsets shift the principal point.
///
/// Radiometry follows the camera equation: a scene radiance L reaches the sensor as irradiance
/// E = L pi t cos^4(theta) / (4 N^2) for an object far away, with transmission t, f-number N and
/// field angle theta; a pixel collects E times its area times the exposure time, converted to
/// photons and electrons per band. The photographic outputs instead scale luminance by USD's
/// linear exposure scale.
///
/// References:
/// - B. K. P. Horn, "Robot Vision", MIT Press, 1986 (the image irradiance equation
///   E = L (pi / 4) (d / f)^2 cos^4(alpha), with N = f / d).
/// - Pixar, "UsdGeomCamera", https://openusd.org/release/api/class_usd_geom_camera.html
struct Camera {
    glm::dmat4 transform{1.0};  ///< Camera to world.
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    double focalLengthMm = 50.0;
    double verticalApertureMm = 15.2908;
    double horizontalApertureOffsetMm = 0.0;
    double verticalApertureOffsetMm = 0.0;
    double nearPlane = 1.0;
    double farPlane = 1e6;
    double exposureStops = 0.0;  ///< USD `exposure`: scales the photographic output by 2^stops.
    double exposureTime = 1.0;   ///< Seconds.
    double iso = 100.0;
    double fStop = 1.0;
    double responsivity = 1.0;
    SpectralBands bands;
    LensModel lens;
    SensorModel sensor = SensorModel::Rgb();

    /// The camera's position in the world.
    glm::dvec3 Eye() const {
        return glm::dvec3(transform[3]);
    }

    /// Unit viewing direction in the world, the transform's -z axis.
    glm::dvec3 Forward() const;

    /// Sets the vertical aperture so the image spans `radians` vertically at the focal length.
    void SetVerticalFov(double radians);

    /// The raster view: the inverse pose, and a projection with the focal length divided by
    /// `overscan` (so with lens distortion it covers the undistorted rays of every sensor pixel),
    /// the principal point and the clipping range.
    CameraView RasterView(double overscan = 1.0) const;

    /// USD's ratio between incident luminance and the normalised photographic output:
    /// responsivity * time * (iso / 100) * 2^exposure / fStop^2.
    double LinearExposureScale() const;

    /// Side of a pixel, in metres.
    double PixelPitchMetres() const;

    /// Focal length in pixels.
    double FocalLengthPixels() const;

    /// The principal point in pixels, from the image's top-left corner.
    glm::dvec2 PrincipalPoint() const;

    /// The vertical field of view in radians.
    double VerticalFov() const;

    /// Irradiance at the sensor per unit scene radiance at normalised image coordinates
    /// (`x`, `y`): pi t cos^4(theta) / (4 N^2), without the cos^4 term when vignetting is off.
    double IrradiancePerRadiance(glm::dvec2 normalised) const;

    /// Mean electrons a pixel at normalised coordinates `normalised` collects in channel
    /// `channel` from band radiances `radiance` (W m^-2 sr^-1 nm^-1, one per band), before dark
    /// current, noise and clipping.
    double Electrons(std::span<const float> radiance, std::size_t channel,
                     glm::dvec2 normalised = {0.0, 0.0}) const;

    /// Per-band factors f_i so that `Electrons` = sum_i radiance_i f_i at the image centre.
    std::vector<float> ElectronsPerRadiance(std::size_t channel) const;

    /// @throws std::invalid_argument for non-positive sizes, times, f-numbers, ISO or focal
    /// length, a bad clipping range, a degenerate transform, a bad spectral range, no sensor
    /// channels or more than four.
    void Check() const;
};

/// The camera of the Camera prim at `path` for an image of `width` x `height`: its world
/// transform, and UsdGeomCamera's `focalLength`, `verticalAperture`, aperture offsets,
/// `clippingRange`, `exposure`, `exposure:time`, `exposure:iso`, `exposure:fStop` and
/// `exposure:responsivity`, and Ernie's `ernie:spectral:*`, `ernie:lens:*` and `ernie:sensor:*`
/// attributes (see `tokens.h`). Unset attributes keep `Camera`'s defaults.
/// @throws std::invalid_argument if the prim is not a Camera, its projection is not perspective,
/// or the result fails `Check`.
Camera CameraOf(const Scene& scene, const Path& path, std::uint32_t width, std::uint32_t height);

}  // namespace ernie
