#include "settings.h"

#include "terraingen/craters.h"
#include "terraingen/spatial/cells.h"

#include "ernie/ernie.h"

#include <cmath>
#include <exception>
#include <stdexcept>

namespace terraingen::viewer {

NoiseLayer LayerSettings::EffectiveLayer() const {
    NoiseLayer effective = layer;
    if (useHurst) {
        effective.fractal.gain = GainFromHurst(hurst, layer.fractal.lacunarity);
    }
    return effective;
}

ernie::MaterialSettings DefaultMaterial() {
    ernie::MaterialSettings material;
    material.albedo = glm::vec3(0.5f);
    return material;
}

LayerSettings DefaultLayer() {
    LayerSettings settings;
    settings.layer.fractal.seed = 1u;
    settings.layer.fractal.wavelength = 500.0f;
    settings.layer.fractal.amplitude = 50.0f;
    return settings;
}

namespace {

/// The square region a terrain of `settings` covers, centred on the origin.
Bounds2d TerrainBounds(const TerrainSettings& settings) {
    const double halfX = 0.5 * static_cast<double>(settings.width) * settings.pixelSize;
    const double halfY = 0.5 * static_cast<double>(settings.height) * settings.pixelSize;
    return {-halfX, -halfY, halfX, halfY};
}

std::string ValidateCraters(const TerrainSettings& settings) {
    const CraterSettings& craters = settings.craters;
    if (!craters.enabled) {
        return {};
    }
    for (std::size_t n = 0; n < craters.manual.size(); ++n) {
        const Crater& crater = craters.manual[n];
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y) &&
              std::isfinite(crater.diameter) && crater.diameter > 0.0 &&
              std::isfinite(crater.ageGyr) && crater.ageGyr >= 0.0)) {
            return "Craters: manual crater " + std::to_string(n + 1) +
                   " needs a positive diameter and a non-negative age";
        }
    }
    if (!craters.generate) {
        try {
            ValidateCraterApplyOptions(craters.apply);
            ValidateRimSmoothing(craters.rims);
        } catch (const std::invalid_argument& error) {
            return std::string("Craters: ") + error.what();
        }
        return {};
    }
    if (!(std::isfinite(craters.minDiameter) && craters.minDiameter > 0.0)) {
        return "Craters: minimum diameter must be positive";
    }
    if (!(std::isfinite(craters.maxDiameter) && craters.maxDiameter > craters.minDiameter)) {
        return "Craters: maximum diameter must exceed the minimum";
    }
    try {
        ValidateCraterSpacing(craters.spacing);
        ValidateCraterApplyOptions(craters.apply);
        ValidateRimSmoothing(craters.rims);
    } catch (const std::invalid_argument& error) {
        return std::string("Craters: ") + error.what();
    }
    if (craters.density == CraterDensityModel::LunarSurface && craters.minDiameter < 2.5) {
        return "Craters: the lunar surface model starts at 2.5 m";
    }
    double expected = 0.0;
    try {
        expected = ExpectedCraterCount(settings);
    } catch (const std::exception& error) {
        return std::string("Craters: ") + error.what();
    }
    if (expected > kMaxViewerCraters) {
        return "Craters: about " + std::to_string(static_cast<long long>(expected)) +
               " expected; raise the minimum diameter";
    }
    return {};
}

}  // namespace

CraterModels BuildCraterModels(const CraterSettings& settings) {
    CraterModels models;
    switch (settings.density) {
    case CraterDensityModel::LunarSurface: {
        const SurfaceModel surface = LunarSurface(settings.surfaceAgeGyr);
        models.density = surface.density;
        models.ages = surface.ages;
        break;
    }
    case CraterDensityModel::Trask:
        models.density = std::make_shared<PowerLawSfd>(TraskEquilibrium());
        break;
    case CraterDensityModel::Hartmann:
        models.density = std::make_shared<PowerLawSfd>(HartmannEquilibrium());
        break;
    case CraterDensityModel::PowerLaw:
        models.density =
            std::make_shared<PowerLawSfd>(settings.powerLawCoefficient, settings.powerLawExponent);
        break;
    }
    if (!models.ages) {
        models.ages = std::make_shared<UniformAges>(settings.lifetimeGyr);
    }
    if (settings.degrade) {
        models.degradation = std::make_shared<FreshnessDegradation>(models.ages);
    } else {
        models.degradation = std::make_shared<NoDegradation>();
    }
    switch (settings.profile) {
    case CraterProfileModel::FassettThomson:
        models.profile = std::make_shared<FassettThomsonProfile>(FassettThomsonRim::Original);
        break;
    case CraterProfileModel::FassettThomsonPlateau:
        models.profile = std::make_shared<FassettThomsonProfile>(FassettThomsonRim::Plateau);
        break;
    case CraterProfileModel::MartinParkesDunstan:
        models.profile = std::make_shared<MartinParkesDunstanProfile>();
        break;
    }
    return models;
}

double MinDiameterForCount(const TerrainSettings& settings) {
    const CraterSettings& craters = settings.craters;
    const CraterModels models = BuildCraterModels(craters);
    const Bounds2d bounds = TerrainBounds(settings);
    const double area = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY);
    const double lowest = std::fmax(models.density->ValidRange().min, 1e-3);
    return terraingen::MinDiameterForCount(*models.density, area,
                                           static_cast<double>(craters.maxCraters), lowest,
                                           craters.maxDiameter);
}

double ExpectedCraterCount(const TerrainSettings& settings) {
    const CraterSettings& craters = settings.craters;
    if (!craters.enabled) {
        return 0.0;
    }
    const auto manual = static_cast<double>(craters.manual.size());
    if (!craters.generate) {
        return manual;
    }
    const CraterModels models = BuildCraterModels(craters);
    const Bounds2d bounds = TerrainBounds(settings);
    const double area = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY);
    return manual + models.density->ExpectedCount(area, craters.minDiameter, craters.maxDiameter);
}

SyntheticTerrainParams TerrainSettings::EffectiveTerrain() const {
    SyntheticTerrainParams params;
    params.layers.clear();
    for (const LayerSettings& layer : layers) {
        params.layers.push_back(layer.EffectiveLayer());
    }
    return params;
}

std::string ValidateSettings(const TerrainSettings& settings) {
    if (settings.width < 2 || settings.height < 2) {
        return "Width and height must be at least 2 cells";
    }
    if (settings.width > kMaxGridSize || settings.height > kMaxGridSize) {
        return "Width and height must be at most " + std::to_string(kMaxGridSize) + " cells";
    }
    if (!(std::isfinite(settings.pixelSize) && settings.pixelSize > 0.0)) {
        return "Pixel size must be positive";
    }
    for (std::size_t i = 0; i < settings.layers.size(); ++i) {
        if (settings.layers[i].useHurst && !std::isfinite(settings.layers[i].hurst)) {
            return "Layer " + std::to_string(i + 1) + ": Hurst exponent must be finite";
        }
    }
    try {
        ValidateSyntheticTerrain(settings.EffectiveTerrain());
    } catch (const std::invalid_argument& error) {
        return error.what();
    }
    return ValidateCraters(settings);
}

Raster<float> GenerateTerrain(const TerrainSettings& settings) {
    const std::string error = ValidateSettings(settings);
    if (!error.empty()) {
        throw std::invalid_argument(error);
    }
    Raster<float> raster = GenerateSyntheticTerrain(
        settings.width, settings.height, settings.pixelSize, settings.EffectiveTerrain());
    const CraterSettings& craters = settings.craters;
    if (craters.enabled) {
        const CraterModels models = BuildCraterModels(craters);
        const Bounds2d bounds = TerrainBounds(settings);
        CraterList manual;
        for (Crater crater : craters.manual) {
            crater.manual = true;
            if (crater.id == 0) {
                crater.id = ManualCraterId(crater.x, crater.y, crater.diameter);
            }
            manual.Append(crater);
        }
        CraterList list;
        if (craters.generate) {
            list =
                GenerateCraters(PlanarCells(), bounds, *models.density, *models.ages, craters.seed,
                                {.dMin = craters.minDiameter,
                                 .dMax = craters.maxDiameter,
                                 .marginRadii = models.profile->ReachRadii(),
                                 .spacing = craters.spacing,
                                 .manual = manual,
                                 .maxCraters = craters.maxCraters});
        } else {
            for (std::size_t n = 0; n < manual.Size(); ++n) {
                const Crater crater = manual.At(n);
                const double margin = 0.5 * models.profile->ReachRadii() * crater.diameter;
                if (bounds.Expanded(margin).Contains(crater.x, crater.y)) {
                    list.Append(crater);
                }
            }
        }
        ApplyCraters(raster, list, *models.profile, *models.degradation, craters.apply);
        SmoothCraterRims(raster, list, craters.rims);
    }
    return raster;
}

glm::vec3 SunDirection(float azimuthDegrees, float elevationDegrees) {
    const float azimuth = glm::radians(azimuthDegrees);
    const float elevation = glm::radians(elevationDegrees);
    return {std::sin(azimuth) * std::cos(elevation), std::cos(azimuth) * std::cos(elevation),
            std::sin(elevation)};
}

ernie::RasterView ViewOf(const Raster<float>& raster) {
    const GeoTransform& t = raster.Transform();
    if (t.rowRotation != 0.0 || t.colRotation != 0.0 || !(t.pixelWidth > 0.0) ||
        t.pixelHeight != -t.pixelWidth) {
        throw std::invalid_argument("Terrain must be north-up with square pixels to render");
    }
    return {raster.Values().data(), raster.Width(), raster.Height(), 0, t.originX, t.originY,
            t.pixelWidth,           raster.NoData()};
}

ernie::MaterialSettings SurfaceViewSettings::Effective(const ernie::BrdfRegistry& registry) const {
    ernie::MaterialSettings effective = surface;
    if (const ernie::BrdfModel* model = registry.Find(surface.brdf)) {
        const auto edited = brdfParams.find(surface.brdf);
        effective.brdfParams = edited != brdfParams.end()
                                   ? ernie::ResolveBrdfParams(*model, edited->second)
                                   : model->Defaults();
    }
    return effective;
}

void AuthorTerrain(ernie::Scene& scene, std::shared_ptr<const Raster<float>> raster) {
    const ernie::RasterView view = ViewOf(*raster);
    ernie::Tensor heights(
        const_cast<float*>(view.data), ernie::DType::Float32,
        {static_cast<std::int64_t>(view.height), static_cast<std::int64_t>(view.width)},
        {static_cast<std::int64_t>(view.Stride()), 1}, {}, raster);
    ernie::Prim terrain = scene.Define(kTerrainPrim, ernie::tokens::kTerrain);
    terrain.Write(ernie::tokens::kHeights, heights)
        .Set(ernie::tokens::kOrigin, glm::dvec2(view.originX, view.originY))
        .Set(ernie::tokens::kCellSize, view.cellSize);
    if (view.noData) {
        terrain.Set(ernie::tokens::kNoData, *view.noData);
    } else {
        scene.Clear(kTerrainPrim, ernie::tokens::kNoData);
    }
}

std::optional<std::pair<glm::vec3, glm::vec3>>
TerrainBounds(const Raster<float>& raster, const RasterStats& stats, float exaggeration) {
    if (stats.validCount == 0) {
        return std::nullopt;
    }
    const ernie::RasterView view = ViewOf(raster);
    const double half = 0.5 * view.cellSize;
    const glm::vec3 a(static_cast<float>(view.originX + half),
                      static_cast<float>(view.originY - half),
                      static_cast<float>(stats.min) * exaggeration);
    const glm::vec3 b(
        static_cast<float>(view.originX + static_cast<double>(view.width) * view.cellSize - half),
        static_cast<float>(view.originY - static_cast<double>(view.height) * view.cellSize + half),
        static_cast<float>(stats.max) * exaggeration);
    return std::pair(glm::min(a, b), glm::max(a, b));
}

ernie::Tensor TensorOf(std::shared_ptr<const ernie::Image> image) {
    void* data = const_cast<glm::vec4*>(image->pixels.data());
    std::vector<std::int64_t> shape{static_cast<std::int64_t>(image->height),
                                    static_cast<std::int64_t>(image->width), 4};
    return ernie::Tensor(data, ernie::DType::Float32, std::move(shape), {}, {}, std::move(image));
}

void AuthorView(ernie::Scene& scene, const ViewSettings& view, const ernie::OrbitController& orbit,
                const ernie::BrdfRegistry& brdfs) {
    using namespace ernie::tokens;
    ernie::Camera camera;
    orbit.Apply(camera);
    camera.exposureStops = view.camera.exposureStops;
    camera.exposureTime = view.camera.shutterSeconds;
    camera.iso = view.camera.iso;
    camera.fStop = view.camera.fStop;
    camera.bands.count = view.camera.bands;
    ernie::SetCamera(scene, kCameraPrim, camera);

    const ernie::MaterialSettings surface = view.material.Effective(brdfs);
    scene.Define(kSunPrim, kDistantLight)
        .Set(kTransform, ernie::DistantLightTransform(glm::dvec3(
                             SunDirection(view.sunAzimuthDegrees, view.sunElevationDegrees))))
        .Set(kIntensity, view.material.exposure)
        .Set(kShadowEnable, view.shadows.enabled);
    if (view.camera.physicalSun) {
        scene.Set(kSunPrim, kLightIrradiance, 1361.0);
    } else {
        scene.Clear(kSunPrim, kLightIrradiance);
    }

    ernie::Prim material = scene.Define(kSurfacePrim, kMaterial);
    material.Set(kBrdf, surface.brdf)
        .Set(kAlbedo, surface.albedo)
        .Set(kAlbedoMapEnable, surface.useAlbedoMap);
    if (const ernie::BrdfModel* model = brdfs.Find(surface.brdf)) {
        for (std::size_t i = 0; i < model->params.size(); ++i) {
            material.Set("inputs:" + model->params[i].name, surface.brdfParams[i]);
        }
    }
    const ernie::DetailSettings& detail = surface.detail;
    material.Set(kDetailTileSize, detail.tileSize)
        .Set(kDetailAlbedoStrength, detail.albedoStrength)
        .Set(kDetailNormalStrength, detail.normalStrength)
        .Set(kDetailDirectXNormals, detail.directXNormals)
        .Set(kDetailHexTiling, detail.hexTiling)
        .Set(kDetailHexRotation, detail.hexRotation)
        .Set(kDetailHexContrast, detail.hexContrast)
        .Set(kDetailHexSharpness, detail.hexSharpness);

    scene.Define(kTerrainPrim, kTerrain)
        .Set(kExaggeration, view.exaggeration)
        .Set(kMaterialBinding, ernie::Path(kSurfacePrim));

    std::string_view colorMode = kShadedColormap;
    if (view.colorMode == ernie::TerrainColorMode::Hillshade) {
        colorMode = kHillshade;
    } else if (view.colorMode == ernie::TerrainColorMode::Colormap) {
        colorMode = kColormap;
    }
    scene.Define(kSettingsPrim, kRenderSettings)
        .Set(kCameraRel, ernie::Path(kCameraPrim))
        .Set(kAmbient, view.ambient)
        .Set(kColorMode, colorMode)
        .Set(kWireframe, view.wireframe)
        .Set(kClearColor, glm::vec3(view.background[0], view.background[1], view.background[2]))
        .Set(kShadowCascades, view.shadows.cascades)
        .Set(kShadowResolution, static_cast<int>(view.shadows.resolution))
        .Set(kShadowSoftness, view.shadows.softness)
        .Set(kShadowNormalOffset, view.shadows.normalOffset)
        .Set(kShadowSplitLambda, view.shadows.splitLambda)
        .Set(kShadowShowCascades, view.shadows.showCascades)
        .Set(kLodEnable, view.lod.enabled)
        .Set(kLodDetail, view.lod.detail)
        .Set(kLodMorphStart, view.lod.morphStart)
        .Set(kLodFreeze, view.lod.freeze)
        .Set(kLodShowLevels, view.lod.showLevels);
}

std::variant<ernie::Image, std::string> LoadDetailTexture(const std::string& path, bool normalMap) {
    if (path.empty()) {
        return std::string("No file given");
    }
    try {
        return ernie::LoadImage(path, normalMap ? ernie::ImageEncoding::Linear
                                                : ernie::ImageEncoding::Srgb);
    } catch (const std::exception& error) {
        return std::string(error.what());
    }
}

}  // namespace terraingen::viewer
