#include "terraingen/config/validate.h"

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/config/build.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/generate.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/detail.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/util/overloaded.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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

/// Where an output evaluates the layers: the area it covers and its sample spacing.
struct Extent {
    double area = 0.0;     ///< Square metres.
    double spacing = 0.0;  ///< Metres.
};

/// What a layer is checked against, and what the layers before it found.
struct LayerChecks {
    const std::vector<Extent>& extents;
    const std::filesystem::path& directory;
    const Surface& surface;
    bool craters = false;  ///< Whether a crater layer came before.
    /// Craters the crater layers so far are expected to generate over each extent; empty when
    /// none generate.
    std::vector<double> expectedCraters{};
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
    context.expectedCraters.resize(context.extents.size(), 0.0);
    for (std::size_t i = 0; i < context.extents.size(); ++i) {
        const double expected = At(minPath, [&] {
            return models.density->ExpectedCount(context.extents[i].area, craters.minDiameter,
                                                 craters.maxDiameter);
        });
        CheckExpectedCount(expected, "craters", "raise min_diameter or set max_craters", minPath);
        context.expectedCraters[i] += expected;
    }
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
    for (const Extent& extent : context.extents) {
        const double minDiameter =
            std::fmax(sizes.minDiameter, boulders.fade.startSamples * extent.spacing);
        if (minDiameter >= sizes.maxDiameter) {
            continue;
        }
        const double expected =
            BoulderCandidates(boulders)->ExpectedCount(extent.area, minDiameter, sizes.maxDiameter);
        CheckExpectedCount(expected, "candidate boulders", "raise min_diameter or lower coverage",
                           Child(path, "min_diameter"));
    }
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

RasterGrid DescribeRegion(const GridRegionSpec& region, const std::string& path,
                          const Surface& surface) {
    RequirePositive(region.pixelSize, Child(path, "pixel_size"));
    CheckGridSize(region.width, region.height, Child(path, "width"));
    if (!(std::isfinite(region.centreX) && std::isfinite(region.centreY))) {
        throw ConfigError(Child(path, "centre_x"), "the centre must be finite");
    }
    const double cell = region.pixelSize / surface.UnitLength();
    if (surface.IsBody() &&
        !(std::fabs(region.centreY) + 0.5 * cell * static_cast<double>(region.height) <= 90.0 &&
          cell * static_cast<double>(region.width) <= 360.0)) {
        throw ConfigError(Child(path, "height"),
                          "the grid must lie within latitudes -90 to 90 and one turn of longitude");
    }
    return config::RegionGrid(region, surface);
}

/// @throws ConfigError for a raster whose coordinates do not suit the surface.
void CheckRasterCrs(const GeoTiffInfo& info, const Surface& surface, const std::string& path) {
    if (info.geoKeys.IsGeographic() && !surface.IsBody()) {
        throw ConfigError(path, "the raster's CRS is geographic (degrees); craters are sized in "
                                "metres, so reproject it first, for example with gdalwarp -t_srs, "
                                "or set the body to a sphere");
    }
    if (surface.IsBody() && !info.geoKeys.Empty() && !info.geoKeys.IsGeographic()) {
        throw ConfigError(
            path, "on a body the raster must be in longitude and latitude (a geographic CRS)");
    }
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
            [&](const DuneLayerSpec& layer) {
                At(path, [&] { ValidateDuneOptions(layer.dunes); });
            },
            [&](const PavementLayerSpec& layer) {
                At(path, [&] { ValidatePavementOptions(layer.pavement); });
            },
            [&](const StrataLayerSpec& layer) {
                At(path, [&] { ValidateStrataOptions(layer.strata); });
            },
            [&](const SandFillLayerSpec& layer) {
                At(path, [&] { ValidateSandFillOptions(layer.sandFill); });
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
            [&](const DemDetailLayerSpec& layer) {
                const std::string file = Child(path, "path");
                RequireNonEmpty(layer.path, file);
                const GeoTiffInfo info = ReadRasterInfo(layer.path, context.directory, file);
                CheckBand(layer.read.band, info, Child(path, "band"));
                CheckRasterCrs(info, context.surface, file);
                RequirePositive(layer.read.lowPass, Child(path, "low_pass"));
                At(Child(path, "weight"), [&] {
                    DetailLayer(std::make_shared<FlatSource>(2, 2, 1.0),
                                std::make_shared<FlatSource>(2, 2, 1.0), layer.weight);
                });
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

RasterGrid DescribeRaster(const RasterSourceSpec& raster, const std::filesystem::path& directory,
                          const std::string& path, const Surface& surface) {
    const std::string file = Child(path, "path");
    RequireNonEmpty(raster.path, file);
    const GeoTiffInfo info = ReadRasterInfo(raster.path, directory, file);
    CheckBand(raster.read.band, info, Child(path, "band"));
    CheckRasterCrs(info, surface, file);
    RequireNonNegative(raster.read.lowPass, Child(path, "low_pass"));
    RasterGrid grid{info.width, info.height, info.transform};
    if (!raster.read.pixelSize) {
        CheckGridSize(grid.width, grid.height, file);
        return grid;
    }
    const std::string pixelSize = Child(path, "pixel_size");
    RequirePositive(*raster.read.pixelSize, pixelSize);
    if (grid.transform.rowRotation != 0.0 || grid.transform.colRotation != 0.0) {
        throw ConfigError(pixelSize, "rotated rasters cannot be resampled");
    }
    RasterGrid resampled;
    try {
        resampled = ResampledGrid(grid, *raster.read.pixelSize / surface.UnitLength());
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
            [&](const RasterSourceSpec& s) { return DescribeRaster(s, directory, path, surface); },
            [&](const ChainSourceSpec& s) { return DescribeChain(s, directory, path, surface); },
        },
        source);
}

/// Rows of a whole-body list's grid above which its spacing is too fine.
constexpr double kMaxWholeBodyRows = 1e9;

/// The grid the output at `path` covers (`OutputGrid`), its region and spacing checked; none for a
/// mesh.
/// @throws ConfigError for an invalid region or spacing, or an output with no grid to cover.
std::optional<RasterGrid> CheckOutputGrid(const OutputSpec& output, const std::string& path,
                                          const std::optional<RasterGrid>& base,
                                          const Surface& surface) {
    if (std::holds_alternative<MeshOutput>(output)) {
        return std::nullopt;
    }
    bool listed = false;
    std::visit(
        [&](const auto& o) {
            if constexpr (requires { o.region; }) {
                if (o.region) {
                    DescribeRegion(*o.region, Child(path, "region"), surface);
                }
            }
            if constexpr (config::FeatureOutput<std::decay_t<decltype(o)>>) {
                listed = true;
                if (o.spacing) {
                    const std::string spacing = Child(path, "spacing");
                    RequirePositive(*o.spacing, spacing);
                    if (!o.region && !base && surface.IsBody() &&
                        180.0 * surface.UnitLength() / *o.spacing > kMaxWholeBodyRows) {
                        throw ConfigError(spacing, "is too fine to list features over the body");
                    }
                }
            }
        },
        output);
    std::optional<RasterGrid> grid = config::OutputGrid(output, base, surface);
    if (!grid) {
        throw ConfigError(Child(path, "region"),
                          listed && surface.IsBody()
                              ? "required without a base, or a spacing to list the whole body"
                              : "required without a base: the grid of square cells to cover");
    }
    return grid;
}

/// What outputs are checked against.
struct OutputContext {
    bool body = false;       ///< Whether the surface is a body.
    bool albedo = false;     ///< Whether the configuration has an albedo.
    bool materials = false;  ///< Whether the configuration has a material map.
};

void RequireMeshFile(const std::string& file, const std::string& path) {
    if (!IsMeshPath(file)) {
        throw ConfigError(path, "must end in .obj, .gltf, .glb or .stl");
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

void ValidateOutput(const MaterialsOutput& output, const std::string& path,
                    const OutputContext& context) {
    if (!context.materials) {
        throw ConfigError(path, "the configuration has no materials");
    }
    ValidateGeoTiffOutput(output, path);
}

void ValidateOutput(const PhotometryOutput& output, const std::string& path,
                    const OutputContext& context) {
    if (!context.materials) {
        throw ConfigError(path, "the configuration has no materials");
    }
    ValidateGeoTiffOutput(output, path);
}

/// Whether a layer lays sand over the material map.
bool LaysSand(const LayerSpec& layer) {
    return std::holds_alternative<DuneLayerSpec>(layer) ||
           std::holds_alternative<BedformLayerSpec>(layer) ||
           std::holds_alternative<SandFillLayerSpec>(layer);
}

/// @throws ConfigError under "materials" for invalid options or photometry, or a photometry output
/// without parameters for a class the surface can have.
void ValidateMaterials(const TerrainConfig& config) {
    const MaterialsSpec& materials = *config.materials;
    At("materials", [&] { ValidateMaterialOptions(materials.options); });
    const MaterialPhotometry photometry = config::BuildPhotometry(materials.photometry);
    At("materials.photometry", [&] { ValidateMaterialPhotometry(photometry); });
    const bool photometryOutput =
        std::any_of(config.outputs.begin(), config.outputs.end(), [](const OutputSpec& output) {
            return std::holds_alternative<PhotometryOutput>(output);
        });
    if (!photometryOutput) {
        return;
    }
    std::vector<std::pair<MaterialClass, const char*>> needed{{MaterialClass::Soil, "soil"}};
    if (materials.options.exposure ||
        std::any_of(config.layers.begin(), config.layers.end(), [](const LayerSpec& layer) {
            return std::holds_alternative<PavementLayerSpec>(layer) ||
                   std::holds_alternative<StrataLayerSpec>(layer);
        })) {
        needed.emplace_back(MaterialClass::Bedrock, "bedrock");
    }
    if (materials.options.dustCover > 0.0) {
        needed.emplace_back(MaterialClass::Dust, "dust");
    }
    if (std::any_of(config.layers.begin(), config.layers.end(), LaysSand)) {
        needed.emplace_back(MaterialClass::Sand, "sand");
    }
    for (const auto& [material, name] : needed) {
        if (!photometry[static_cast<std::size_t>(material)]) {
            throw ConfigError(Child("materials.photometry", name),
                              "required by the photometry output: the surface has some");
        }
    }
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

void ValidateOutput(const MeshOutput& output, const std::string& path,
                    const OutputContext& context) {
    if (!context.body) {
        throw ConfigError(path, "a mesh needs a whole body: a sphere, ellipsoid or shape model; "
                                "use terrain_mesh on a plane");
    }
    if (!IsIcqPath(output.path)) {
        RequireMeshFile(output.path, Child(path, "path"));
    }
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
    return DescribeSourceAt(source, directory, "base", surface);
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
        const std::string path = Element("albedo.layers", i);
        std::visit(Overloaded{
                       [&](const CraterAlbedoSpec& craters) {
                           CheckCraterAlbedo(craters, craterLayers, path);
                       },
                       [&](const SplotchAlbedoSpec& splotches) {
                           At(path, [&] { ValidateSplotches(splotches.options); });
                       },
                   },
                   albedo.layers[i]);
    }
}

ConfigSummary ValidateConfig(const TerrainConfig& config, const std::filesystem::path& directory) {
    ConfigSummary summary;
    const Surface surface = BuildSurface(config.body, directory);
    if (config.base) {
        summary.base = DescribeSource(*config.base, directory, surface);
    }
    const OutputContext outputs{surface.IsBody(), config.albedo.has_value(),
                                config.materials.has_value()};
    std::vector<Extent> extents;
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const OutputSpec& output = config.outputs[i];
        std::visit([&](const auto& o) { ValidateOutput(o, Element("outputs", i), outputs); },
                   output);
        const std::optional<RasterGrid> grid =
            CheckOutputGrid(output, Element("outputs", i), summary.base, surface);
        summary.outputGrids.push_back(grid);
        if (grid) {
            const double spacing = std::visit(
                [](const auto& o) {
                    if constexpr (config::FeatureOutput<std::decay_t<decltype(o)>>) {
                        return o.spacing.value_or(0.0);
                    }
                    return 0.0;
                },
                output);
            extents.push_back(
                {grid->SurfaceArea(surface), std::fmax(spacing, grid->Spacing(surface))});
        }
    }
    LayerChecks layers{extents, directory, surface};
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        ValidateLayer(config.layers[i], Element("layers", i), layers);
    }
    if (!layers.expectedCraters.empty()) {
        summary.expectedCraters =
            *std::max_element(layers.expectedCraters.begin(), layers.expectedCraters.end());
    }
    if (config.albedo) {
        ValidateAlbedo(config, directory);
    }
    if (config.materials) {
        ValidateMaterials(config);
    }
    return summary;
}

}  // namespace terraingen
