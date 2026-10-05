#include "ernie/camera/camera.h"

#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

/// A Gaussian bump of height `peak` centred on `centre` nm with standard deviation `width` nm,
/// tabulated every 5 nm over 350-1100 nm.
Spectrum Bump(double centre, double width, double peak) {
    std::vector<double> wavelengths;
    std::vector<double> values;
    for (double w = 350.0; w <= 1100.0; w += 5.0) {
        const double t = (w - centre) / width;
        wavelengths.push_back(w);
        values.push_back(peak * std::exp(-0.5 * t * t));
    }
    return Spectrum(std::move(wavelengths), std::move(values));
}

const std::vector<float>* FloatArray(const Scene& scene, const Path& path,
                                     std::string_view attribute) {
    const Value* value = scene.Get(path, attribute);
    return value != nullptr ? value->Get<std::vector<float>>() : nullptr;
}

}  // namespace

glm::dvec2 LensModel::Distort(glm::dvec2 p) const {
    const double r2 = glm::dot(p, p);
    const double radial = 1.0 + r2 * (k1 + r2 * (k2 + r2 * k3));
    return {p.x * radial + 2.0 * p1 * p.x * p.y + p2 * (r2 + 2.0 * p.x * p.x),
            p.y * radial + p1 * (r2 + 2.0 * p.y * p.y) + 2.0 * p2 * p.x * p.y};
}

glm::dvec2 LensModel::Undistort(glm::dvec2 distorted) const {
    glm::dvec2 p = distorted;
    for (int i = 0; i < 20; ++i) {
        p += distorted - Distort(p);
    }
    return p;
}

SensorModel SensorModel::Rgb() {
    SensorModel sensor;
    sensor.channels = {{"r", Bump(600.0, 32.0, 0.50)},
                       {"g", Bump(535.0, 36.0, 0.55)},
                       {"b", Bump(460.0, 28.0, 0.45)}};
    return sensor;
}

SensorModel SensorModel::Mono() {
    SensorModel sensor;
    sensor.channels = {
        {"mono", Spectrum({350.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 1100.0},
                          {0.15, 0.35, 0.55, 0.62, 0.55, 0.40, 0.22, 0.06, 0.0})}};
    return sensor;
}

double SensorModel::ElectronsPerDn(double iso) const {
    const double maxDn = std::exp2(adcBits) - 1.0;
    const double atIso100 =
        electronsPerDnAtIso100 > 0.0 ? electronsPerDnAtIso100 : fullWellElectrons / maxDn;
    return atIso100 * 100.0 / iso;
}

double Camera::LinearExposureScale() const {
    return responsivity * exposureTime * (iso / 100.0) * std::exp2(exposureStops) / (fStop * fStop);
}

double Camera::PixelPitchMetres() const {
    return verticalApertureMm * 1e-3 / height;
}

double Camera::FocalLengthPixels() const {
    return focalLengthMm * 1e-3 / PixelPitchMetres();
}

glm::dvec2 Camera::PrincipalPoint() const {
    const double pitchMm = PixelPitchMetres() * 1e3;
    return {0.5 * width + horizontalApertureOffsetMm / pitchMm,
            0.5 * height - verticalApertureOffsetMm / pitchMm};
}

double Camera::VerticalFov() const {
    return 2.0 * std::atan(0.5 * verticalApertureMm / focalLengthMm);
}

double Camera::IrradiancePerRadiance(glm::dvec2 normalised) const {
    double factor = M_PI * lens.transmission / (4.0 * fStop * fStop);
    if (lens.vignetting) {
        const double cosine2 = 1.0 / (1.0 + glm::dot(normalised, normalised));
        factor *= cosine2 * cosine2;
    }
    return factor;
}

std::vector<float> Camera::ElectronsPerRadiance(std::size_t channel) const {
    const SensorChannel& sensorChannel = sensor.channels.at(channel);
    const double area = PixelPitchMetres() * PixelPitchMetres();
    const double optics = IrradiancePerRadiance({0.0, 0.0});
    std::vector<float> factors(static_cast<std::size_t>(bands.count));
    for (int i = 0; i < bands.count; ++i) {
        const double low = bands.Lower(i);
        const double qe = sensorChannel.quantumEfficiency.Average(low, low + bands.Width());
        const double photonsPerJoule = bands.Centre(i) * 1e-9 / (kPlanck * kSpeedOfLight);
        factors[static_cast<std::size_t>(i)] =
            static_cast<float>(optics * area * exposureTime * bands.Width() * photonsPerJoule * qe);
    }
    return factors;
}

double Camera::Electrons(std::span<const float> radiance, std::size_t channel,
                         glm::dvec2 normalised) const {
    const std::vector<float> factors = ElectronsPerRadiance(channel);
    if (radiance.size() != factors.size()) {
        throw std::invalid_argument("Camera::Electrons needs one radiance per band");
    }
    double electrons = 0.0;
    for (std::size_t i = 0; i < factors.size(); ++i) {
        electrons += static_cast<double>(radiance[i]) * static_cast<double>(factors[i]);
    }
    return electrons * IrradiancePerRadiance(normalised) / IrradiancePerRadiance({0.0, 0.0});
}

glm::dvec3 Camera::Forward() const {
    return glm::normalize(-glm::dvec3(transform[2]));
}

void Camera::SetVerticalFov(double radians) {
    verticalApertureMm = 2.0 * focalLengthMm * std::tan(0.5 * radians);
}

CameraView Camera::RasterView(double overscan) const {
    const double w = width;
    const double h = height;
    const double focal = FocalLengthPixels() / overscan;
    const glm::dvec2 centre = PrincipalPoint();
    CameraView view;
    view.verticalFov = static_cast<float>(2.0 * std::atan(0.5 * h / focal));
    view.aspect = static_cast<float>(w / h);
    view.nearPlane = static_cast<float>(nearPlane);
    view.farPlane = static_cast<float>(farPlane);
    view.eye = glm::vec3(Eye());
    view.forward = glm::vec3(Forward());
    view.view = glm::mat4(glm::inverse(transform));
    view.projection =
        glm::perspectiveRH_ZO(view.verticalFov, view.aspect, view.nearPlane, view.farPlane);
    view.projection[2][0] = static_cast<float>(1.0 - 2.0 * centre.x / w);
    view.projection[2][1] = static_cast<float>(2.0 * centre.y / h - 1.0);
    return view;
}

void Camera::Check() const {
    bands.Check();
    if (width == 0 || height == 0 || !(focalLengthMm > 0.0) || !(verticalApertureMm > 0.0) ||
        !(exposureTime > 0.0) || !(iso > 0.0) || !(fStop > 0.0) || !(responsivity > 0.0)) {
        throw std::invalid_argument("A camera needs positive image size, focal length, "
                                    "aperture, exposure time, ISO, f-number and responsivity");
    }
    if (!(nearPlane > 0.0) || !(farPlane > nearPlane)) {
        throw std::invalid_argument("A camera needs a positive clipping range");
    }
    if (!(std::abs(glm::determinant(glm::dmat3(transform))) > 0.0)) {
        throw std::invalid_argument("A camera needs an invertible transform");
    }
    if (sensor.channels.empty() || sensor.channels.size() > 4) {
        throw std::invalid_argument("A sensor needs one to four channels");
    }
    if (sensor.adcBits < 1 || sensor.adcBits > 16 || !(sensor.fullWellElectrons > 0.0) ||
        !(lens.transmission > 0.0)) {
        throw std::invalid_argument("A sensor needs 1 to 16 ADC bits and a positive full well, "
                                    "and a lens a positive transmission");
    }
}

Camera CameraOf(const Scene& scene, const Path& path, std::uint32_t width, std::uint32_t height) {
    using namespace tokens;
    if (!scene.Has(path) || scene.TypeOf(path) != kCamera) {
        throw std::invalid_argument("'" + path.String() + "' is not a Camera");
    }
    if (GetToken(scene, path, kProjection, kPerspective) != kPerspective) {
        throw std::invalid_argument("Camera '" + path.String() +
                                    "': only perspective projection is supported");
    }
    Camera camera;
    camera.transform = scene.WorldTransform(path);
    camera.width = width;
    camera.height = height;
    const auto number = [&](std::string_view attribute, double fallback) {
        return GetNumber(scene, path, attribute, fallback);
    };
    camera.focalLengthMm = number(kFocalLength, camera.focalLengthMm);
    camera.verticalApertureMm = number(kVerticalAperture, camera.verticalApertureMm);
    camera.horizontalApertureOffsetMm = number(kHorizontalApertureOffset, 0.0);
    camera.verticalApertureOffsetMm = number(kVerticalApertureOffset, 0.0);
    const glm::dvec2 clipping =
        GetVec2(scene, path, kClippingRange, {camera.nearPlane, camera.farPlane});
    camera.nearPlane = clipping.x;
    camera.farPlane = clipping.y;
    camera.exposureStops = number(kCameraExposure, 0.0);
    camera.exposureTime = number(kExposureTime, camera.exposureTime);
    camera.iso = number(kExposureIso, camera.iso);
    camera.fStop = number(kExposureFStop, camera.fStop);
    camera.responsivity = number(kExposureResponsivity, camera.responsivity);

    const glm::dvec2 range =
        GetVec2(scene, path, kSpectralRange, {camera.bands.minNm, camera.bands.maxNm});
    camera.bands = {range.x, range.y, static_cast<int>(number(kSpectralBands, camera.bands.count))};

    LensModel& lens = camera.lens;
    if (const std::vector<float>* k = FloatArray(scene, path, kLensDistortion)) {
        const auto at = [k](std::size_t i) { return i < k->size() ? double((*k)[i]) : 0.0; };
        lens.k1 = at(0);
        lens.k2 = at(1);
        lens.k3 = at(2);
        lens.p1 = at(3);
        lens.p2 = at(4);
    }
    lens.vignetting = GetBool(scene, path, kLensVignetting, lens.vignetting);
    lens.transmission = number(kLensTransmission, lens.transmission);

    const std::string type = GetToken(scene, path, kSensorType, "rgb");
    if (type == "rgb") {
        camera.sensor = SensorModel::Rgb();
    } else if (type == "mono") {
        camera.sensor = SensorModel::Mono();
    } else {
        throw std::invalid_argument("Camera '" + path.String() + "' has sensor type '" + type +
                                    "'; use rgb or mono");
    }
    SensorModel& sensor = camera.sensor;
    for (SensorChannel& channel : sensor.channels) {
        const std::string attribute = std::string(kSensorQePrefix) + channel.name;
        if (const std::vector<float>* pairs = FloatArray(scene, path, attribute)) {
            channel.quantumEfficiency = Spectrum::FromPairs(*pairs);
        }
    }
    sensor.fullWellElectrons = number(kSensorFullWell, sensor.fullWellElectrons);
    sensor.readNoiseElectrons = number(kSensorReadNoise, sensor.readNoiseElectrons);
    sensor.darkCurrentElectrons = number(kSensorDarkCurrent, sensor.darkCurrentElectrons);
    sensor.prnu = number(kSensorPrnu, sensor.prnu);
    sensor.dsnuElectrons = number(kSensorDsnu, sensor.dsnuElectrons);
    sensor.electronsPerDnAtIso100 = number(kSensorGain, sensor.electronsPerDnAtIso100);
    sensor.adcBits = static_cast<int>(number(kSensorAdcBits, sensor.adcBits));
    sensor.blackLevelDn = number(kSensorBlackLevel, sensor.blackLevelDn);
    sensor.noise = GetBool(scene, path, kSensorNoise, sensor.noise);
    sensor.seed = static_cast<std::uint32_t>(number(kSensorSeed, sensor.seed));
    camera.Check();
    return camera;
}

}  // namespace ernie
