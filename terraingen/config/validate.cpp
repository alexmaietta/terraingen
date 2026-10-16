#include "terraingen/config/validate.h"

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/config/build.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/generate.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/mesh/mesh_io.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/util/overloaded.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

using config::At;
using config::BuildBoulderOptions;
using config::BuildRegionShape;
using config::Child;
using config::Element;
using config::RequireNonEmpty;
using config::RequireNonNegative;
using config::RequirePositive;
using config::Resolve;

void CheckGridSize(std::size_t width, std::size_t height, const std::string& path) {
    if (width < 2 || height < 2 || width > kMaxConfigGridSize || height > kMaxConfigGridSize) {
        throw ConfigError(path, std::to_string(width) + "x" + std::to_string(height) +
                                    " cells; each side must be between 2 and " +
                                    std::to_string(kMaxConfigGridSize));
    }
}

/// @throws ConfigError at `path` for an expected count of `items` above the generation limit.
void CheckExpectedCount(double expected, std::string_view items, std::string_view remedy,
                        const std::string& path) {
    const double limit = CraterGenerationOptions{}.maxExpectedCraters;
    if (expected > limit) {
        throw ConfigError(path, "about " + std::to_string(static_cast<long long>(expected)) + " " +
                                    std::string(items) + " expected, more than the limit of " +
                                    std::to_string(static_cast<long long>(limit)) + "; " +
                                    std::string(remedy));
    }
}

/// @throws ConfigError for a stack of more than `kMaxNoiseLayers` layers, or none unless
/// `allowEmpty`, or an invalid layer.
void ValidateNoiseStack(const std::vector<NoiseLayerConfig>& layers, const std::string& path,
                        bool allowEmpty) {
    if ((layers.empty() && !allowEmpty) || layers.size() > kMaxNoiseLayers) {
        throw ConfigError(path, std::string("needs ") + (allowEmpty ? "0" : "1") + " to " +
                                    std::to_string(kMaxNoiseLayers) + " layers");
    }
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const std::string layer = Element(path, i);
        if (layers[i].hurst) {
            config::RequireFinite(*layers[i].hurst, Child(layer, "hurst"));
        }
        At(layer, [&] { ValidateNoiseLayer(layers[i].Effective()); });
    }
}

/// What a layer is checked against, and what the layers before it found.
struct LayerChecks {
    const RasterGrid& grid;
    const std::filesystem::path& directory;
    const Surface& surface;
    bool craters = false;  ///< Whether a crater layer came before.
    std::optional<double> expectedCraters{};
};

void CheckManualCraters(const std::vector<Crater>& manual, const std::string& path) {
    for (std::size_t n = 0; n < manual.size(); ++n) {
        const Crater& crater = manual[n];
        const std::string at = Element(path, n);
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y))) {
            throw ConfigError(at, "position must be finite");
        }
        RequirePositive(crater.diameter, Child(at, "diameter"));
        RequireNonNegative(crater.ageGyr, Child(at, "age_gyr"));
    }
}

void ValidateCraters(const CraterLayerSpec& craters, const std::string& path,
                     LayerChecks& context) {
    CheckManualCraters(craters.manual, Child(path, "manual"));
    At(Child(path, "spacing"), [&] { ValidateCraterSpacing(craters.spacing); });
    At(Child(path, "apply"), [&] { ValidateCraterApplyOptions(craters.apply); });
    const CraterModels models = BuildCraterModels(craters, path);
    context.craters = true;
    if (!craters.generate) {
        return;
    }
    const std::string minPath = Child(path, "min_diameter");
    RequirePositive(craters.minDiameter, minPath);
    if (!(std::isfinite(craters.maxDiameter) && craters.maxDiameter > craters.minDiameter)) {
        throw ConfigError(Child(path, "max_diameter"), "must exceed min_diameter");
    }
    const double area = context.grid.SurfaceArea(context.surface);
    const double expected = At(minPath, [&] {
        return models.density->ExpectedCount(area, craters.minDiameter, craters.maxDiameter);
    });
    CheckExpectedCount(expected, "craters", "raise min_diameter or set max_craters", minPath);
    context.expectedCraters = context.expectedCraters.value_or(0.0) + expected;
}

void ValidateBoulders(const BoulderLayerSpec& spec, const std::string& path,
                      const LayerChecks& context) {
    const BoulderLayerOptions boulders = BuildBoulderOptions(spec, path, context.directory);
    At(path, [&] { ValidateBoulderLayerOptions(boulders); });
    if (boulders.ejecta.MinCraterDiameter() > 0.0 && !context.craters) {
        throw ConfigError(Child(Child(path, "ejecta"), "min_crater_diameter"),
                          "ejecta need a crater layer before this one");
    }
    const BoulderSizes& sizes = boulders.sizes;
    const double minDiameter = std::fmax(
        sizes.minDiameter, boulders.fade.startSamples * context.grid.Spacing(context.surface));
    if (minDiameter >= sizes.maxDiameter) {
        return;
    }
    const double expected = BoulderCandidates(boulders)->ExpectedCount(
        context.grid.SurfaceArea(context.surface), minDiameter, sizes.maxDiameter);
    CheckExpectedCount(expected, "candidate boulders", "raise min_diameter or lower coverage",
                       Child(path, "min_diameter"));
}

void ValidateSecondaries(const SecondaryLayerSpec& layer, const std::string& path,
                         const LayerChecks& context) {
    if (!context.craters) {
        throw ConfigError(path, "secondaries need a crater layer before this one");
    }
    if (!(layer.secondaries.onsetCount && *layer.secondaries.onsetCount > 0.0)) {
        throw ConfigError(Child(path, "onset_count"),
                          "required: the sources give secondary sizes, not their number");
    }
    At(path, [&] { ValidateSecondaryCraterOptions(layer.secondaries); });
    RequirePositive(layer.depthRatio, Child(path, "depth_ratio"));
}

void ValidateLayer(const LayerSpec& spec, const std::string& path, LayerChecks& context) {
    std::visit(
        Overloaded{
            [&](const NoiseLayerSpec& noise) {
                ValidateNoiseStack(noise.stack, Child(path, "stack"), false);
            },
            [&](const CraterLayerSpec& layer) { ValidateCraters(layer, path, context); },
            [&](const BoulderLayerSpec& layer) { ValidateBoulders(layer, path, context); },
            [&](const SecondaryLayerSpec& layer) { ValidateSecondaries(layer, path, context); },
            [&](const RoughnessLayerSpec& layer) {
                At(path, [&] { ValidateRoughnessOptions(layer.roughness); });
            },
            [&](const BedformLayerSpec& layer) {
                At(path, [&] { ValidateBedformOptions(layer.bedforms); });
            },
            [&](const ScaleLayerSpec& layer) {
                At(path, [&] { ScaleLayer(layer.scale, layer.offset); });
            },
            [&](const ClampLayerSpec& layer) {
                At(path, [&] { ClampLayer(layer.min, layer.max); });
            },
            [&](const RegionLayerSpec& layer) {
                At(Child(path, "shape"),
                   [&] { ValidateRegionShape(BuildRegionShape(layer.shape)); });
                RequireNonNegative(layer.feather, Child(path, "feather"));
                // Crater layers inside a region count for neither later layers nor the summary.
                LayerChecks inner = context;
                ValidateLayer(*layer.layer, Child(path, "layer"), inner);
            },
        },
        spec);
}

/// @throws ConfigError at `path` for a raster file that cannot be read.
GeoTiffInfo ReadRasterInfo(const std::string& file, const std::filesystem::path& directory,
                           const std::string& path) {
    try {
        return ReadGeoTiffInfo(Resolve(file, directory));
    } catch (const GeoTiffError& error) {
        throw ConfigError(path, error.what());
    }
}

/// @throws ConfigError at `path` for a band the raster does not have.
void CheckBand(std::size_t band, const GeoTiffInfo& info, const std::string& path) {
    if (band >= info.bands) {
        throw ConfigError(path, "the file has " + std::to_string(info.bands) +
                                    (info.bands == 1 ? " band" : " bands"));
    }
}

RasterGrid DescribeSourceAt(const SourceSpec& source, const std::filesystem::path& directory,
                            const std::string& path, const Surface& surface);

RasterGrid DescribeChain(const ChainSourceSpec& chain, const std::filesystem::path& directory,
                         const std::string& path, const Surface& surface) {
    const std::string sources = Child(path, "sources");
    if (chain.sources.empty()) {
        throw ConfigError(sources, "must not be empty");
    }
    RasterGrid grid;
    for (std::size_t n = 0; n < chain.sources.size(); ++n) {
        const RasterGrid described =
            DescribeSourceAt(chain.sources[n], directory, Element(sources, n), surface);
        if (n == 0) {
            grid = described;
        }
    }
    return grid;
}

RasterGrid DescribeFlat(const FlatSourceSpec& flat, const std::string& path,
                        const Surface& surface) {
    RequirePositive(flat.pixelSize, Child(path, "pixel_size"));
    CheckGridSize(flat.width, flat.height, Child(path, "width"));
    if (!(std::isfinite(flat.centreX) && std::isfinite(flat.centreY))) {
        throw ConfigError(Child(path, "centre_x"), "the centre must be finite");
    }
    const double cell = flat.pixelSize / surface.UnitLength();
    if (surface.IsSphere() &&
        !(std::fabs(flat.centreY) + 0.5 * cell * static_cast<double>(flat.height) <= 90.0 &&
          cell * static_cast<double>(flat.width) <= 360.0)) {
        throw ConfigError(Child(path, "height"),
                          "the grid must lie within latitudes -90 to 90 and one turn of longitude");
    }
    return FlatSource(flat.width, flat.height, cell, flat.elevation, {flat.centreX, flat.centreY})
        .Grid();
}

/// @throws ConfigError for a raster whose coordinates do not suit the surface.
void CheckRasterCrs(const GeoTiffInfo& info, const Surface& surface, const std::string& path) {
    if (info.geoKeys.IsGeographic() && !surface.IsSphere()) {
        throw ConfigError(path, "the raster's CRS is geographic (degrees); craters are sized in "
                                "metres, so reproject it first, for example with gdalwarp -t_srs, "
                                "or set the surface to a sphere");
    }
    if (surface.IsSphere() && !info.geoKeys.Empty() && !info.geoKeys.IsGeographic()) {
        throw ConfigError(
            path, "on a sphere the raster must be in longitude and latitude (a geographic CRS)");
    }
}

RasterGrid DescribeRaster(const RasterSourceSpec& raster, const std::filesystem::path& directory,
                          const std::string& path, const Surface& surface) {
    const std::string file = Child(path, "path");
    RequireNonEmpty(raster.path, file);
    const GeoTiffInfo info = ReadRasterInfo(raster.path, directory, file);
    CheckBand(raster.band, info, Child(path, "band"));
    CheckRasterCrs(info, surface, file);
    RasterGrid grid{info.width, info.height, info.transform};
    if (!raster.pixelSize) {
        CheckGridSize(grid.width, grid.height, file);
        return grid;
    }
    const std::string pixelSize = Child(path, "pixel_size");
    RequirePositive(*raster.pixelSize, pixelSize);
    if (grid.transform.rowRotation != 0.0 || grid.transform.colRotation != 0.0) {
        throw ConfigError(pixelSize, "rotated rasters cannot be resampled");
    }
    RasterGrid resampled;
    try {
        resampled = ResampledGrid(grid, *raster.pixelSize / surface.UnitLength());
    } catch (const std::invalid_argument&) {
        throw ConfigError(pixelSize, "resampling would exceed " +
                                         std::to_string(kMaxConfigGridSize) + " cells per side");
    }
    CheckGridSize(resampled.width, resampled.height, pixelSize);
    return resampled;
}

RasterGrid DescribeSourceAt(const SourceSpec& source, const std::filesystem::path& directory,
                            const std::string& path, const Surface& surface) {
    return std::visit(
        Overloaded{
            [&](const FlatSourceSpec& s) { return DescribeFlat(s, path, surface); },
            [&](const RasterSourceSpec& s) { return DescribeRaster(s, directory, path, surface); },
            [&](const ChainSourceSpec& s) { return DescribeChain(s, directory, path, surface); },
        },
        source);
}

/// What outputs are checked against.
struct OutputContext {
    bool sphere = false;  ///< Whether the surface is a sphere.
    bool albedo = false;  ///< Whether the configuration has an albedo.
};

void RequireMeshFile(const std::string& file, const std::string& path) {
    if (!IsMeshPath(file)) {
        throw ConfigError(path, "must end in .ply, .obj, .gltf or .glb");
    }
}

void ValidateGeoTiffOutput(const GeoTiffOutput& output, const std::string& path) {
    RequireNonEmpty(output.path, Child(path, "path"));
    if (output.compression == TiffCompression::PackBits) {
        throw ConfigError(Child(path, "compression"),
                          "packbits is read-only; use none, lzw or deflate");
    }
    if (output.tileSize == 0 || output.tileSize % 16 != 0) {
        throw ConfigError(Child(path, "tile_size"), "must be a positive multiple of 16");
    }
}

void ValidateOutput(const HeightmapOutput& output, const std::string& path,
                    const OutputContext& /*context*/) {
    ValidateGeoTiffOutput(output, path);
}

void ValidateOutput(const AlbedoOutput& output, const std::string& path,
                    const OutputContext& context) {
    if (!context.albedo) {
        throw ConfigError(path, "the configuration has no albedo");
    }
    ValidateGeoTiffOutput(output, path);
}

void ValidateOutput(const ListOutput& output, const std::string& path,
                    const OutputContext& /*context*/) {
    RequireNonEmpty(output.path, Child(path, "path"));
}

void ValidateOutput(const BoulderModelsOutput& output, const std::string& path,
                    const OutputContext& /*context*/) {
    RequireMeshFile(output.path, Child(path, "path"));
    if (!(output.creaseAngle >= 0.0 && output.creaseAngle <= 180.0)) {
        throw ConfigError(Child(path, "crease_angle"), "must be between 0 and 180 degrees");
    }
}

void ValidateOutput(const BodyMeshOutput& output, const std::string& path,
                    const OutputContext& context) {
    if (!context.sphere) {
        throw ConfigError(path, "a body mesh needs a sphere surface");
    }
    RequireMeshFile(output.path, Child(path, "path"));
    if (output.cellsPerFace == 0 || output.cellsPerFace > 4096) {
        throw ConfigError(Child(path, "cells_per_face"), "must be 1 to 4096");
    }
    if (output.spacing) {
        RequirePositive(*output.spacing, Child(path, "spacing"));
    }
}

void ValidateOutput(const TerrainMeshOutput& output, const std::string& path,
                    const OutputContext& /*context*/) {
    RequireMeshFile(output.path, Child(path, "path"));
}

void CheckSyntheticAlbedo(const SyntheticAlbedoSpec& spec, const std::string& path) {
    const std::vector<std::pair<const std::vector<NoiseLayerConfig>*, std::string>> stacks{
        {&spec.hueNoise, Child(path, "hue_noise")},
        {&spec.saturationNoise, Child(path, "saturation_noise")},
        {&spec.valueNoise, Child(path, "value_noise")}};
    for (const auto& [stack, at] : stacks) {
        ValidateNoiseStack(*stack, at, true);
    }
    At(path, [&] { ValidateSyntheticAlbedo(spec.Params()); });
}

void CheckRasterAlbedo(const RasterAlbedoSpec& spec, const std::filesystem::path& directory,
                       const std::string& path) {
    const std::string file = Child(path, "path");
    const std::string bands = Child(path, "bands");
    RequireNonEmpty(spec.path, file);
    if (spec.bands.size() != 1 && spec.bands.size() != kMaxAlbedoBands) {
        throw ConfigError(bands, "needs 1 or 3 bands");
    }
    const GeoTiffInfo info = ReadRasterInfo(spec.path, directory, file);
    for (const std::size_t band : spec.bands) {
        CheckBand(band, info, bands);
    }
    if (info.transform.rowRotation != 0.0 || info.transform.colRotation != 0.0) {
        throw ConfigError(file, "rotated albedo maps are not supported");
    }
    if ((spec.scale && !std::isfinite(*spec.scale)) ||
        (spec.offset && !std::isfinite(*spec.offset))) {
        throw ConfigError(Child(path, "scale"), "the scale and offset must be finite");
    }
    At(Child(path, "detail"), [&] { ValidateAlbedoDetail(spec.options.detail); });
}

void CheckCraterAlbedo(const CraterAlbedoSpec& craters, std::size_t craterLayers,
                       const std::string& path) {
    if (craterLayers == 0) {
        throw ConfigError(path, "crater albedo needs a crater layer");
    }
    if (craters.craterLayer && *craters.craterLayer >= craterLayers) {
        throw ConfigError(Child(path, "crater_layer"),
                          "there are " + std::to_string(craterLayers) + " crater layers");
    }
    At(path, [&] { ValidateCraterAlbedo(craters.options); });
}

}  // namespace

RasterGrid DescribeSource(const SourceSpec& source, const std::filesystem::path& directory,
                          const Surface& surface) {
    return DescribeSourceAt(source, directory, "source", surface);
}

void ValidateAlbedo(const TerrainConfig& config, const std::filesystem::path& directory) {
    const AlbedoSpec& albedo = *config.albedo;
    std::visit(
        Overloaded{
            [&](const SyntheticAlbedoSpec& s) { CheckSyntheticAlbedo(s, "albedo.source"); },
            [&](const RasterAlbedoSpec& s) { CheckRasterAlbedo(s, directory, "albedo.source"); },
        },
        albedo.source);
    const auto craterLayers = static_cast<std::size_t>(
        std::count_if(config.layers.begin(), config.layers.end(), [](const LayerSpec& layer) {
            return std::holds_alternative<CraterLayerSpec>(layer);
        }));
    for (std::size_t i = 0; i < albedo.layers.size(); ++i) {
        std::visit(
            [&](const CraterAlbedoSpec& craters) {
                CheckCraterAlbedo(craters, craterLayers, Element("albedo.layers", i));
            },
            albedo.layers[i]);
    }
}

ConfigSummary ValidateConfig(const TerrainConfig& config, const std::filesystem::path& directory) {
    const Surface surface = BuildSurface(config.surface);
    const RasterGrid grid = DescribeSource(config.source, directory, surface);
    LayerChecks layers{grid, directory, surface};
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        ValidateLayer(config.layers[i], Element("layers", i), layers);
    }
    for (std::size_t i = 0; i < config.filters.size(); ++i) {
        At(Element("filters", i), [&] {
            std::visit([](const RimSmoothing& smoothing) { ValidateRimSmoothing(smoothing); },
                       config.filters[i]);
        });
    }
    const OutputContext outputs{surface.IsSphere(), config.albedo.has_value()};
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        std::visit(
            [&](const auto& output) { ValidateOutput(output, Element("outputs", i), outputs); },
            config.outputs[i]);
    }
    if (config.albedo) {
        ValidateAlbedo(config, directory);
    }
    return {grid, layers.expectedCraters};
}

}  // namespace terraingen
