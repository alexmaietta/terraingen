#include "settings.h"

#include "terraingen/features/craters/apply.h"
#include "terraingen/features/craters/lunar_cratering.h"
#include "terraingen/features/craters/population.h"
#include "terraingen/spatial/cells.h"

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
    if (!(std::isfinite(craters.minDiameter) && craters.minDiameter > 0.0)) {
        return "Craters: minimum diameter must be positive";
    }
    if (!(std::isfinite(craters.maxDiameter) && craters.maxDiameter > craters.minDiameter)) {
        return "Craters: maximum diameter must exceed the minimum";
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
        models.density = std::make_shared<PowerLaw>(TraskEquilibrium());
        break;
    case CraterDensityModel::Hartmann:
        models.density = std::make_shared<PowerLaw>(HartmannEquilibrium());
        break;
    case CraterDensityModel::PowerLaw:
        models.density =
            std::make_shared<PowerLaw>(settings.powerLawCoefficient, settings.powerLawExponent);
        break;
    }
    if (!models.ages) {
        models.ages = std::make_shared<UniformFreshness>(settings.lifetimeGyr);
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

double ExpectedCraterCount(const TerrainSettings& settings) {
    const CraterSettings& craters = settings.craters;
    if (!craters.enabled) {
        return 0.0;
    }
    const CraterModels models = BuildCraterModels(craters);
    const Bounds2d bounds = TerrainBounds(settings);
    const double area = (bounds.maxX - bounds.minX) * (bounds.maxY - bounds.minY);
    return models.density->ExpectedCount(area, craters.minDiameter, craters.maxDiameter);
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
        const CraterList list = GenerateCraters(PlanarCells(), TerrainBounds(settings),
                                                *models.density, *models.ages, craters.seed,
                                                {.dMin = craters.minDiameter,
                                                 .dMax = craters.maxDiameter,
                                                 .marginRadii = models.profile->ReachRadii()});
        ApplyCraters(raster, list, *models.profile, *models.degradation);
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

ernie::SurfaceSettings SurfaceViewSettings::Effective(const ernie::BrdfRegistry& registry) const {
    ernie::SurfaceSettings effective = surface;
    if (const ernie::BrdfModel* model = registry.Find(surface.brdf)) {
        const auto edited = brdfParams.find(surface.brdf);
        effective.brdfParams = edited != brdfParams.end()
                                   ? ernie::ResolveBrdfParams(*model, edited->second)
                                   : model->Defaults();
    }
    return effective;
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
