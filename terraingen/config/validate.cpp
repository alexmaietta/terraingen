#include "terraingen/config/validate.h"

#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/config/build.h"
#include "terraingen/config/fields/checks.h"
#include "terraingen/config/fields/common.h"
#include "terraingen/config/generate.h"
#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/craters/shaping/explicit_state.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/layers/transforms.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/noise/noise_layers.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/sources/upsampling.h"
#include "terraingen/util/overloaded.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

using config::At;
using config::AtFile;
using config::BuildBoulderOptions;
using config::BuildRegionShape;
using config::CheckFinite;
using config::CheckNonEmpty;
using config::Child;
using config::Element;
using config::Resolve;

/// @throws ConfigError at `path` for a grid side outside the sizes a configuration may request.
void CheckGridSize(std::size_t width, std::size_t height, const std::string& path) {
    const auto fits = [](std::size_t side) {
        return side >= kMinConfigGridSize && side <= kMaxConfigGridSize;
    };
    if (!fits(width) || !fits(height)) {
        throw ConfigError(path, std::to_string(width) + "x" + std::to_string(height) +
                                    " cells; each side must be " +
                                    config::CountText(kMinConfigGridSize, kMaxConfigGridSize));
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
void CheckNoiseStack(const std::vector<NoiseLayerConfig>& layers, const std::string& path,
                     bool allowEmpty) {
    const std::size_t fewest = allowEmpty ? 0 : 1;
    if (layers.size() < fewest || layers.size() > kMaxNoiseLayers) {
        throw ConfigError(path, "needs " + config::CountText(fewest, kMaxNoiseLayers) + " layers");
    }
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const std::string layer = Element(path, i);
        if (layers[i].hurst) {
            CheckFinite(static_cast<double>(*layers[i].hurst), Child(layer, "hurst"));
        }
        At(layer, [&] { ValidateNoiseLayer(layers[i].Effective()); });
    }
}

/// Where an output evaluates the layers: the area it covers and its sample spacing.
struct Extent {
    double area = 0.0;     ///< Square meters.
    double spacing = 0.0;  ///< Meters.
    bool listed = false;   ///< A feature list, which holds every boulder however small.
};

/// What a layer is checked against, and what the layers before it found.
struct LayerChecks {
    const std::vector<Extent>& extents;
    const std::filesystem::path& directory;
    const Surface& surface;
    bool posts = false;     ///< Whether the source has posts to estimate settings from.
    bool craters = false;   ///< Whether a crater layer came before.
    bool boulders = false;  ///< Whether a boulder layer came before.
    /// Craters the crater layers so far are expected to generate over each extent; empty when
    /// none generate.
    std::vector<double> expectedCraters{};
};

void CheckLayer(const LayerSpec& spec, const std::string& path, LayerChecks& context);

void CheckManualCraters(const std::vector<Crater>& manual, const std::string& path) {
    for (std::size_t n = 0; n < manual.size(); ++n) {
        const Crater& crater = manual[n];
        const std::string at = Element(path, n);
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y))) {
            throw ConfigError(at, "position must be finite");
        }
        if (!(std::isnan(crater.depth) || (std::isfinite(crater.depth) && crater.depth > 0.0))) {
            throw ConfigError(Child(at, "depth"), "must be positive");
        }
        if (!(std::isnan(crater.rimHeight) ||
              (std::isfinite(crater.rimHeight) && crater.rimHeight >= 0.0))) {
            throw ConfigError(Child(at, "rim_height"), "must not be negative");
        }
        if (std::isnan(crater.depth) && !std::isnan(crater.rimHeight)) {
            throw ConfigError(Child(at, "rim_height"), "needs a depth");
        }
    }
}

/// @throws ConfigError at a manual crater whose depth and rim height the layer's profile cannot
/// take (`ResolvedCraterState`).
void CheckExplicitShapes(const std::vector<Crater>& manual, const CraterModels& models,
                         const std::string& path) {
    for (std::size_t n = 0; n < manual.size(); ++n) {
        if (!std::isnan(manual[n].depth)) {
            At(Element(path, n), [&] { ResolvedCraterState(models.Shape(), manual[n]); });
        }
    }
}

/// @throws ConfigError under the layer's "degradation" for an invalid power-law diffusivity.
void CheckDiffusivity(const DegradationSpec& degradation, const std::string& path) {
    const auto* diffusion = std::get_if<DiffusionDegradationSpec>(&degradation);
    if (diffusion && diffusion->diffusivity == DiffusivityModel::PowerLaw) {
        At(path, [&] { ValidatePowerLawDiffusivity(diffusion->powerLaw); });
    }
}

/// Adds the craters a generating layer is expected to make over each extent to `context`.
/// @throws ConfigError at the layer's "min_diameter_m" for more than the generation limit.
void AddExpectedCraters(const CraterLayerSpec& craters, const CraterModels& models,
                        const std::string& path, LayerChecks& context) {
    const std::string minPath = Child(path, "min_diameter_m");
    context.expectedCraters.resize(context.extents.size(), 0.0);
    for (std::size_t i = 0; i < context.extents.size(); ++i) {
        const double expected = At(minPath, [&] {
            return models.density->ExpectedCount(context.extents[i].area, craters.minDiameter,
                                                 craters.maxDiameter);
        });
        CheckExpectedCount(expected, "craters", "raise min_diameter_m or set max_craters", minPath);
        context.expectedCraters[i] += expected;
    }
}

/// A length as messages give it, such as 2.5.
std::string Meters(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%g", value);
    return buffer.data();
}

/// The diameters a production function is defined over.
DiameterRange ProductionRange(ProductionFunction production) {
    switch (production) {
    case ProductionFunction::Neukum:
        return NeukumProductionFunction().ValidRange();
    case ProductionFunction::Ivanov:
        return IvanovMarsProductionFunction().ValidRange();
    case ProductionFunction::ExtendedNeukum:
        break;
    }
    return ExtendedNeukumProductionFunction().ValidRange();
}

/// @throws ConfigError at `path` when the density or the ages of a generating layer are not
/// defined at its smallest diameter, as steady-state ages are not below their production
/// function's range.
void CheckModelRange(const CraterLayerSpec& craters, const CraterModels& models,
                     const std::string& path) {
    try {
        models.density->Cumulative(craters.minDiameter);
        models.ages->LifetimeGyr(craters.minDiameter);
    } catch (const std::domain_error&) {
        const DiameterRange& density = models.density->ValidRange();
        std::string message =
            "the density or ages are not defined at " + Meters(craters.minDiameter) + " m";
        if (const auto* steady = std::get_if<SteadyStateAgesSpec>(&craters.ages)) {
            message += "; steady-state ages need the production function's diameters, from " +
                       Meters(ProductionRange(steady->production).min) + " m";
        } else if (density.min > 0.0) {
            message += "; the density starts at " + Meters(density.min) + " m";
        }
        throw ConfigError(path, message);
    }
}

/// @throws ConfigError at the layer's "source_resolves_px" for a set value that is not positive
/// and finite.
void CheckSourceResolves(const std::optional<double>& pixels, const std::string& path) {
    At(Child(path, "source_resolves_px"), [&] { ValidateSourceResolvesPixels(pixels); });
}

void CheckLayerSpec(const CraterLayerSpec& craters, const std::string& path, LayerChecks& context) {
    CheckSourceResolves(craters.sourceResolvesPixels, path);
    CheckManualCraters(craters.manual, Child(path, "manual"));
    At(Child(path, "spacing"), [&] { ValidateCraterSpacing(craters.spacing); });
    At(Child(path, "apply"), [&] { ValidateCraterApplyOptions(craters.apply); });
    if (craters.densityMap) {
        const std::string at = Child(path, "density_map");
        const DensityMap map = config::BuildDensityMap(*craters.densityMap, at, context.directory);
        At(at, [&] { ValidateDensityMap(map); });
    }
    CheckDiffusivity(craters.degradation, Child(path, "degradation"));
    const CraterModels models = BuildCraterModels(craters, path);
    CheckExplicitShapes(craters.manual, models, Child(path, "manual"));
    context.craters = true;
    if (!craters.generateRandom) {
        return;
    }
    if (!(craters.maxDiameter > craters.minDiameter)) {
        throw ConfigError(Child(path, "max_diameter_m"), "must exceed min_diameter_m");
    }
    CheckModelRange(craters, models, Child(path, "min_diameter_m"));
    AddExpectedCraters(craters, models, path, context);
}

/// @throws ConfigError at a boulder layer's "density_map" or "slope_map" for an invalid map.
void CheckBoulderMaps(const BoulderLayerOptions& boulders, const std::string& path) {
    if (boulders.densityMap) {
        At(Child(path, "density_map"), [&] { ValidateDensityMap(*boulders.densityMap); });
    }
    if (boulders.slopeMap) {
        At(Child(path, "slope_map"), [&] { ValidateSizeSlopeMap(*boulders.slopeMap); });
    }
}

bool IsGolombekRapp(const BoulderSizesSpec& sizes) {
    return std::holds_alternative<GolombekRappBouldersSpec>(sizes);
}

/// @throws ConfigError at a Golombek-Rapp term changed from its default, such as "coverage", when
/// no size model of the layer is Golombek-Rapp and so none would use it.
void CheckGolombekRappTerms(const BoulderLayerSpec& spec, const std::string& path) {
    const BoulderLayerSpec defaults{};
    const BoulderSizes& sizes = spec.boulders.sizes;
    const BoulderSizes& unset = defaults.boulders.sizes;
    const bool layer = IsGolombekRapp(spec.sizes);
    const bool ejecta = IsGolombekRapp(spec.ejectaSizes.value_or(spec.sizes));
    const auto check = [](bool used, double value, double fallback, const std::string& at,
                          const char* fix) {
        if (!used && value != fallback) {
            throw ConfigError(at, std::string("is only used by Golombek-Rapp sizes; set ") + fix +
                                      " or leave it out");
        }
    };
    const char* const layerFix = R"("sizes": {"type": "golombek_rapp"})";
    check(layer, sizes.coverage, unset.coverage, Child(path, "coverage"), layerFix);
    check(layer || ejecta, sizes.qConstant, unset.qConstant, Child(path, "qa"), layerFix);
    check(layer || ejecta, sizes.qCoverageCoefficient, unset.qCoverageCoefficient,
          Child(path, "qb"), layerFix);
    check(ejecta, spec.boulders.ejecta.coverage, defaults.boulders.ejecta.coverage,
          Child(Child(path, "ejecta"), "coverage"), R"("ejecta_sizes": {"type": "golombek_rapp"})");
}

/// @throws ConfigError at a manual boulder that is invalid or shares an earlier one's ID.
void CheckManualBoulders(const std::vector<ManualBoulder>& manual, const std::string& path) {
    std::unordered_map<std::uint64_t, std::size_t> ids;
    for (std::size_t n = 0; n < manual.size(); ++n) {
        At(Element(path, n), [&] { ValidateManualBoulder(manual[n]); });
        const auto added = ids.emplace(ManualBoulderId(manual[n]), n);
        if (!added.second) {
            throw ConfigError(Element(path, n),
                              "shares its ID with " + Element(path, added.first->second));
        }
    }
}

void CheckLayerSpec(const BoulderLayerSpec& spec, const std::string& path, LayerChecks& context) {
    CheckSourceResolves(spec.boulders.sourceResolvesPixels, path);
    CheckManualBoulders(spec.manual, Child(path, "manual"));
    const BoulderLayerOptions boulders = BuildBoulderOptions(spec, path, context.directory);
    CheckBoulderMaps(boulders, path);
    context.boulders = true;
    if (!boulders.generateRandom) {
        At(path, [&] { ValidateBoulderLayerOptions(boulders); });
        return;
    }
    CheckGolombekRappTerms(spec, path);
    if (IsGolombekRapp(spec.sizes) && !(boulders.sizes.coverage > 0.0)) {
        throw ConfigError(Child(path, "coverage"),
                          "must be above 0 to generate random boulders; set generate_random to "
                          "false to place only the manual ones");
    }
    At(path, [&] { ValidateBoulderLayerOptions(boulders); });
    if (boulders.ejecta.MinCraterDiameter() > 0.0 && !context.craters) {
        throw ConfigError(Child(Child(path, "ejecta"), "min_crater_diameter_m"),
                          "ejecta need a crater layer before this one");
    }
    const BoulderSizes& sizes = boulders.sizes;
    for (const Extent& extent : context.extents) {
        const double minDiameter =
            extent.listed
                ? sizes.minDiameter
                : std::fmax(sizes.minDiameter, boulders.fade.startSamples * extent.spacing);
        if (minDiameter >= sizes.maxDiameter) {
            continue;
        }
        const double expected =
            BoulderCandidates(boulders)->ExpectedCount(extent.area, minDiameter, sizes.maxDiameter);
        CheckExpectedCount(expected, "candidate boulders", "raise min_diameter_m or lower coverage",
                           Child(path, "min_diameter_m"));
    }
}

void CheckLayerSpec(const SecondaryLayerSpec& layer, const std::string& path,
                    LayerChecks& context) {
    if (!context.craters) {
        throw ConfigError(path, "secondaries need a crater layer before this one");
    }
    if (!layer.secondaries.onsetCount) {
        throw ConfigError(Child(path, "onset_count"),
                          "required: the sources give secondary sizes, not their number");
    }
    CheckSourceResolves(layer.secondaries.sourceResolvesPixels, path);
    At(path, [&] { ValidateSecondaryCraterOptions(layer.secondaries); });
}

/// Requires a source with posts for a layer at `path` that estimates its settings from it.
/// @throws ConfigError at the layer's "estimate_from_base" without one.
void CheckEstimateBase(bool estimate, const std::string& path, const LayerChecks& context) {
    if (estimate && !context.posts) {
        throw ConfigError(Child(path, "estimate_from_base"),
                          "needs a source with posts to estimate from, a raster or synthetic noise "
                          "with a pixel size; without one give the values instead");
    }
}

void CheckLayerSpec(const NoiseLayerSpec& noise, const std::string& path, LayerChecks& context) {
    CheckNoiseStack(noise.stack, Child(path, "stack"), false);
    CheckEstimateBase(noise.estimateFromBase, path, context);
    CheckSourceResolves(noise.sourceResolvesPixels, path);
}

void CheckLayerSpec(const SandShadowLayerSpec& layer, const std::string& path,
                    LayerChecks& context) {
    if (!context.boulders) {
        throw ConfigError(path, "sand shadows need a boulder layer before this one");
    }
    At(path, [&] { ValidateSandShadowOptions(layer.shadows); });
}

void CheckLayerSpec(const RoughnessLayerSpec& layer, const std::string& path,
                    LayerChecks& context) {
    At(path, [&] { ValidateRoughnessOptions(layer.Options()); });
    CheckEstimateBase(layer.estimateFromBase, path, context);
}

void CheckLayerSpec(const DuneLayerSpec& layer, const std::string& path, LayerChecks&) {
    CheckSourceResolves(layer.dunes.sourceResolvesPixels, path);
    At(path, [&] { ValidateDuneOptions(layer.dunes); });
}

void CheckLayerSpec(const PavementLayerSpec& layer, const std::string& path, LayerChecks&) {
    At(path, [&] { ValidatePavementOptions(layer.pavement); });
}

void CheckLayerSpec(const StrataLayerSpec& layer, const std::string& path, LayerChecks&) {
    At(path, [&] { ValidateStrataOptions(layer.strata); });
}

void CheckLayerSpec(const SandFillLayerSpec& layer, const std::string& path, LayerChecks&) {
    At(path, [&] { ValidateSandFillOptions(layer.sandFill); });
}

void CheckLayerSpec(const BedformLayerSpec& layer, const std::string& path, LayerChecks&) {
    CheckSourceResolves(layer.bedforms.sourceResolvesPixels, path);
    At(path, [&] { ValidateBedformOptions(layer.bedforms); });
}

void CheckLayerSpec(const ScaleLayerSpec& layer, const std::string& path, LayerChecks&) {
    At(path, [&] { ScaleLayer(layer.scale, layer.offset); });
}

void CheckLayerSpec(const ClampLayerSpec& layer, const std::string& path, LayerChecks&) {
    At(path, [&] { ClampLayer(layer.min, layer.max); });
}

/// Checks a GeoTIFF a base or layer at `path` reads: a path to a readable file with the band, in
/// coordinates that suit the surface, and returns its header.
/// @throws ConfigError at the spec's "path" or "band" otherwise.
GeoTiffInfo CheckGeoTiffRead(const std::string& file, std::size_t band, const std::string& path,
                             const std::filesystem::path& directory, const Surface& surface) {
    const std::string filePath = Child(path, "path");
    CheckNonEmpty(file, filePath);
    const GeoTiffInfo info =
        AtFile(filePath, [&] { return ReadGeoTiffInfo(Resolve(file, directory)); });
    if (band >= info.bands) {
        throw ConfigError(Child(path, "band"), "the file has " + std::to_string(info.bands) +
                                                   (info.bands == 1 ? " band" : " bands"));
    }
    if (info.geoKeys.IsGeographic() && !surface.IsBody()) {
        throw ConfigError(filePath,
                          "the raster's CRS is geographic (degrees); craters are sized in "
                          "meters, so reproject it first, for example with gdalwarp -t_srs, "
                          "or set the body to a sphere");
    }
    if (surface.IsBody() && !info.geoKeys.Empty() && !info.geoKeys.IsGeographic()) {
        throw ConfigError(
            filePath, "on a body the raster must be in longitude and latitude (a geographic CRS)");
    }
    return info;
}

/// The cells of `lattice`, the grid a GeoTIFF read at `path` gives over the whole file, that the
/// read's `crop` meets; the whole lattice without a crop.
/// @throws ConfigError under the read's "crop" for a crop that is not finite, is empty or misses
/// the file.
RasterGrid CheckGeoTiffCrop(const std::optional<Bounds2d>& crop, const RasterGrid& lattice,
                            const std::string& path) {
    if (!crop) {
        return lattice;
    }
    const std::string at = Child(path, "crop");
    CheckFinite(crop->minX, Child(at, "min_x"));
    CheckFinite(crop->minY, Child(at, "min_y"));
    CheckFinite(crop->maxX, Child(at, "max_x"));
    CheckFinite(crop->maxY, Child(at, "max_y"));
    if (!(crop->maxX > crop->minX)) {
        throw ConfigError(Child(at, "max_x"), "must exceed min_x");
    }
    if (!(crop->maxY > crop->minY)) {
        throw ConfigError(Child(at, "max_y"), "must exceed min_y");
    }
    const PixelWindow cells = PixelWindowIn(lattice, *crop);
    if (cells.Empty()) {
        throw ConfigError(at, "misses the file");
    }
    return Subgrid(lattice, cells);
}

void CheckLayerSpec(const SpectralContinuationLayerSpec& layer, const std::string& path,
                    LayerChecks& context) {
    if (!context.posts && !(layer.pixelSize && layer.slope && layer.octaveRms)) {
        throw ConfigError(path, "without a source with posts to fit its spectrum to needs "
                                "pixel_size, slope and octave_rms");
    }
    At(path, [&] {
        ValidateSpectrumFitBand(layer.fit);
        SpectralContinuationOptions options;
        options.pixelSize = layer.pixelSize.value_or(options.pixelSize);
        options.slope = layer.slope.value_or(options.slope);
        options.octaveRms = layer.octaveRms.value_or(options.octaveRms);
        options.minWavelength = layer.minWavelength;
        options.taperOctaves = layer.taperOctaves;
        options.fade = layer.fade;
        ValidateSpectralContinuationOptions(options);
    });
}

void CheckLayerSpec(const MaskedLayerSpec& layer, const std::string& path, LayerChecks& context) {
    At(Child(path, "shape"), [&] { ValidateRegionShape(BuildRegionShape(layer.shape)); });
    const bool empty = std::visit(
        Overloaded{
            [](const RectangleRegionSpec& r) { return !(r.maxX > r.minX && r.maxY > r.minY); },
            [](const CircleRegionSpec& c) { return !(c.radius > 0.0); },
        },
        layer.shape);
    if (empty) {
        throw ConfigError(Child(path, "shape"),
                          "required: the shape has no area; give the rectangle's bounds or the "
                          "circle's radius");
    }
    // Crater layers inside a masked layer count for neither later layers nor the summary.
    LayerChecks inner = context;
    CheckLayer(*layer.layer, Child(path, "layer"), inner);
}

void CheckLayer(const LayerSpec& spec, const std::string& path, LayerChecks& context) {
    std::visit([&](const auto& layer) { CheckLayerSpec(layer, path, context); }, spec);
}

std::optional<RasterGrid> DescribeSourceAt(const SourceSpec& source,
                                           const std::filesystem::path& directory,
                                           const std::string& path, const Surface& surface);

std::optional<RasterGrid> DescribeChain(const ChainSourceSpec& chain,
                                        const std::filesystem::path& directory,
                                        const std::string& path, const Surface& surface) {
    const std::string sources = Child(path, "sources");
    if (chain.sources.empty()) {
        throw ConfigError(sources, "must not be empty");
    }
    std::optional<RasterGrid> grid;
    for (std::size_t n = 0; n < chain.sources.size(); ++n) {
        const std::optional<RasterGrid> described =
            DescribeSourceAt(chain.sources[n], directory, Element(sources, n), surface);
        if (n == 0) {
            grid = described;
        }
    }
    return grid;
}

/// Checks an upsampling at `path`: a kernel, diamond-square with valid settings, or a type
/// registered with `RegisterUpsampler`.
/// @throws ConfigError at `path` for an unknown type, or below it for invalid settings.
void CheckUpsampling(const UpsamplingSpec& upsampling, const std::string& path) {
    if (KernelUpsamplerMethod(upsampling.type) || IsUpsamplerRegistered(upsampling.type)) {
        return;
    }
    if (upsampling.type == "diamond_square") {
        At(path, [&] { ValidateDiamondSquareOptions(upsampling.diamondSquare); });
        return;
    }
    std::string expected;
    for (const std::string& name : KernelUpsamplerNames()) {
        expected += name + ", ";
    }
    expected += "diamond_square";
    for (const std::string& name : RegisteredUpsamplerNames()) {
        expected += ", " + name;
    }
    throw ConfigError(path, "unknown upsampling type \"" + upsampling.type +
                                "\"; expected one of: " + expected);
}

/// Checks a synthetic source at `path`: its stack, and upsampling only with a pixel size.
/// @throws ConfigError below `path` for an invalid stack or upsampling.
void CheckSyntheticSource(const SyntheticSourceSpec& synthetic, const std::string& path) {
    CheckNoiseStack(synthetic.stack, Child(path, "stack"), true);
    if (synthetic.upsampling) {
        const std::string at = Child(path, "upsampling");
        if (!synthetic.pixelSize) {
            throw ConfigError(at, "needs pixel_size_m: only noise sampled at posts is upsampled");
        }
        CheckUpsampling(*synthetic.upsampling, at);
    }
}

RasterGrid DescribeRaster(const RasterSourceSpec& raster, const std::filesystem::path& directory,
                          const std::string& path, const Surface& surface) {
    const GeoTiffInfo info =
        CheckGeoTiffRead(raster.path, raster.read.band, path, directory, surface);
    RasterGrid grid{info.width, info.height, info.transform};
    std::string sizePath = Child(path, "path");
    if (raster.read.crop) {
        grid = CheckGeoTiffCrop(raster.read.crop, grid, path);
        sizePath = Child(path, "crop");
    }
    CheckGridSize(grid.width, grid.height, sizePath);
    CheckUpsampling(raster.upsampling, Child(path, "upsampling"));
    return grid;
}

std::optional<RasterGrid> DescribeSourceAt(const SourceSpec& source,
                                           const std::filesystem::path& directory,
                                           const std::string& path, const Surface& surface) {
    return std::visit(
        Overloaded{
            [&](const RasterSourceSpec& s) -> std::optional<RasterGrid> {
                return DescribeRaster(s, directory, path, surface);
            },
            [&](const ChainSourceSpec& s) { return DescribeChain(s, directory, path, surface); },
            [&](const SyntheticSourceSpec& s) -> std::optional<RasterGrid> {
                CheckSyntheticSource(s, path);
                return std::nullopt;
            },
        },
        source);
}

/// Whether a source has posts that layers can estimate settings from: a raster, a chain whose
/// first source has, or synthetic noise with a pixel size.
bool HasPosts(const SourceSpec& source) {
    return std::visit(Overloaded{
                          [](const RasterSourceSpec&) { return true; },
                          [](const ChainSourceSpec& s) {
                              return !s.sources.empty() && HasPosts(s.sources.front());
                          },
                          [](const SyntheticSourceSpec& s) { return s.pixelSize.has_value(); },
                      },
                      source);
}

/// Rows of a whole-body list's grid above which its spacing is too fine.
constexpr double kMaxWholeBodyRows = 1e9;

/// Checks an [x, y] pair at `path`: empty for unset, or two finite numbers.
/// @throws ConfigError at `path` otherwise.
void CheckPair(const std::vector<double>& pair, const std::string& path) {
    if (!pair.empty() && pair.size() != 2) {
        throw ConfigError(path, "needs two numbers, [x, y]");
    }
    for (const double value : pair) {
        CheckFinite(value, path);
    }
}

/// Checks an extent at `path` and returns its box (`ExtentBox`).
/// @throws ConfigError at `path` or below it for an incomplete, mixed or empty box.
Bounds2d CheckExtent(const ExtentSpec& extent, const std::string& path) {
    CheckPair(extent.center, Child(path, "center"));
    CheckPair(extent.size, Child(path, "size"));
    CheckPair(extent.min, Child(path, "min"));
    CheckPair(extent.max, Child(path, "max"));
    const bool corners = !extent.min.empty() || !extent.max.empty();
    const bool centered = !extent.center.empty() || !extent.size.empty();
    if (corners && centered) {
        throw ConfigError(path, "give center and size, or min and max, not both");
    }
    if (!corners && !centered) {
        throw ConfigError(path, "needs center and size, or min and max");
    }
    if (corners) {
        if (extent.min.empty() || extent.max.empty()) {
            throw ConfigError(Child(path, extent.min.empty() ? "min" : "max"),
                              "required with " + std::string(extent.min.empty() ? "max" : "min"));
        }
        if (!(extent.max[0] > extent.min[0] && extent.max[1] > extent.min[1])) {
            throw ConfigError(Child(path, "max"), "must lie above min along x and y");
        }
    } else {
        if (extent.center.empty() || extent.size.empty()) {
            throw ConfigError(Child(path, extent.center.empty() ? "center" : "size"),
                              "required with " +
                                  std::string(extent.center.empty() ? "size" : "center"));
        }
        if (!(extent.size[0] > 0.0 && extent.size[1] > 0.0)) {
            throw ConfigError(Child(path, "size"), "must be positive");
        }
    }
    return *config::ExtentBox(extent);
}

/// The number `cells`, written whole.
std::string CellText(double cells) {
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.0f", cells);
    return text.data();
}

/// Checks an output's area at `path`: its extent, pixel size and the grid they give.
/// @param listed Whether the output lists features, which may cover the whole body.
/// @throws ConfigError below `path` for an extent of the wrong unit or an invalid one, a missing
/// extent or pixel size without a bounded source, a grid of too many cells or off the body, or a
/// pixel size too fine to list the whole body.
void CheckOutputArea(const OutputAreaSpec& area, const std::string& path, bool listed,
                     const std::optional<RasterGrid>& base, const Surface& surface) {
    const bool body = surface.IsBody();
    const std::string extentKey = body ? "extent_deg" : "extent_m";
    const std::string pixelKey = Child(path, "pixel_size_m");
    if (body ? area.extentM : area.extentDeg) {
        throw ConfigError(Child(path, body ? "extent_m" : "extent_deg"),
                          body ? "on a body give extent_deg, in degrees of longitude and latitude"
                               : "only on a body; give extent_m, in meters, on a plane");
    }
    const std::optional<ExtentSpec>& extent = config::SurfaceExtent(area, surface);
    std::optional<Bounds2d> box;
    if (extent) {
        box = CheckExtent(*extent, Child(path, extentKey));
    }
    if (listed && body && !base && !extent && area.pixelSize) {
        if (180.0 * surface.UnitLength() / *area.pixelSize > kMaxWholeBodyRows) {
            throw ConfigError(pixelKey, "is too fine to list features over the body");
        }
        return;
    }
    if (!box && !base) {
        throw ConfigError(Child(path, extentKey),
                          listed && body
                              ? "required without a raster or chain source, or give pixel_size_m "
                                "alone to list the whole body"
                              : "required without a raster or chain source: the area to cover");
    }
    if (!area.pixelSize && !base) {
        throw ConfigError(pixelKey, "required without a raster or chain source");
    }
    if (!box && !area.pixelSize) {
        return;
    }
    const Bounds2d covered = box ? *box : base->Bounds();
    const double cell = area.pixelSize ? *area.pixelSize / surface.UnitLength() : base->PixelSize();
    const double columns = config::SnappedCells(covered.maxX - covered.minX, cell);
    const double rows = config::SnappedCells(covered.maxY - covered.minY, cell);
    const std::string sizePath = area.pixelSize ? pixelKey : Child(path, extentKey);
    const auto limit = static_cast<double>(kMaxConfigGridSize);
    if (columns > limit || rows > limit) {
        throw ConfigError(sizePath, "gives " + CellText(columns) + "x" + CellText(rows) +
                                        " cells; each side must be " +
                                        config::CountText(kMinConfigGridSize, kMaxConfigGridSize));
    }
    const RasterGrid grid = config::SnappedGrid(covered, cell);
    CheckGridSize(grid.width, grid.height, sizePath);
    const Bounds2d bounds = grid.Bounds();
    const double slack = config::kExtentSnapTolerance * cell;
    if (body && !(bounds.minY >= -90.0 - slack && bounds.maxY <= 90.0 + slack &&
                  bounds.maxX - bounds.minX <= 360.0 + slack)) {
        throw ConfigError(Child(path, extentKey),
                          "the grid must lie within latitudes -90 to 90 and one turn of longitude");
    }
}

/// The grid the output at `path` covers (`OutputGrid`), its area checked; none for an output
/// without an area, a body mesh or a report.
/// @throws ConfigError as `CheckOutputArea`.
std::optional<RasterGrid> CheckOutputGrid(const OutputSpec& output, const std::string& path,
                                          const std::optional<RasterGrid>& base,
                                          const Surface& surface) {
    const OutputAreaSpec* area = config::OutputArea(output);
    if (!area) {
        return std::nullopt;
    }
    CheckOutputArea(*area, path, config::ListsFeatures(output), base, surface);
    return config::OutputGrid(output, base, surface);
}

/// What outputs are checked against.
struct OutputChecks {
    bool body = false;       ///< Whether the surface is a body.
    bool albedo = false;     ///< Whether the configuration has an albedo.
    bool materials = false;  ///< Whether the configuration has a material map.
};

void CheckMeshFile(const std::string& file, const std::string& path) {
    if (!IsMeshPath(file) || IsIcqPath(file)) {
        throw ConfigError(path, "must end in .obj, .gltf, .glb or .stl");
    }
}

void CheckGeoTiffOutput(const GeoTiffOutput& output, const std::string& path) {
    if (output.compression == TiffCompression::PackBits) {
        throw ConfigError(Child(path, "compression"),
                          "packbits is read-only; use none, lzw or deflate");
    }
    if (output.tileSize % kTileSizeMultiple != 0) {
        throw ConfigError(Child(path, "tile_size"),
                          "must be a multiple of " + std::to_string(kTileSizeMultiple));
    }
}

void CheckOutput(const HeightmapOutput& output, const std::string& path, const OutputChecks&) {
    CheckGeoTiffOutput(output, path);
}

void CheckOutput(const AlbedoOutput& output, const std::string& path, const OutputChecks& context) {
    if (!context.albedo) {
        throw ConfigError(path, "the configuration has no albedo");
    }
    CheckGeoTiffOutput(output, path);
}

/// An output that writes the material map or its photometry.
template <typename Output>
constexpr bool MaterialMapOutput =
    std::is_same_v<Output, MaterialsOutput> || std::is_same_v<Output, PhotometryOutput>;

template <typename Output, std::enable_if_t<MaterialMapOutput<Output>, int> = 0>
void CheckOutput(const Output& output, const std::string& path, const OutputChecks& context) {
    if (!context.materials) {
        throw ConfigError(path, "the configuration has no materials");
    }
    CheckGeoTiffOutput(output, path);
}

void CheckOutput(const ListOutput& /*output*/, const std::string& /*path*/, const OutputChecks&) {}

void CheckOutput(const BoulderModelsOutput& output, const std::string& path, const OutputChecks&) {
    if (!output.path.empty()) {
        CheckMeshFile(output.path, Child(path, "path"));
    }
}

void CheckOutput(const MeshOutput& output, const std::string& path, const OutputChecks& context) {
    if (!context.body) {
        throw ConfigError(path, "a mesh needs a whole body: a sphere, ellipsoid or shape model; "
                                "use terrain_mesh on a plane");
    }
    if (!output.path.empty() && !IsIcqPath(output.path)) {
        CheckMeshFile(output.path, Child(path, "path"));
    }
}

void CheckOutput(const TerrainMeshOutput& output, const std::string& path,
                 const OutputChecks& context) {
    if (!output.path.empty()) {
        CheckMeshFile(output.path, Child(path, "path"));
    }
    for (std::size_t c = 0; c < output.cutouts.size(); ++c) {
        CheckExtent(output.cutouts[c], Element(Child(path, "cutouts"), c));
    }
    const std::string site = Child(path, "site_deg");
    CheckPair(output.siteDeg, site);
    if (!output.siteDeg.empty() && !context.body) {
        throw ConfigError(site, "only on a body, where the mesh's frame is centered on the site");
    }
}

void CheckOutput(const ReportOutput& /*output*/, const std::string& /*path*/, const OutputChecks&) {
}

/// Whether an output's body mesh is an ICQ shape model rather than a mesh.
bool WritesIcq(const MeshOutput& output) {
    return IsIcqPath(output.path);
}

/// Checks that outputs sharing one result agree on it: body meshes of one kind (mesh or ICQ) on
/// their cells and spacing, and terrain meshes over one grid on their cutouts, skirt and site.
/// @throws ConfigError at the first output that disagrees with an earlier one.
void CheckSharedResults(const TerrainGenConfig& config, const ConfigSummary& summary) {
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const std::string path = Element("outputs", i);
        for (std::size_t j = 0; j < i; ++j) {
            const auto* mesh = std::get_if<MeshOutput>(&config.outputs[i]);
            const auto* earlier = std::get_if<MeshOutput>(&config.outputs[j]);
            if (mesh && earlier && WritesIcq(*mesh) == WritesIcq(*earlier) &&
                (mesh->cellsPerFace != earlier->cellsPerFace ||
                 mesh->spacing != earlier->spacing)) {
                throw ConfigError(path, "body meshes share one result: give the same "
                                        "cells_per_face and spacing as " +
                                            Element("outputs", j));
            }
            const auto* terrain = std::get_if<TerrainMeshOutput>(&config.outputs[i]);
            const auto* before = std::get_if<TerrainMeshOutput>(&config.outputs[j]);
            if (terrain && before && summary.outputGrids[i] == summary.outputGrids[j] &&
                (terrain->cutouts != before->cutouts || terrain->skirt != before->skirt ||
                 terrain->siteDeg != before->siteDeg)) {
                throw ConfigError(path, "terrain meshes over one grid share one result: give the "
                                        "same cutouts, skirt_m and site_deg as " +
                                            Element("outputs", j));
            }
        }
    }
}

/// Checks each output and its grid, records the grids in `summary` and returns where each output
/// with a grid evaluates the layers.
/// @throws ConfigError under "outputs" for an invalid output.
std::vector<Extent> CheckOutputs(const TerrainGenConfig& config, const Surface& surface,
                                 ConfigSummary& summary) {
    const OutputChecks checks{surface.IsBody(), config.albedo.has_value(),
                              config.materials.has_value()};
    std::vector<Extent> extents;
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const OutputSpec& output = config.outputs[i];
        const std::string path = Element("outputs", i);
        std::visit([&](const auto& o) { CheckOutput(o, path, checks); }, output);
        const std::optional<RasterGrid> grid =
            CheckOutputGrid(output, path, summary.source, surface);
        summary.outputGrids.push_back(grid);
        if (grid) {
            const bool listed = config::ListsFeatures(output);
            const double requested = config::OutputSpacing(output, summary.source, surface);
            const double spacing = listed ? ListedSpacing(*grid, requested, surface)
                                          : std::fmax(requested, grid->Spacing(surface));
            extents.push_back({grid->SurfaceArea(surface), spacing, listed});
        }
    }
    CheckSharedResults(config, summary);
    return extents;
}

/// Checks each layer in order over the output extents, and records in `summary` the most craters
/// the top-level crater layers are expected to generate.
/// @throws ConfigError under "layers" for an invalid layer.
void CheckLayers(const TerrainGenConfig& config, const std::vector<Extent>& extents,
                 const std::filesystem::path& directory, const Surface& surface,
                 ConfigSummary& summary) {
    LayerChecks layers{extents, directory, surface};
    layers.posts = config.source && HasPosts(*config.source);
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        CheckLayer(config.layers[i], Element("layers", i), layers);
    }
    if (!layers.expectedCraters.empty()) {
        summary.expectedCraters =
            *std::max_element(layers.expectedCraters.begin(), layers.expectedCraters.end());
    }
}

/// The material classes, with their names, a configuration's surface can have.
std::vector<std::pair<MaterialClass, const char*>>
PresentMaterials(const TerrainGenConfig& config) {
    const MaterialsSpec& materials = *config.materials;
    std::vector<std::pair<MaterialClass, const char*>> present{{MaterialClass::Soil, "soil"}};
    if (materials.options.exposure ||
        config::HasLayer<PavementLayerSpec, StrataLayerSpec>(config.layers)) {
        present.emplace_back(MaterialClass::Bedrock, "bedrock");
    }
    if (materials.options.dustCover > 0.0) {
        present.emplace_back(MaterialClass::Dust, "dust");
    }
    if (config::HasLayer<DuneLayerSpec, BedformLayerSpec, SandFillLayerSpec, SandShadowLayerSpec>(
            config.layers)) {
        present.emplace_back(MaterialClass::Sand, "sand");
    }
    if (config::HasLayer<BoulderLayerSpec>(config.layers)) {
        present.emplace_back(MaterialClass::Rock, "rock");
    }
    return present;
}

/// @throws ConfigError under "materials" for invalid options or photometry, or a photometry output
/// without parameters for a class the surface can have.
void CheckMaterials(const TerrainGenConfig& config) {
    const MaterialsSpec& materials = *config.materials;
    At("materials", [&] { ValidateMaterialOptions(materials.options); });
    const MaterialPhotometry photometry = config::BuildPhotometry(materials.photometry);
    At("materials.photometry", [&] { ValidateMaterialPhotometry(photometry); });
    if (!config::HasOutput<PhotometryOutput>(config.outputs)) {
        return;
    }
    for (const auto& [material, name] : PresentMaterials(config)) {
        if (!photometry[static_cast<std::size_t>(material)]) {
            throw ConfigError(Child("materials.photometry", name),
                              "required by the photometry output: the surface has some");
        }
    }
}

/// @throws ConfigError at `path`, the body, for a shape model without a path.
void CheckBody(const SurfaceSpec& body, const std::string& path) {
    if (const auto* shape = std::get_if<ShapeModelSurfaceSpec>(&body)) {
        CheckNonEmpty(shape->path, Child(path, "path"));
    }
}

void CheckAlbedoSource(const SyntheticAlbedoSpec& spec, const std::filesystem::path& /*directory*/,
                       const std::string& path) {
    CheckNoiseStack(spec.hueNoise, Child(path, "hue_noise_deg"), true);
    CheckNoiseStack(spec.saturationNoise, Child(path, "saturation_noise"), true);
    CheckNoiseStack(spec.valueNoise, Child(path, "value_noise"), true);
    At(path, [&] { ValidateSyntheticAlbedoOptions(spec.Params()); });
}

void CheckAlbedoSource(const RasterAlbedoSpec& spec, const std::filesystem::path& directory,
                       const std::string& path) {
    const std::string file = Child(path, "path");
    const std::string bands = Child(path, "bands");
    CheckNonEmpty(spec.path, file);
    if (spec.bands.size() != 1 && spec.bands.size() != kMaxAlbedoBands) {
        throw ConfigError(bands, "needs 1 or " + std::to_string(kMaxAlbedoBands) + " bands");
    }
    const GeoTiffInfo info =
        AtFile(file, [&] { return ReadGeoTiffInfo(Resolve(spec.path, directory)); });
    for (const std::size_t band : spec.bands) {
        if (band >= info.bands) {
            throw ConfigError(bands, "the file has " + std::to_string(info.bands) +
                                         (info.bands == 1 ? " band" : " bands"));
        }
    }
    if (info.transform.IsRotated()) {
        throw ConfigError(file, "rotated albedo maps are not supported");
    }
    if ((spec.scale && !std::isfinite(*spec.scale)) ||
        (spec.offset && !std::isfinite(*spec.offset))) {
        throw ConfigError(Child(path, "scale"), "the scale and offset must be finite");
    }
    At(Child(path, "detail"), [&] { ValidateAlbedoDetail(spec.options.detail); });
}

/// The top-level crater and boulder layers albedo layers can read.
struct AlbedoChecks {
    std::size_t craterLayers = 0;
    std::size_t boulderLayers = 0;
};

void CheckAlbedoLayer(const CraterAlbedoSpec& craters, const std::string& path,
                      const AlbedoChecks& context) {
    if (context.craterLayers == 0) {
        throw ConfigError(path, "crater albedo needs a crater layer");
    }
    if (craters.craterLayer && *craters.craterLayer >= context.craterLayers) {
        throw ConfigError(Child(path, "crater_layer"),
                          "there are " + std::to_string(context.craterLayers) + " crater layers");
    }
    At(path, [&] { ValidateCraterAlbedoOptions(craters.options); });
}

void CheckAlbedoLayer(const RockAlbedoSpec& rocks, const std::string& path,
                      const AlbedoChecks& context) {
    if (context.boulderLayers == 0) {
        throw ConfigError(path, "rock albedo needs a top-level boulder layer");
    }
    if (rocks.boulderLayer && *rocks.boulderLayer >= context.boulderLayers) {
        throw ConfigError(Child(path, "boulder_layer"),
                          "there are " + std::to_string(context.boulderLayers) + " boulder layers");
    }
    At(path, [&] { ValidateRockAlbedoOptions(rocks.options); });
}

void CheckAlbedoLayer(const SplotchAlbedoSpec& splotches, const std::string& path,
                      const AlbedoChecks&) {
    At(path, [&] { ValidateSplotchAlbedoOptions(splotches.options); });
}

void CheckAlbedoLayer(const BlastZoneAlbedoSpec& zones, const std::string& path,
                      const AlbedoChecks&) {
    for (std::size_t z = 0; z < zones.zones.size(); ++z) {
        At(Element(Child(path, "zones"), z), [&] { ValidateBlastZone(zones.zones[z]); });
    }
}

void CheckAlbedoLayer(const SlopeAlbedoSpec& slopes, const std::string& path, const AlbedoChecks&) {
    At(path, [&] { ValidateSlopeAlbedoOptions(slopes.options); });
}

}  // namespace

std::optional<RasterGrid> DescribeSource(const SourceSpec& source,
                                         const std::filesystem::path& directory,
                                         const Surface& surface) {
    return DescribeSourceAt(source, directory, "source", surface);
}

void ValidateAlbedo(const TerrainGenConfig& config, const std::filesystem::path& directory) {
    const AlbedoSpec& albedo = *config.albedo;
    std::visit([&](const auto& source) { CheckAlbedoSource(source, directory, "albedo.source"); },
               albedo.source);
    const AlbedoChecks context{config::CountLayers<CraterLayerSpec>(config.layers),
                               config::CountLayers<BoulderLayerSpec>(config.layers)};
    for (std::size_t i = 0; i < albedo.layers.size(); ++i) {
        const std::string path = Element("albedo.layers", i);
        std::visit([&](const auto& layer) { CheckAlbedoLayer(layer, path, context); },
                   albedo.layers[i]);
    }
}

ConfigSummary ValidateConfig(const TerrainGenConfig& config,
                             const std::filesystem::path& directory) {
    CheckConfigRanges(config);
    CheckBody(config.body, "body");
    ConfigSummary summary;
    const Surface surface = BuildSurface(config.body, directory);
    if (config.source) {
        summary.source = DescribeSource(*config.source, directory, surface);
    }
    const std::vector<Extent> extents = CheckOutputs(config, surface, summary);
    CheckLayers(config, extents, directory, surface, summary);
    if (config.albedo) {
        ValidateAlbedo(config, directory);
    }
    if (config.materials) {
        CheckMaterials(config);
    }
    return summary;
}

}  // namespace terraingen
