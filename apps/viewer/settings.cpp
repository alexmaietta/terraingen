#include "settings.h"

#include "terraingen/core/log.h"
#include "terraingen/craters.h"
#include "terraingen/spatial/cells.h"

#include "ernie/ernie.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

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

/// Identifies a version of a file by its path, size and modification time.
struct FileStamp {
    std::string path;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};

    bool operator==(const FileStamp&) const = default;
};

FileStamp StampOf(const std::string& path) {
    if (path.empty()) {
        throw GeoTiffError("No raster file given");
    }
    std::error_code error;
    FileStamp stamp{path, std::filesystem::file_size(path, error), {}};
    if (!error) {
        stamp.modified = std::filesystem::last_write_time(path, error);
    }
    if (error) {
        throw GeoTiffError("Cannot open " + path + ": " + error.message());
    }
    return stamp;
}

/// The header of the GeoTIFF at `path`, read once per change to the file.
GeoTiffInfo RasterHeader(const std::string& path) {
    static std::mutex mutex;
    static std::optional<std::pair<FileStamp, GeoTiffInfo>> cached;
    const FileStamp stamp = StampOf(path);
    const std::lock_guard lock(mutex);
    if (!cached || cached->first != stamp) {
        cached.emplace(stamp, ReadGeoTiffInfo(path));
    }
    return cached->second;
}

/// What a loaded raster was read and resampled from.
struct RasterKey {
    FileStamp file;
    std::size_t band = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    GeoTransform transform{};
    ResampleMethod resampling = ResampleMethod::Cubic;

    bool operator==(const RasterKey&) const = default;
};

TerrainGrid DescribeRaster(const RasterSourceSettings& raster) {
    const GeoTiffInfo info = RasterHeader(raster.path);
    if (raster.band >= info.bands) {
        throw std::invalid_argument("Band " + std::to_string(raster.band) +
                                    " is missing; the file has " + std::to_string(info.bands) +
                                    (info.bands == 1 ? " band" : " bands"));
    }
    if (info.geoKeys.IsGeographic()) {
        throw std::invalid_argument(
            "The raster's CRS is geographic (degrees); reproject it first, for example with "
            "gdalwarp -t_srs");
    }
    const GeoTransform& t = info.transform;
    if (t.rowRotation != 0.0 || t.colRotation != 0.0 || !(t.pixelWidth > 0.0) ||
        !(t.pixelHeight < 0.0)) {
        throw std::invalid_argument("The raster must be north-up to view");
    }
    TerrainGrid grid{info.width, info.height, t, info.width, info.height, t.pixelWidth};
    const double extentX = static_cast<double>(info.width) * t.pixelWidth;
    const double extentY = static_cast<double>(info.height) * -t.pixelHeight;
    double pixelSize = raster.pixelSize;
    if (pixelSize == 0.0) {
        const bool square = t.pixelHeight == -t.pixelWidth;
        const bool fits = info.width <= kMaxGridSize && info.height <= kMaxGridSize;
        if (square && fits) {
            return grid;
        }
        pixelSize = std::max({t.pixelWidth, -t.pixelHeight,
                              std::max(extentX, extentY) / static_cast<double>(kMaxGridSize)});
    }
    if (!(std::isfinite(pixelSize) && pixelSize > 0.0)) {
        throw std::invalid_argument("Pixel size must be positive, or 0 for the file's");
    }
    grid.width =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(extentX / pixelSize)));
    grid.height =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(extentY / pixelSize)));
    grid.transform = GeoTransform::NorthUp(t.originX, t.originY, pixelSize);
    return grid;
}

/// Describes the first problem with the grid `settings` produce, or returns an empty string.
std::string ValidateGrid(const TerrainSettings& settings) {
    if (settings.source == TerrainSource::Raster) {
        TerrainGrid grid;
        try {
            grid = DescribeTerrain(settings);
        } catch (const std::exception& error) {
            return std::string("Raster: ") + error.what();
        }
        if (grid.width < 2 || grid.height < 2) {
            return "Raster: the resampled grid must be at least 2 cells across";
        }
        if (grid.width > kMaxGridSize || grid.height > kMaxGridSize) {
            return "Raster: the resampled grid must be at most " + std::to_string(kMaxGridSize) +
                   " cells across; raise the pixel size";
        }
        return {};
    }
    if (settings.width < 2 || settings.height < 2) {
        return "Width and height must be at least 2 cells";
    }
    if (settings.width > kMaxGridSize || settings.height > kMaxGridSize) {
        return "Width and height must be at most " + std::to_string(kMaxGridSize) + " cells";
    }
    if (!(std::isfinite(settings.pixelSize) && settings.pixelSize > 0.0)) {
        return "Pixel size must be positive";
    }
    return {};
}

/// Whether the noise layers contribute to the terrain `settings` describe.
bool UsesLayers(const TerrainSettings& settings) {
    return settings.source == TerrainSource::Synthetic || settings.raster.detail;
}

/// Adds `params`' layers to every cell of `raster` with data, evaluated relative to the raster's
/// origin.
void AddDetail(Raster<float>& raster, const SyntheticTerrainParams& params) {
    const GeoTransform& transform = raster.Transform();
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            float& cell = raster(col, row);
            if (raster.IsNoData(cell)) {
                continue;
            }
            const Vec2d centre = raster.CellCenter(col, row);
            cell += EvaluateNoiseLayers(static_cast<float>(centre.x - transform.originX),
                                        static_cast<float>(centre.y - transform.originY),
                                        params.layers.data(), params.layers.size());
        }
    }
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
    const double area = DescribeTerrain(settings).Area();
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
    const double area = DescribeTerrain(settings).Area();
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

Bounds2d TerrainGrid::Bounds() const {
    const Vec2d a = transform.PixelToWorld(0.0, 0.0);
    const Vec2d b = transform.PixelToWorld(static_cast<double>(width), static_cast<double>(height));
    return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmax(a.x, b.x), std::fmax(a.y, b.y)};
}

double TerrainGrid::PixelSize() const {
    return std::sqrt(std::fabs(transform.pixelWidth * transform.pixelHeight));
}

double TerrainGrid::Area() const {
    return static_cast<double>(width) * static_cast<double>(height) * PixelSize() * PixelSize();
}

TerrainGrid DescribeTerrain(const TerrainSettings& settings) {
    if (settings.source == TerrainSource::Raster) {
        return DescribeRaster(settings.raster);
    }
    const double halfX = 0.5 * static_cast<double>(settings.width) * settings.pixelSize;
    const double halfY = 0.5 * static_cast<double>(settings.height) * settings.pixelSize;
    return {settings.width, settings.height,
            GeoTransform::NorthUp(-halfX, halfY, settings.pixelSize)};
}

std::string ValidateSettings(const TerrainSettings& settings) {
    if (std::string error = ValidateGrid(settings); !error.empty()) {
        return error;
    }
    if (UsesLayers(settings)) {
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
    }
    return ValidateCraters(settings);
}

Raster<float> LoadRaster(const TerrainSettings& settings) {
    static std::mutex mutex;
    static std::optional<std::pair<RasterKey, Raster<float>>> cached;
    const RasterSourceSettings& source = settings.raster;
    const TerrainGrid grid = DescribeTerrain(settings);
    const RasterKey key{StampOf(source.path), source.band,    grid.width,
                        grid.height,          grid.transform, source.resampling};
    Raster<float> raster;
    {
        const std::lock_guard lock(mutex);
        if (cached && cached->first == key) {
            raster = cached->second;
        }
    }
    if (raster.Empty()) {
        GeoTiff tiff = ReadGeoTiff(source.path, source.band);
        raster = grid.width == grid.nativeWidth && grid.height == grid.nativeHeight &&
                         grid.transform == tiff.raster.Transform()
                     ? std::move(tiff.raster)
                     : Resample(tiff.raster, grid.PixelSize(), source.resampling);
        LogInfo("viewer", "Loaded ", source.path, " as ", raster.Width(), "x", raster.Height(),
                " cells of ", grid.PixelSize(), " m");
        const std::lock_guard lock(mutex);
        cached.emplace(key, raster);
    }
    if (settings.raster.detail && !settings.layers.empty()) {
        AddDetail(raster, settings.EffectiveTerrain());
    }
    return raster;
}

Raster<float> GenerateTerrain(const TerrainSettings& settings) {
    const std::string error = ValidateSettings(settings);
    if (!error.empty()) {
        throw std::invalid_argument(error);
    }
    Raster<float> raster =
        settings.source == TerrainSource::Raster
            ? LoadRaster(settings)
            : GenerateSyntheticTerrain(settings.width, settings.height, settings.pixelSize,
                                       settings.EffectiveTerrain());
    const CraterSettings& craters = settings.craters;
    if (craters.enabled) {
        const CraterModels models = BuildCraterModels(craters);
        const Bounds2d bounds = DescribeTerrain(settings).Bounds();
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

ernie::Tensor TensorOf(std::shared_ptr<const ernie::Image> image) {
    void* data = const_cast<glm::vec4*>(image->pixels.data());
    std::vector<std::int64_t> shape{static_cast<std::int64_t>(image->height),
                                    static_cast<std::int64_t>(image->width), 4};
    return ernie::Tensor(data, ernie::DType::Float32, std::move(shape), {}, {}, std::move(image));
}

void AuthorView(ernie::Scene& scene, const ViewSettings& view, const ernie::Path& camera,
                const ernie::BrdfRegistry& brdfs) {
    using namespace ernie::tokens;
    if (scene.Has(camera) && scene.TypeOf(camera) == kCamera) {
        scene.GetPrim(camera)
            .Set(kCameraExposure, static_cast<double>(view.camera.exposureStops))
            .Set(kExposureTime, static_cast<double>(view.camera.shutterSeconds))
            .Set(kExposureIso, static_cast<double>(view.camera.iso))
            .Set(kExposureFStop, static_cast<double>(view.camera.fStop))
            .Set(kSpectralBands, view.camera.bands);
    }

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
