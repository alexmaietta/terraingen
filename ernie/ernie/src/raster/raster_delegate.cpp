#include "ernie/raster/raster_delegate.h"

#include "ernie/scene/schema.h"
#include "ernie/scene/tokens.h"

#include "product_frame.h"
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace ernie {

namespace {

Path FirstVisible(const Scene& scene, std::string_view type) {
    for (const Path& path : scene.Prims(type)) {
        if (scene.IsVisible(path)) {
            return path;
        }
    }
    return {};
}

float Number(const Scene& scene, const Path& path, std::string_view attribute, float fallback) {
    return static_cast<float>(GetNumber(scene, path, attribute, fallback));
}

std::invalid_argument UploadError(const Path& path, std::string_view attribute,
                                  const std::string& why) {
    return std::invalid_argument("Cannot upload " + std::string(attribute) + " of '" +
                                 path.String() + "': " + why);
}

/// `tensor` as a float32 raster with the grid in the prim's attributes.
RasterView RasterOf(const Scene& scene, const Path& path, const Tensor& tensor,
                    std::string_view origin, std::string_view cellSize, std::string_view noData) {
    if (tensor.Type() != DType::Float32 || tensor.Rank() != 2 ||
        tensor.Location().kind != DeviceKind::Cpu) {
        throw std::invalid_argument("expected a float32 (rows, cols) CPU tensor");
    }
    RasterView view;
    view.data = tensor.As<float>();
    view.height = static_cast<std::size_t>(tensor.Shape()[0]);
    view.width = static_cast<std::size_t>(tensor.Shape()[1]);
    const glm::dvec2 corner = GetVec2(scene, path, origin, {0.0, 0.0});
    view.originX = corner.x;
    view.originY = corner.y;
    view.cellSize = GetNumber(scene, path, cellSize, 1.0);
    if (scene.Get(path, noData) != nullptr) {
        view.noData = static_cast<float>(GetNumber(scene, path, noData, 0.0));
    }
    return view;
}

TerrainColorMode ColorModeOf(const std::string& token) {
    if (token == tokens::kHillshade) {
        return TerrainColorMode::Hillshade;
    }
    if (token == tokens::kColormap) {
        return TerrainColorMode::Colormap;
    }
    return TerrainColorMode::ShadedColormap;
}

/// A CPU array of `components`-vectors as floats, shaped (n, components) or flat.
std::vector<float> FloatsOf(const Tensor& tensor, std::size_t components, std::string_view what) {
    const bool shaped =
        tensor.Rank() == 2 && tensor.Shape()[1] == static_cast<std::int64_t>(components);
    if (tensor.Location().kind != DeviceKind::Cpu || !(tensor.Rank() == 1 || shaped) ||
        static_cast<std::size_t>(tensor.Elements()) % components != 0) {
        throw std::invalid_argument(std::string(what) + " must be a CPU array of " +
                                    std::to_string(components) + "-vectors");
    }
    const Tensor packed = tensor.Contiguous();
    const auto count = static_cast<std::size_t>(packed.Elements());
    if (packed.Type() == DType::Float32) {
        return {packed.As<float>(), packed.As<float>() + count};
    }
    if (packed.Type() == DType::Float64) {
        return {packed.As<double>(), packed.As<double>() + count};
    }
    throw std::invalid_argument(std::string(what) + " must be float32 or float64");
}

/// A CPU array of integers, of any shape.
std::vector<std::int64_t> IntsOf(const Tensor& tensor, std::string_view what) {
    if (tensor.Location().kind != DeviceKind::Cpu) {
        throw std::invalid_argument(std::string(what) + " must be a CPU array");
    }
    const Tensor packed = tensor.Contiguous();
    const auto count = static_cast<std::size_t>(packed.Elements());
    switch (packed.Type()) {
    case DType::Int32:
        return {packed.As<std::int32_t>(), packed.As<std::int32_t>() + count};
    case DType::Int64:
        return {packed.As<std::int64_t>(), packed.As<std::int64_t>() + count};
    case DType::UInt32:
        return {packed.As<std::uint32_t>(), packed.As<std::uint32_t>() + count};
    default:
        throw std::invalid_argument(std::string(what) + " must be int32, int64 or uint32");
    }
}

/// The prim's attribute as an array: a written tensor or, for small arrays, a float array value.
std::optional<std::vector<float>> FloatAttribute(const Scene& scene, const Path& path,
                                                 std::string_view attribute,
                                                 std::size_t components) {
    if (const Tensor* tensor = scene.Read(path, attribute)) {
        return FloatsOf(*tensor, components, attribute);
    }
    return std::nullopt;
}

/// Targets of a relationship holding one path or a list of them.
std::vector<Path> TargetsOf(const Scene& scene, const Path& path, std::string_view attribute) {
    const Value* value = scene.Get(path, attribute);
    if (value == nullptr) {
        return {};
    }
    if (const auto* list = value->Get<std::vector<Path>>()) {
        return *list;
    }
    if (const auto* single = value->Get<Path>()) {
        return {*single};
    }
    return {};
}

/// The constant colour of `primvars:displayColor`: a colour value or a one-row array.
std::optional<glm::vec3> ConstantDisplayColor(const Scene& scene, const Path& mesh) {
    if (scene.Get(mesh, tokens::kDisplayColor) != nullptr) {
        return glm::vec3(GetVec3(scene, mesh, tokens::kDisplayColor, glm::dvec3(0.5)));
    }
    if (const Tensor* tensor = scene.Read(mesh, tokens::kDisplayColor)) {
        if (tensor->Elements() == 3) {
            const std::vector<float> color = FloatsOf(*tensor, 3, tokens::kDisplayColor);
            return glm::vec3(color[0], color[1], color[2]);
        }
    }
    return std::nullopt;
}

std::string Describe(const Path& path, std::string_view what, const std::exception& error) {
    return "'" + path.String() + "' " + std::string(what) + ": " + error.what();
}

/// The `ernie:spectrum` pairs of a light, if it has them.
const std::vector<float>* SpectrumPairs(const Scene& scene, const Path& light) {
    const Value* value = scene.Get(light, tokens::kLightSpectrum);
    return value != nullptr ? value->Get<std::vector<float>>() : nullptr;
}

/// UsdLux's `inputs:intensity` times 2^`inputs:exposure`.
double LightIntensity(const Scene& scene, const Path& light) {
    return GetNumber(scene, light, tokens::kIntensity, 1.0) *
           std::exp2(GetNumber(scene, light, tokens::kExposure, 0.0));
}

/// The black body temperature of a light with `inputs:enableColorTemperature`, else 0.
double ColorTemperature(const Scene& scene, const Path& light) {
    return GetBool(scene, light, tokens::kEnableColorTemperature, false)
               ? GetNumber(scene, light, tokens::kColorTemperature, 6500.0)
               : 0.0;
}

/// `shape` averaged over each of `bands` (eight samples per band) times `scale`.
template <typename Shape>
std::vector<float> BinShape(const Shape& shape, const SpectralBands& bands, double scale) {
    std::vector<float> values(static_cast<std::size_t>(bands.count));
    for (int i = 0; i < bands.count; ++i) {
        double sum = 0.0;
        for (int k = 0; k < 8; ++k) {
            sum += shape(bands.Lower(i) + (k + 0.5) * bands.Width() / 8.0);
        }
        values[static_cast<std::size_t>(i)] = static_cast<float>(scale * sum / 8.0);
    }
    return values;
}

/// A light's spectrum in relative units, per band of `bands`: the shape of `inputs:color`
/// (upsampled with `RgbToSigmoid`) times a black body at the enabled colour temperature, scaled
/// so that its luminous value (683 lm/W times its integral against the CIE y function) is
/// `luminous`.
std::vector<float> RelativeSpectrum(const Scene& scene, const Path& light,
                                    const SpectralBands& bands, double luminous) {
    const double temperature = ColorTemperature(scene, light);
    const glm::dvec3 color = GetVec3(scene, light, tokens::kColor, glm::dvec3(1.0));
    const bool white = color == glm::dvec3(1.0);
    const SigmoidSpectrum tint = RgbToSigmoid(color);
    const auto shape = [&](double w) {
        return (temperature > 0.0 ? PlanckRadiance(w, temperature) : 1.0) * (white ? 1.0 : tint(w));
    };
    double luminance = 0.0;
    for (double w = 360.0; w <= 830.0; w += 1.0) {
        luminance += shape(w) * CieXyz(w).y;
    }
    return BinShape(shape, bands, luminous / (kLuminousEfficacy * luminance));
}

/// A physical SphereLight's spectral radiant intensity per band, W sr^-1 nm^-1 (see
/// `tokens::kLightPower`).
std::vector<float> LampIntensity(const Scene& scene, const Path& light, const SpectralBands& bands,
                                 const std::vector<float>* pairs) {
    const bool hasPower = scene.Get(light, tokens::kLightPower) != nullptr;
    const double power = GetNumber(scene, light, tokens::kLightPower, 0.0);
    if (power < 0.0) {
        throw std::invalid_argument("Light '" + light.String() + "' has a negative power");
    }
    if (pairs != nullptr) {
        const Spectrum spectrum = Spectrum::FromPairs(*pairs);
        std::vector<float> binned = spectrum.Bin(bands);
        if (hasPower && !spectrum.Empty()) {
            const double low = std::max(360.0, spectrum.Wavelengths().front());
            const double high = std::min(830.0, spectrum.Wavelengths().back());
            const double total = high > low ? spectrum.Average(low, high) * (high - low) : 0.0;
            const double scale = total > 0.0 ? power / (4.0 * M_PI) / total : 0.0;
            for (float& value : binned) {
                value = static_cast<float>(value * scale);
            }
        }
        return binned;
    }
    const double temperature = ColorTemperature(scene, light);
    const auto shape = [temperature](double w) {
        return temperature > 0.0 ? PlanckRadiance(w, temperature) : 1.0;
    };
    double total = 0.0;
    for (double w = 360.5; w < 830.0; w += 1.0) {
        total += shape(w);
    }
    return BinShape(shape, bands, power / (4.0 * M_PI) / total);
}

}  // namespace

Path BoundMaterial(const Scene& scene, const Path& prim) {
    const Path material = GetTarget(scene, prim, tokens::kMaterialBinding);
    if (scene.Has(material) && scene.TypeOf(material) == tokens::kMaterial) {
        return material;
    }
    return {};
}

RasterPrims ResolveRasterPrims(const Scene& scene, const Path& settings) {
    RasterPrims prims;
    if (!settings.IsEmpty()) {
        if (!scene.Has(settings) || scene.TypeOf(settings) != tokens::kRenderSettings) {
            throw std::invalid_argument("'" + settings.String() + "' is not a RenderSettings prim");
        }
        prims.settings = settings;
    } else {
        const std::vector<Path> all = scene.Prims(tokens::kRenderSettings);
        prims.settings = all.empty() ? Path() : all.front();
    }
    if (!prims.settings.IsEmpty()) {
        prims.camera = GetTarget(scene, prims.settings, tokens::kCameraRel);
    }
    if (prims.camera.IsEmpty()) {
        const std::vector<Path> cameras = scene.Prims(tokens::kCamera);
        prims.camera = cameras.empty() ? Path() : cameras.front();
    }
    prims.terrain = FirstVisible(scene, tokens::kTerrain);
    if (!prims.terrain.IsEmpty()) {
        prims.material = BoundMaterial(scene, prims.terrain);
    }
    prims.light = FirstVisible(scene, tokens::kDistantLight);
    std::vector<Path> lights;
    for (const std::string_view type : {tokens::kDistantLight, tokens::kSphereLight}) {
        for (const Path& path : scene.Prims(type)) {
            if (path != prims.light && scene.IsVisible(path)) {
                lights.push_back(path);
            }
        }
    }
    std::sort(lights.begin(), lights.end());
    prims.lights = std::move(lights);
    return prims;
}

std::vector<RenderProductSpec>
ResolveRenderProducts(const Scene& scene, std::span<const Path> products, const Path& settings) {
    using namespace tokens;
    const Path resolved = ResolveRasterPrims(scene, settings).settings;
    const glm::dvec2 settingsResolution = GetVec2(scene, resolved, kResolution, {2048.0, 1080.0});
    std::vector<Path> paths(products.begin(), products.end());
    if (paths.empty() && !resolved.IsEmpty()) {
        paths = TargetsOf(scene, resolved, kProducts);
    }
    const auto extentOf = [](glm::dvec2 resolution) {
        if (!(resolution.x >= 1.0) || !(resolution.y >= 1.0)) {
            throw std::invalid_argument("Render resolutions must be positive");
        }
        return VkExtent2D{static_cast<std::uint32_t>(resolution.x),
                          static_cast<std::uint32_t>(resolution.y)};
    };
    std::vector<RenderProductSpec> specs;
    if (paths.empty()) {
        RenderProductSpec spec;
        spec.product = resolved.IsEmpty() ? Path::Root() : resolved;
        spec.resolution = extentOf(settingsResolution);
        spec.vars.emplace_back(spec.product.IsRoot() ? Path("/LdrColor")
                                                     : spec.product.Child("LdrColor"),
                               std::string(kLdrColor));
        specs.push_back(std::move(spec));
        return specs;
    }
    for (const Path& path : paths) {
        if (!scene.Has(path) || scene.TypeOf(path) != kRenderProduct) {
            throw std::invalid_argument("'" + path.String() + "' is not a RenderProduct prim");
        }
        RenderProductSpec spec;
        spec.product = path;
        spec.camera = GetTarget(scene, path, kCameraRel);
        spec.resolution = extentOf(GetVec2(scene, path, kResolution, settingsResolution));
        for (const Path& var : TargetsOf(scene, path, kOrderedVars)) {
            if (!scene.Has(var) || scene.TypeOf(var) != kRenderVar) {
                throw std::invalid_argument("'" + var.String() + "' is not a RenderVar prim");
            }
            const std::string name = GetToken(scene, var, kSourceName, kLdrColor);
            spec.vars.emplace_back(var, name);
        }
        for (const Path& filter : TargetsOf(scene, path, kFilters)) {
            if (!scene.Has(filter) || scene.TypeOf(filter) != kSensorFilter) {
                throw std::invalid_argument("'" + filter.String() + "' is not a SensorFilter prim");
            }
            const std::string type = GetToken(scene, filter, kFilterType, "");
            const std::optional<SensorFilter> known = SensorFilterNamed(type);
            if (!known) {
                throw std::invalid_argument("Sensor filter '" + filter.String() +
                                            "' has unknown type '" + type + "'");
            }
            spec.filters.push_back(*known);
        }
        const std::vector<SensorFilter> chain =
            CheckFilterChain(spec.filters.empty() ? DefaultFilterChain() : spec.filters);
        const std::vector<std::string_view> written = VarsWrittenBy(chain);
        for (const auto& [var, name] : spec.vars) {
            const auto names = RenderTargets::VarNames();
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                throw std::invalid_argument("Render var '" + var.String() + "' asks for '" + name +
                                            "', which Ernie's rasterizer does not render");
            }
            if (std::find(written.begin(), written.end(), name) == written.end()) {
                throw std::invalid_argument("Render var '" + var.String() + "' asks for '" + name +
                                            "', which the product's sensor filters do not write");
            }
        }
        if (spec.vars.empty()) {
            spec.vars.emplace_back(path.Child("LdrColor"), std::string(kLdrColor));
        }
        specs.push_back(std::move(spec));
    }
    return specs;
}

std::vector<float> SunIrradianceOf(const Scene& scene, const Path& light,
                                   const SpectralBands& bands) {
    using namespace tokens;
    bands.Check();
    const double temperature = GetBool(scene, light, kEnableColorTemperature, false)
                                   ? GetNumber(scene, light, kColorTemperature, 6500.0)
                                   : 0.0;
    const std::vector<float>* pairs = SpectrumPairs(scene, light);
    const bool physical = scene.Get(light, kLightIrradiance) != nullptr || pairs != nullptr;
    if (physical) {
        if (pairs != nullptr) {
            const Spectrum spectrum = Spectrum::FromPairs(*pairs);
            std::vector<float> binned = spectrum.Bin(bands);
            if (scene.Get(light, kLightIrradiance) != nullptr && !spectrum.Empty()) {
                const double total =
                    spectrum.Average(spectrum.Wavelengths().front(),
                                     spectrum.Wavelengths().back()) *
                    (spectrum.Wavelengths().back() - spectrum.Wavelengths().front());
                const double scale = GetNumber(scene, light, kLightIrradiance, total) / total;
                for (float& value : binned) {
                    value = static_cast<float>(value * scale);
                }
            }
            return binned;
        }
        return BlackbodyIrradiance(bands, temperature > 0.0 ? temperature : 5772.0,
                                   GetNumber(scene, light, kLightIrradiance, 1361.0));
    }
    return RelativeSpectrum(scene, light, bands, M_PI * LightIntensity(scene, light));
}

Light LightOf(const Scene& scene, const Path& path, const SpectralBands& bands) {
    using namespace tokens;
    bands.Check();
    Light light;
    light.shadows = GetBool(scene, path, kShadowEnable, true);
    const std::string_view type = scene.TypeOf(path);
    if (type == kDistantLight) {
        light.type = LightType::Distant;
        light.direction = glm::vec3(TowardsLight(scene, path));
        light.spectrum = SunIrradianceOf(scene, path, bands);
        return light;
    }
    if (type != kSphereLight) {
        throw std::invalid_argument("'" + path.String() + "' is not a DistantLight or SphereLight");
    }
    const glm::dmat4 world = scene.WorldTransform(path);
    light.position = glm::vec3(world[3]);
    const glm::dvec3 axis = -glm::dvec3(world[2]);
    if (glm::length(axis) == 0.0) {
        throw std::invalid_argument("SphereLight '" + path.String() +
                                    "' has a degenerate transform");
    }
    light.direction = glm::vec3(glm::normalize(axis));
    light.radius =
        GetBool(scene, path, kTreatAsPoint, false) ? 0.0f : Number(scene, path, kRadius, 0.5f);
    light.coneAngle = std::clamp(Number(scene, path, kShapingConeAngle, 180.0f), 0.0f, 180.0f);
    light.coneSoftness = std::clamp(Number(scene, path, kShapingConeSoftness, 0.0f), 0.0f, 1.0f);
    if (light.radius < 0.0f) {
        throw std::invalid_argument("SphereLight '" + path.String() + "' has a negative radius");
    }
    const std::vector<float>* pairs = SpectrumPairs(scene, path);
    if (scene.Get(path, kLightPower) != nullptr || pairs != nullptr) {
        light.spectrum = LampIntensity(scene, path, bands, pairs);
        return light;
    }
    const double area = light.radius > 0.0f ? M_PI * light.radius * light.radius : 1.0;
    light.spectrum = RelativeSpectrum(scene, path, bands, LightIntensity(scene, path) * area);
    return light;
}

double OverscanOf(const PhysicalCamera& camera) {
    if (!camera.lens.HasDistortion()) {
        return 1.0;
    }
    const double f = camera.FocalLengthPixels();
    const glm::dvec2 centre = camera.PrincipalPoint();
    const double w = camera.width;
    const double h = camera.height;
    double overscan = 1.0;
    const auto include = [&](double x, double y) {
        const glm::dvec2 u = camera.lens.Undistort({(x - centre.x) / f, (centre.y - y) / f});
        const double right = w - centre.x;
        const double up = centre.y;
        const double down = h - centre.y;
        if (u.x > 0.0) {
            overscan = std::max(overscan, u.x * f / right);
        } else if (u.x < 0.0) {
            overscan = std::max(overscan, -u.x * f / centre.x);
        }
        if (u.y > 0.0) {
            overscan = std::max(overscan, u.y * f / up);
        } else if (u.y < 0.0) {
            overscan = std::max(overscan, -u.y * f / down);
        }
    };
    constexpr int kSamples = 64;
    for (int i = 0; i <= kSamples; ++i) {
        const double t = static_cast<double>(i) / kSamples;
        include(t * w, 0.0);
        include(t * w, h);
        include(0.0, t * h);
        include(w, t * h);
    }
    return overscan * 1.01;
}

CameraView RasterCameraOf(const Scene& scene, const Path& path, const PhysicalCamera& camera,
                          double overscan) {
    const glm::dmat4 world = scene.WorldTransform(path);
    const double w = camera.width;
    const double h = camera.height;
    const double focal = camera.FocalLengthPixels() / overscan;
    const glm::dvec2 centre = camera.PrincipalPoint();
    CameraView view;
    view.verticalFov = static_cast<float>(2.0 * std::atan(0.5 * h / focal));
    view.aspect = static_cast<float>(w / h);
    view.nearPlane = static_cast<float>(camera.nearPlane);
    view.farPlane = static_cast<float>(camera.farPlane);
    view.eye = glm::vec3(world[3]);
    view.forward = glm::vec3(glm::normalize(-glm::dvec3(world[2])));
    view.view = glm::mat4(glm::inverse(world));
    view.projection =
        glm::perspectiveRH_ZO(view.verticalFov, view.aspect, view.nearPlane, view.farPlane);
    view.projection[2][0] = static_cast<float>(1.0 - 2.0 * centre.x / w);
    view.projection[2][1] = static_cast<float>(2.0 * centre.y / h - 1.0);
    return view;
}

FrameParams FrameParamsOf(const Scene& scene, const RasterPrims& prims, std::uint32_t width,
                          std::uint32_t height) {
    using namespace tokens;
    if (prims.camera.IsEmpty()) {
        throw std::invalid_argument("The scene has no Camera");
    }
    if (GetToken(scene, prims.camera, kProjection, kPerspective) != kPerspective) {
        throw std::invalid_argument("Camera '" + prims.camera.String() +
                                    "': only perspective projection is supported");
    }
    FrameParams params;
    params.physical = PhysicalCameraOf(scene, prims.camera, width, height);
    params.overscan = OverscanOf(params.physical);
    params.camera = RasterCameraOf(scene, prims.camera, params.physical, params.overscan);
    const Path& settings = prims.settings;
    params.ambient = Number(scene, settings, kAmbient, params.ambient);
    params.wireframe = GetBool(scene, settings, kWireframe, params.wireframe);
    params.clearColor = glm::vec3(GetVec3(scene, settings, kClearColor, {0.0, 0.0, 0.0}));
    ShadowSettings& shadows = params.shadows;
    shadows.cascades =
        static_cast<int>(GetNumber(scene, settings, kShadowCascades, shadows.cascades));
    shadows.resolution = static_cast<std::uint32_t>(
        GetNumber(scene, settings, kShadowResolution, shadows.resolution));
    shadows.softness = Number(scene, settings, kShadowSoftness, shadows.softness);
    shadows.normalOffset = Number(scene, settings, kShadowNormalOffset, shadows.normalOffset);
    shadows.splitLambda = Number(scene, settings, kShadowSplitLambda, shadows.splitLambda);
    shadows.showCascades = GetBool(scene, settings, kShadowShowCascades, shadows.showCascades);
    if (!prims.light.IsEmpty()) {
        params.sunDirection = glm::vec3(TowardsLight(scene, prims.light));
        params.sunIrradiance = SunIrradianceOf(scene, prims.light, params.physical.bands);
        params.sunAngle = std::max(Number(scene, prims.light, kAngle, params.sunAngle), 0.0f);
        shadows.enabled = GetBool(scene, prims.light, kShadowEnable, true);
    }
    params.sun = !prims.light.IsEmpty() || prims.lights.empty();
    for (const Path& light : prims.lights) {
        params.lights.push_back(LightOf(scene, light, params.physical.bands));
    }
    return params;
}

TerrainParams TerrainParamsOf(const Scene& scene, const RasterPrims& prims) {
    using namespace tokens;
    TerrainParams params;
    params.exaggeration = Number(scene, prims.terrain, kExaggeration, 1.0f);
    const Path& settings = prims.settings;
    params.colorMode = ColorModeOf(GetToken(scene, settings, kColorMode, kShadedColormap));
    LodSettings& lod = params.lod;
    lod.enabled = GetBool(scene, settings, kLodEnable, lod.enabled);
    lod.detail = Number(scene, settings, kLodDetail, lod.detail);
    lod.morphStart = Number(scene, settings, kLodMorphStart, lod.morphStart);
    lod.freeze = GetBool(scene, settings, kLodFreeze, lod.freeze);
    lod.showLevels = GetBool(scene, settings, kLodShowLevels, lod.showLevels);
    return params;
}

MaterialSettings MaterialSettingsOf(const Scene& scene, const Path& material,
                                    const BrdfRegistry& brdfs) {
    using namespace tokens;
    MaterialSettings settings;
    settings.brdf = GetToken(scene, material, kBrdf, settings.brdf);
    const BrdfModel* model = brdfs.Find(settings.brdf);
    if (model == nullptr) {
        throw std::invalid_argument("Material '" + material.String() + "' names unknown BRDF '" +
                                    settings.brdf + "'");
    }
    for (const BrdfParam& param : model->params) {
        settings.brdfParams.push_back(
            Number(scene, material, "inputs:" + param.name, param.defaultValue));
    }
    settings.albedo = glm::vec3(GetVec3(scene, material, kAlbedo, glm::dvec3(settings.albedo)));
    settings.useAlbedoMap = GetBool(scene, material, kAlbedoMapEnable, true);
    DetailSettings& detail = settings.detail;
    detail.tileSize = Number(scene, material, kDetailTileSize, detail.tileSize);
    detail.albedoStrength = Number(scene, material, kDetailAlbedoStrength, detail.albedoStrength);
    detail.normalStrength = Number(scene, material, kDetailNormalStrength, detail.normalStrength);
    detail.directXNormals = GetBool(scene, material, kDetailDirectXNormals, detail.directXNormals);
    detail.hexTiling = GetBool(scene, material, kDetailHexTiling, detail.hexTiling);
    detail.hexRotation = Number(scene, material, kDetailHexRotation, detail.hexRotation);
    detail.hexContrast = Number(scene, material, kDetailHexContrast, detail.hexContrast);
    detail.hexSharpness = Number(scene, material, kDetailHexSharpness, detail.hexSharpness);
    return settings;
}

MeshData MeshDataOf(const Scene& scene, const Path& mesh) {
    using namespace tokens;
    MeshData data;
    const Tensor* pointsTensor = scene.Read(mesh, kPoints);
    if (pointsTensor != nullptr && pointsTensor->Location().kind == DeviceKind::Cuda) {
        if (pointsTensor->Elements() % 3 != 0) {
            throw std::invalid_argument("points must be 3-vectors");
        }
        data.points.assign(static_cast<std::size_t>(pointsTensor->Elements()), 0.0f);
    } else {
        data.points = FloatAttribute(scene, mesh, kPoints, 3).value_or(std::vector<float>());
    }
    const std::size_t count = data.points.size() / 3;
    const Tensor* counts = scene.Read(mesh, kFaceVertexCounts);
    const Tensor* indices = scene.Read(mesh, kFaceVertexIndices);
    if ((counts == nullptr) != (indices == nullptr)) {
        throw std::invalid_argument("needs both faceVertexCounts and faceVertexIndices");
    }
    if (counts != nullptr) {
        data.indices = TriangulateFaces(IntsOf(*counts, kFaceVertexCounts),
                                        IntsOf(*indices, kFaceVertexIndices), count);
    }
    if (std::optional<std::vector<float>> normals = FloatAttribute(scene, mesh, kNormals, 3)) {
        if (normals->size() == data.points.size()) {
            data.normals = std::move(*normals);
        }
    }
    if (const Tensor* colors = scene.Read(mesh, kDisplayColor)) {
        if (static_cast<std::size_t>(colors->Elements()) == data.points.size() && count > 1) {
            data.colors = FloatsOf(*colors, 3, kDisplayColor);
        }
    }
    return data;
}

InstanceData InstanceDataOf(const Scene& scene, const Path& instancer) {
    using namespace tokens;
    InstanceData data;
    data.positions = FloatAttribute(scene, instancer, kPositions, 3).value_or(std::vector<float>());
    data.orientations =
        FloatAttribute(scene, instancer, kOrientations, 4).value_or(std::vector<float>());
    data.scales = FloatAttribute(scene, instancer, kScales, 3).value_or(std::vector<float>());
    return data;
}

std::optional<Bounds3> ExtentOf(const Scene& scene, const Path& prim) {
    std::vector<float> values;
    if (const Tensor* tensor = scene.Read(prim, tokens::kExtent)) {
        if (tensor->Location().kind != DeviceKind::Cpu || tensor->Elements() != 6) {
            return std::nullopt;
        }
        values = FloatsOf(*tensor, 3, tokens::kExtent);
    } else if (const Value* value = scene.Get(prim, tokens::kExtent)) {
        if (const auto* floats = value->Get<std::vector<float>>()) {
            values = *floats;
        }
    }
    if (values.size() != 6) {
        return std::nullopt;
    }
    return Bounds3(glm::vec3(values[0], values[1], values[2]),
                   glm::vec3(values[3], values[4], values[5]));
}

std::vector<std::vector<std::uint32_t>>
InstancesByPrototype(const Scene& scene, const Path& instancer, std::size_t prototypes) {
    std::vector<std::vector<std::uint32_t>> lists(prototypes);
    const Tensor* positions = scene.Read(instancer, tokens::kPositions);
    const std::size_t count =
        positions != nullptr ? static_cast<std::size_t>(positions->Elements()) / 3 : 0;
    if (prototypes == 0) {
        return lists;
    }
    const Tensor* protoIndices = scene.Read(instancer, tokens::kProtoIndices);
    if (protoIndices == nullptr) {
        for (std::uint32_t i = 0; i < count; ++i) {
            lists[0].push_back(i);
        }
        return lists;
    }
    const std::vector<std::int64_t> indices = IntsOf(*protoIndices, tokens::kProtoIndices);
    for (std::size_t i = 0; i < std::min(count, indices.size()); ++i) {
        if (indices[i] >= 0 && static_cast<std::size_t>(indices[i]) < prototypes) {
            lists[static_cast<std::size_t>(indices[i])].push_back(static_cast<std::uint32_t>(i));
        }
    }
    return lists;
}

Image ImageOfTensor(const Tensor& tensor, ImageEncoding encoding) {
    const bool grey = tensor.Rank() == 2;
    const std::int64_t channels = grey ? 1 : (tensor.Rank() == 3 ? tensor.Shape()[2] : 0);
    const bool typed = tensor.Type() == DType::Float32 || tensor.Type() == DType::UInt8;
    if (!typed || (channels != 1 && channels != 3 && channels != 4) ||
        tensor.Location().kind != DeviceKind::Cpu || tensor.Shape()[0] < 1 ||
        tensor.Shape()[1] < 1) {
        throw std::invalid_argument(
            "expected a float32 or uint8 (rows, cols[, 1, 3 or 4]) CPU tensor");
    }
    const Tensor packed = tensor.Contiguous();
    Image image(static_cast<std::uint32_t>(tensor.Shape()[1]),
                static_cast<std::uint32_t>(tensor.Shape()[0]));
    const bool bytes = tensor.Type() == DType::UInt8;
    const auto* floats = bytes ? nullptr : packed.As<float>();
    const auto* uints = bytes ? packed.As<std::uint8_t>() : nullptr;
    const auto value = [&](std::size_t index, bool colour) {
        if (!bytes) {
            return floats[index];
        }
        const float stored = static_cast<float>(uints[index]) / 255.0f;
        return colour && encoding == ImageEncoding::Srgb ? SrgbToLinear(stored) : stored;
    };
    const auto stride = static_cast<std::size_t>(channels);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        glm::vec4& pixel = image.pixels[i];
        const std::size_t base = i * stride;
        if (channels == 1) {
            pixel = glm::vec4(glm::vec3(value(base, true)), 1.0f);
        } else {
            pixel = glm::vec4(value(base, true), value(base + 1, true), value(base + 2, true),
                              channels == 4 ? value(base + 3, false) : 1.0f);
        }
    }
    return image;
}

RasterDelegate::RasterDelegate(const VulkanContext& context, VkFormat presentFormat,
                               BrdfRegistry brdfs)
    : context_(context), renderer_(context, presentFormat, std::move(brdfs)), terrain_(renderer_),
      meshes_(renderer_) {}

RasterDelegate::~RasterDelegate() {
    context_.WaitForSubmissions();
}

RasterDelegate::Key
RasterDelegate::KeyOf(const Scene& scene, const Path& path,
                      std::initializer_list<std::string_view> attributes) const {
    Key key{path, {}};
    key.second[0] = sceneId_;
    std::size_t i = 1;
    for (const std::string_view attribute : attributes) {
        key.second.at(i++) = scene.AttributeVersion(path, attribute);
    }
    return key;
}

std::optional<std::uint32_t> RasterDelegate::MaterialId(const Path& path) const {
    const auto it = materials_.find(path);
    return it != materials_.end() ? std::optional(it->second.id) : std::nullopt;
}

std::uint32_t RasterDelegate::MaterialOf(const Scene& scene, const Path& prim) const {
    return MaterialId(BoundMaterial(scene, prim)).value_or(RasterRenderer::kDefaultMaterial);
}

void RasterDelegate::SyncMaterials(const Scene& scene) {
    using namespace tokens;
    std::set<Path> seen;
    std::vector<std::string> errors;
    for (const Path& path : scene.Prims(kMaterial)) {
        seen.insert(path);
        auto [it, added] = materials_.try_emplace(path);
        SyncedMaterial& material = it->second;
        if (added) {
            material.id = renderer_.AddMaterial();
        }
        try {
            SyncMaterial(scene, path, material);
        } catch (const std::invalid_argument& error) {
            errors.emplace_back(error.what());
        }
    }
    for (auto it = materials_.begin(); it != materials_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        renderer_.RemoveMaterial(it->second.id);
        it = materials_.erase(it);
    }
    if (!errors.empty()) {
        throw std::invalid_argument(errors.front());
    }
}

void RasterDelegate::SyncMaterial(const Scene& scene, const Path& path, SyncedMaterial& material) {
    using namespace tokens;
    renderer_.SetMaterial(material.id, MaterialSettingsOf(scene, path, renderer_.Brdfs()));
    RasterMaterial& gpu = renderer_.Material(material.id);

    const Key albedoKey =
        KeyOf(scene, path, {kAlbedoMap, kAlbedoMapOrigin, kAlbedoMapCellSize, kAlbedoMapNoData});
    if (material.albedoMap != albedoKey) {
        material.albedoMap = albedoKey;
        try {
            if (const Tensor* tensor = scene.Read(path, kAlbedoMap)) {
                const Tensor albedo = tensor->Contiguous();
                gpu.SetAlbedoMap(RasterOf(scene, path, albedo, kAlbedoMapOrigin, kAlbedoMapCellSize,
                                          kAlbedoMapNoData));
            } else if (gpu.HasAlbedoMap()) {
                gpu.ClearAlbedoMap();
            }
        } catch (const std::invalid_argument& error) {
            throw UploadError(path, kAlbedoMap, error.what());
        }
    }
    for (const bool normal : {false, true}) {
        const std::string_view attribute = normal ? kDetailNormal : kDetailAlbedo;
        std::optional<Key>& cached = normal ? material.detailNormal : material.detailAlbedo;
        const Key key = KeyOf(scene, path, {attribute});
        if (cached == key) {
            continue;
        }
        cached = key;
        try {
            std::optional<Image> image;
            if (const Tensor* tensor = scene.Read(path, attribute)) {
                image =
                    ImageOfTensor(*tensor, normal ? ImageEncoding::Linear : ImageEncoding::Srgb);
            }
            if (normal) {
                gpu.SetDetailNormal(image ? &*image : nullptr);
            } else {
                gpu.SetDetailAlbedo(image ? &*image : nullptr);
            }
        } catch (const std::invalid_argument& error) {
            throw UploadError(path, attribute, error.what());
        }
    }
}

void RasterDelegate::SyncHeightfield(const Scene& scene, const Path& terrain) {
    using namespace tokens;
    if (terrain.IsEmpty() || scene.Read(terrain, kHeights) == nullptr) {
        if (heightfield_) {
            terrain_.ClearHeightfield();
            heightfield_.reset();
        }
        return;
    }
    const Key key = KeyOf(scene, terrain, {kHeights, kOrigin, kCellSize, kNoData});
    if (heightfield_ == key) {
        return;
    }
    heightfield_ = key;
    try {
        const Tensor heights = scene.Read(terrain, kHeights)->Contiguous();
        terrain_.SetHeightfield(RasterOf(scene, terrain, heights, kOrigin, kCellSize, kNoData));
    } catch (const std::invalid_argument& error) {
        throw UploadError(terrain, kHeights, error.what());
    }
}

CudaInterop* RasterDelegate::Cuda(std::string* why) {
    if (!cudaTried_) {
        cudaTried_ = true;
        cuda_ = CudaInterop::Create(context_, &cudaWhy_);
    }
    if (cuda_ == nullptr && why != nullptr) {
        *why = cudaWhy_;
    }
    return cuda_.get();
}

CudaInterop& RasterDelegate::RequireCuda() {
    std::string why;
    CudaInterop* cuda = Cuda(&why);
    if (cuda == nullptr) {
        throw std::invalid_argument("CUDA arrays need CUDA interop, which is unavailable: " + why);
    }
    if (!meshesOnCuda_) {
        meshes_.SetCudaInterop(cuda);
        meshesOnCuda_ = true;
    }
    return *cuda;
}

void RasterDelegate::SetMeshPoints(const Scene& scene, const Path& path, std::uint32_t handle) {
    const Tensor* tensor = scene.Read(path, tokens::kPoints);
    if (tensor != nullptr && tensor->Location().kind == DeviceKind::Cuda) {
        RequireCuda();
        meshes_.SetPoints(handle, *tensor, ExtentOf(scene, path));
        return;
    }
    const std::optional<std::vector<float>> points =
        FloatAttribute(scene, path, tokens::kPoints, 3);
    meshes_.SetPoints(handle, points.value_or(std::vector<float>()));
}

void RasterDelegate::SyncMeshes(const Scene& scene) {
    using namespace tokens;
    std::set<Path> seen;
    std::vector<std::string> errors;
    for (const Path& path : scene.Prims(kMesh)) {
        seen.insert(path);
        const Key key =
            KeyOf(scene, path, {kFaceVertexCounts, kFaceVertexIndices, kNormals, kDisplayColor});
        const std::uint64_t points = scene.AttributeVersion(path, kPoints);
        const auto it = syncedMeshes_.find(path);
        if (it != syncedMeshes_.end() && it->second.key == key) {
            SyncedMesh& synced = it->second;
            if (synced.points == points) {
                continue;
            }
            synced.points = points;
            if (synced.handle != 0) {
                try {
                    SetMeshPoints(scene, path, synced.handle);
                    continue;
                } catch (const std::invalid_argument&) {
                }
            }
        }
        SyncedMesh synced;
        synced.key = key;
        synced.points = points;
        if (it != syncedMeshes_.end() && it->second.handle != 0) {
            meshes_.RemoveMesh(it->second.handle);
        }
        try {
            MeshData data = MeshDataOf(scene, path);
            synced.hasColors = !data.colors.empty();
            synced.tint = ConstantDisplayColor(scene, path).value_or(glm::vec3(0.5f));
            synced.handle = meshes_.AddMesh(std::move(data));
            const Tensor* tensor = scene.Read(path, kPoints);
            if (tensor != nullptr && tensor->Location().kind == DeviceKind::Cuda) {
                SetMeshPoints(scene, path, synced.handle);
            }
        } catch (const std::invalid_argument& error) {
            if (synced.handle != 0) {
                meshes_.RemoveMesh(synced.handle);
                synced.handle = 0;
            }
            errors.push_back(Describe(path, "mesh", error));
        }
        syncedMeshes_[path] = synced;
    }
    for (auto it = syncedMeshes_.begin(); it != syncedMeshes_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second.handle != 0) {
            meshes_.RemoveMesh(it->second.handle);
        }
        it = syncedMeshes_.erase(it);
    }
    if (!errors.empty()) {
        throw std::invalid_argument("Cannot upload " + errors.front());
    }
}

void RasterDelegate::SyncInstancers(const Scene& scene) {
    using namespace tokens;
    std::set<Path> seen;
    std::vector<std::string> errors;
    for (const Path& path : scene.Prims(kPointInstancer)) {
        seen.insert(path);
        const Key key = KeyOf(scene, path, {kPositions, kOrientations, kScales});
        auto [it, added] = instancers_.try_emplace(path);
        SyncedInstancer& synced = it->second;
        if (!added && synced.key == key) {
            continue;
        }
        synced.key = key;
        try {
            const Tensor* positions = scene.Read(path, kPositions);
            if (positions != nullptr && positions->Location().kind == DeviceKind::Cuda) {
                CudaInterop& cuda = RequireCuda();
                const auto onDevice = [&](std::string_view attribute) {
                    const Tensor* tensor = scene.Read(path, attribute);
                    if (tensor == nullptr) {
                        return Tensor();
                    }
                    return tensor->Location().kind == DeviceKind::Cuda ? *tensor
                                                                       : cuda.Upload(*tensor);
                };
                DeviceInstanceData data;
                data.positions = *positions;
                data.orientations = onDevice(kOrientations);
                data.scales = onDevice(kScales);
                data.extent = ExtentOf(scene, path);
                if (synced.handle == 0) {
                    synced.handle = meshes_.AddInstancer({});
                }
                meshes_.SetInstances(synced.handle, data);
            } else {
                InstanceData data = InstanceDataOf(scene, path);
                if (synced.handle == 0) {
                    synced.handle = meshes_.AddInstancer(std::move(data));
                } else {
                    meshes_.SetInstances(synced.handle, std::move(data));
                }
            }
        } catch (const std::invalid_argument& error) {
            if (synced.handle != 0) {
                meshes_.RemoveInstancer(synced.handle);
                synced.handle = 0;
            }
            errors.push_back(Describe(path, "instancer", error));
        }
    }
    for (auto it = instancers_.begin(); it != instancers_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second.handle != 0) {
            meshes_.RemoveInstancer(it->second.handle);
        }
        it = instancers_.erase(it);
    }
    if (!errors.empty()) {
        throw std::invalid_argument("Cannot upload " + errors.front());
    }
}

void RasterDelegate::Sync(const Scene& scene) {
    if (scene.Id() != sceneId_) {
        sceneId_ = scene.Id();
        cursor_.version = scene.Version();
    } else if (scene.Changes(cursor_).empty()) {
        return;
    }
    std::vector<std::string> errors;
    const auto attempt = [&errors](auto&& step) {
        try {
            step();
        } catch (const std::invalid_argument& error) {
            errors.emplace_back(error.what());
        }
    };
    attempt([&] { SyncMaterials(scene); });
    attempt([&] { SyncHeightfield(scene, ResolveRasterPrims(scene, {}).terrain); });
    attempt([&] { SyncMeshes(scene); });
    attempt([&] { SyncInstancers(scene); });
    if (!errors.empty()) {
        std::string message = errors.front();
        for (std::size_t i = 1; i < errors.size(); ++i) {
            message += "; " + errors[i];
        }
        throw std::invalid_argument(message);
    }
}

std::vector<MeshItem> RasterDelegate::Items(const Scene& scene) const {
    std::set<Path> prototypes;
    for (const auto& [path, synced] : instancers_) {
        for (const Path& prototype : TargetsOf(scene, path, tokens::kPrototypes)) {
            prototypes.insert(prototype);
        }
    }
    const auto isPrototype = [&prototypes](const Path& path) {
        return std::any_of(prototypes.begin(), prototypes.end(),
                           [&path](const Path& prototype) { return path.HasPrefix(prototype); });
    };
    const auto shade = [this, &scene](const Path& mesh, const SyncedMesh& synced, MeshItem& item) {
        item.material = MaterialOf(scene, mesh);
        const bool bound = item.material != RasterRenderer::kDefaultMaterial;
        item.tint = bound ? glm::vec3(1.0f) : synced.tint;
        item.vertexColors = !bound;
    };

    std::vector<MeshItem> items;
    for (const auto& [path, synced] : syncedMeshes_) {
        if (synced.handle == 0 || isPrototype(path) || !scene.IsVisible(path)) {
            continue;
        }
        MeshItem item;
        item.mesh = synced.handle;
        item.transform = glm::mat4(scene.WorldTransform(path));
        item.primId = PrimIdOf(path);
        shade(path, synced, item);
        items.push_back(std::move(item));
    }
    for (const auto& [path, synced] : instancers_) {
        if (synced.handle == 0 || !scene.IsVisible(path)) {
            continue;
        }
        const std::vector<Path> targets = TargetsOf(scene, path, tokens::kPrototypes);
        const std::vector<std::vector<std::uint32_t>> lists =
            InstancesByPrototype(scene, path, targets.size());
        const glm::mat4 world(scene.WorldTransform(path));
        for (std::size_t k = 0; k < targets.size(); ++k) {
            const auto mesh = syncedMeshes_.find(targets[k]);
            if (mesh == syncedMeshes_.end() || mesh->second.handle == 0 || lists[k].empty()) {
                continue;
            }
            MeshItem item;
            item.mesh = mesh->second.handle;
            item.transform = world;
            item.prototype = glm::mat4(scene.LocalTransform(targets[k]));
            item.instancer = synced.handle;
            item.instances = lists[k];
            item.primId = PrimIdOf(path);
            shade(targets[k], mesh->second, item);
            items.push_back(std::move(item));
        }
    }
    return items;
}

void RasterDelegate::Update(const Scene& scene, const Path& settings, VkExtent2D extent) {
    UpdatePrims(scene, ResolveRasterPrims(scene, settings), extent);
}

void RasterDelegate::UpdatePrims(const Scene& scene, const RasterPrims& prims, VkExtent2D extent,
                                 std::span<const SensorFilter> filters) {
    context_.WaitForSubmissions();
    Sync(scene);
    prims_ = prims;
    FrameParams frame = FrameParamsOf(scene, prims_, extent.width, extent.height);
    frame.filters.assign(filters.begin(), filters.end());
    TerrainParams terrain = TerrainParamsOf(scene, prims_);
    terrain.material = MaterialOf(scene, prims_.terrain);
    terrain.primId = prims_.terrain.IsEmpty() ? 0u : PrimIdOf(prims_.terrain);
    terrain_.SetParams(terrain);
    meshes_.SetItems(Items(scene));
    RasterPart* parts[] = {&terrain_, &meshes_};
    renderer_.Update(frame, parts);
}

std::uint32_t RasterDelegate::PrimIdOf(const Path& path) const {
    const auto [it, added] =
        primIds_.try_emplace(path, static_cast<std::uint32_t>(primPaths_.size() + 1));
    if (added) {
        primPaths_.push_back(path);
    }
    return it->second;
}

Path RasterDelegate::PrimPath(std::uint32_t id) const {
    return id >= 1 && id <= primPaths_.size() ? primPaths_[id - 1] : Path();
}

std::vector<TimelinePoint> RasterDelegate::SubmitWaits() const {
    if (cuda_ == nullptr || cuda_->VulkanWait().semaphore == VK_NULL_HANDLE) {
        return {};
    }
    return {cuda_->VulkanWait()};
}

void RasterDelegate::Record(VkCommandBuffer commands, const RenderingTarget& target) {
    const VkExtent2D extent = renderer_.Extent();
    if (viewTargets_ == nullptr || viewTargets_->Extent().width != extent.width ||
        viewTargets_->Extent().height != extent.height ||
        viewTargets_->Bands() != renderer_.Bands().count) {
        if (extent.width == 0 || extent.height == 0) {
            BeginRendering(commands, target);
            vkCmdEndRendering(commands);
            return;
        }
        vkDeviceWaitIdle(context_.Device());
        viewTargets_ =
            std::make_unique<RenderTargets>(context_, extent, renderer_.Bands().count, false);
    }
    const RasterPart* parts[] = {&terrain_, &meshes_};
    renderer_.Record(commands, *viewTargets_, parts, &target);
}

AttributeBinding RasterDelegate::Bind(const Scene& scene, const Path& path,
                                      std::string_view attribute) {
    using namespace tokens;
    const bool mesh = scene.Has(path) && scene.TypeOf(path) == kMesh;
    const bool instancer = scene.Has(path) && scene.TypeOf(path) == kPointInstancer;
    if (!(mesh && attribute == kPoints) &&
        !(instancer &&
          (attribute == kPositions || attribute == kOrientations || attribute == kScales))) {
        throw std::invalid_argument("Only a Mesh's points and a PointInstancer's positions, "
                                    "orientations and scales can be bound, not " +
                                    std::string(attribute) + " of '" + path.String() + "'");
    }
    RequireCuda();
    Sync(scene);
    const bool synced = mesh ? syncedMeshes_.contains(path) && syncedMeshes_.at(path).handle != 0
                             : instancers_.contains(path) && instancers_.at(path).handle != 0;
    if (!synced) {
        throw std::invalid_argument("'" + path.String() + "' did not sync, so it cannot be bound");
    }
    AttributeBinding binding;
    binding.delegate_ = this;
    binding.path_ = path;
    binding.attribute_ = attribute;
    binding.extent_ = ExtentOf(scene, path);
    return binding;
}

namespace {

InstanceArray InstanceArrayOf(const std::string& attribute) {
    return attribute == tokens::kPositions      ? InstanceArray::Positions
           : attribute == tokens::kOrientations ? InstanceArray::Orientations
                                                : InstanceArray::Scales;
}

}  // namespace

std::uint32_t RasterDelegate::BoundHandle(const AttributeBinding& binding) const {
    if (binding.attribute_ == tokens::kPoints) {
        const auto it = syncedMeshes_.find(binding.path_);
        if (it == syncedMeshes_.end() || it->second.handle == 0) {
            throw std::invalid_argument("Bound mesh '" + binding.path_.String() + "' is gone");
        }
        return it->second.handle;
    }
    const auto it = instancers_.find(binding.path_);
    if (it == instancers_.end() || it->second.handle == 0) {
        throw std::invalid_argument("Bound instancer '" + binding.path_.String() + "' is gone");
    }
    return it->second.handle;
}

Tensor RasterDelegate::MapBinding(const AttributeBinding& binding, std::uintptr_t stream) {
    CudaInterop& cuda = RequireCuda();
    const std::uint32_t handle = BoundHandle(binding);
    const BoundSlot slot =
        binding.attribute_ == tokens::kPoints
            ? meshes_.MapPoints(handle, binding.extent_)
            : meshes_.MapInstances(handle, InstanceArrayOf(binding.attribute_), binding.extent_);
    cuda.WaitForVulkan(stream, slot.readDone);
    return cuda.View(
        slot.buffer, DType::Float32,
        {static_cast<std::int64_t>(slot.count), static_cast<std::int64_t>(slot.components)});
}

void RasterDelegate::UnmapBinding(const AttributeBinding& binding, std::uintptr_t stream) {
    const std::uint32_t handle = BoundHandle(binding);
    if (binding.attribute_ == tokens::kPoints) {
        meshes_.UnmapPoints(handle);
    } else {
        meshes_.UnmapInstances(handle, InstanceArrayOf(binding.attribute_));
    }
    RequireCuda().SignalVulkan(stream);
}

Tensor AttributeBinding::Map(std::uintptr_t stream) {
    if (delegate_ == nullptr) {
        throw std::invalid_argument("AttributeBinding::Map on an empty binding");
    }
    Tensor tensor = delegate_->MapBinding(*this, stream);
    mapped_ = true;
    return tensor;
}

void AttributeBinding::Unmap(std::uintptr_t stream) {
    if (delegate_ == nullptr || !mapped_) {
        return;
    }
    delegate_->UnmapBinding(*this, stream);
    mapped_ = false;
}

std::shared_ptr<ProductFrame> RasterDelegate::AcquireFrame(const Path& product, VkExtent2D extent,
                                                           int bands) {
    std::vector<std::shared_ptr<ProductFrame>>& frames = frames_[product];
    const bool shared = cuda_ != nullptr;
    std::erase_if(frames, [&](const std::shared_ptr<ProductFrame>& frame) {
        const RenderTargets& targets = *frame->targets;
        return frame.use_count() == 1 && frame->arrayMaps == 0 &&
               (targets.Extent().width != extent.width ||
                targets.Extent().height != extent.height || targets.Bands() != bands ||
                targets.Shared() != shared);
    });
    for (const std::shared_ptr<ProductFrame>& frame : frames) {
        if (frame.use_count() == 1 && frame->arrayMaps == 0) {
            return frame;
        }
    }
    auto frame = std::make_shared<ProductFrame>();
    frame->context = &context_;
    frame->cuda = cuda_.get();
    frame->linearPool = &linearPool_;
    frame->targets = std::make_unique<RenderTargets>(context_, extent, bands, shared);
    frames.push_back(frame);
    return frame;
}

StepOperation RasterDelegate::StepAsync(const Scene& scene, std::span<const Path> products,
                                        const Path& settings) {
    context_.WaitForSubmissions();
    CudaInterop* cuda = Cuda();
    const RasterPrims base = ResolveRasterPrims(scene, settings);
    const std::vector<RenderProductSpec> specs = ResolveRenderProducts(scene, products, settings);
    RenderProductSetOutputs outputs;
    std::uint64_t submission = 0;
    for (const RenderProductSpec& spec : specs) {
        RasterPrims prims = base;
        if (!spec.camera.IsEmpty()) {
            prims.camera = spec.camera;
        }
        if (prims.camera.IsEmpty()) {
            throw std::invalid_argument("The scene has no Camera");
        }
        const int bands =
            PhysicalCameraOf(scene, prims.camera, spec.resolution.width, spec.resolution.height)
                .bands.count;
        std::shared_ptr<ProductFrame> frame = AcquireFrame(spec.product, spec.resolution, bands);
        UpdatePrims(scene, prims, spec.resolution, spec.filters);
        const std::vector<TimelinePoint> waits = SubmitWaits();
        std::vector<TimelinePoint> signals;
        if (cuda != nullptr) {
            signals.push_back(cuda->NextVulkanSignal());
        }
        RenderTargets& targets = *frame->targets;
        const RasterPart* parts[] = {&terrain_, &meshes_};
        submission = context_.SubmitAsync(
            [&](VkCommandBuffer commands) { renderer_.Record(commands, targets, parts); }, waits,
            signals);
        if (!signals.empty()) {
            meshes_.MarkRead(signals.front().value);
        }
        frame->submission = submission;
        RenderProductOutput& output = outputs[spec.product];
        output.product = spec.product;
        for (const auto& [path, name] : spec.vars) {
            output.vars.emplace(name, RenderVar(frame, path, name));
        }
    }
    return StepOperation(context_, submission, std::move(outputs));
}

RenderProductSetOutputs RasterDelegate::Step(const Scene& scene, std::span<const Path> products,
                                             const Path& settings) {
    return StepAsync(scene, products, settings).Fetch();
}

Tensor RasterDelegate::Render(const Scene& scene, const Path& settings, Device device) {
    const bool cuda = device.kind == DeviceKind::Cuda;
    if (cuda) {
        std::string why;
        CudaInterop* interop = Cuda(&why);
        if (interop == nullptr) {
            throw std::invalid_argument("A CUDA render needs CUDA interop: " + why);
        }
        if (interop->Device() != device.id) {
            throw std::invalid_argument("The renderer's Vulkan device is CUDA device " +
                                        std::to_string(interop->Device()));
        }
    }
    StepOperation step = StepAsync(scene, {}, settings);
    RenderProductSetOutputs outputs = cuda ? step.Outputs() : step.Fetch();
    for (auto& [path, output] : outputs) {
        if (const auto it = output.vars.find(std::string(render_vars::kLdrColor));
            it != output.vars.end()) {
            return it->second.Map(cuda ? MapDevice::Cuda : MapDevice::Cpu).tensor;
        }
    }
    throw std::invalid_argument("The render products have no LdrColor render var");
}

}  // namespace ernie
