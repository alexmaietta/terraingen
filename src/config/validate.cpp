// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/config/validate.h"

#include "config/build_steps.h"
#include "config/fields/checks.h"
#include "config/fields/common.h"
#include "config/fields/materials.h"
#include "layers/craters/shaping/resolved_state.h"
#include "terraingen/albedo/crater_albedo.h"
#include "terraingen/albedo/raster_albedo.h"
#include "terraingen/albedo/synthetic.h"
#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/material.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/noise/fractals.h"
#include "terraingen/noise/validate.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/shape_model_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/synthetic_source.h"
#include "terraingen/sources/upsampling.h"
#include "util/overloaded.h"
#include "util/text.h"

#include <algorithm>
#include <cmath>
#include <memory>
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
using config::BuildRegionShape;
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

/// @throws ConfigError for an invalid layer of a stack; its length is checked by its field.
template <typename Spec>
void CheckNoiseStack(const std::vector<Spec>& layers, const std::string& path) {
    for (std::size_t i = 0; i < layers.size(); ++i) {
        At(Element(path, i), [&] { ValidateFractal(layers[i].Effective()); });
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

/// @throws ConfigError at a manual crater with a rim height but no depth.
void CheckManualCraters(const std::vector<Crater>& manual, const std::string& path) {
    for (std::size_t n = 0; n < manual.size(); ++n) {
        if (!manual[n].depth && manual[n].rimHeight) {
            throw ConfigError(Child(Element(path, n), "rim_height_m"), "needs a depth");
        }
    }
}

/// @throws ConfigError at a manual crater whose depth and rim height the layer's profile cannot
/// take (`ResolvedCraterState`).
void CheckExplicitShapes(const std::vector<Crater>& manual, const CraterModels& models,
                         const std::string& path) {
    for (std::size_t n = 0; n < manual.size(); ++n) {
        if (manual[n].depth) {
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

/// @throws ConfigError at "ages" when steady-state ages name another production function,
/// chronology, surface age or equilibrium than a density of a production surface capped by an
/// equilibrium, so the ages would not follow the craters drawn.
void CheckMatchingAges(const CraterLayerSpec& craters, const std::string& path) {
    const auto* steady = craters.ages ? std::get_if<SteadyStateAgesSpec>(&*craters.ages) : nullptr;
    const std::optional<SteadyStateAgesSpec> matching = config::MatchingAges(craters.density);
    if (!steady || !matching) {
        return;
    }
    if (matching->production != steady->production || matching->chronology != steady->chronology ||
        matching->ageGyr != steady->ageGyr || matching->equilibrium != steady->equilibrium) {
        throw ConfigError(Child(path, "ages"),
                          "steady-state ages must name the production function, chronology, "
                          "age_gyr and equilibrium of the density's production surface and "
                          "equilibrium, or be left out to follow them");
    }
}

void CheckLayerSpec(const CraterLayerSpec& craters, const std::string& path, LayerChecks& context) {
    CheckManualCraters(craters.manual, Child(path, "manual"));
    At(Child(path, "spacing"), [&] { ValidateCraterSpacing(craters.spacing); });
    At(Child(path, "apply"), [&] { ValidateCraterApplyOptions(craters.apply); });
    CheckDiffusivity(craters.degradation, Child(path, "degradation"));
    CheckMatchingAges(craters, path);
    context.craters = true;
    if (craters.generateRandom && !(craters.maxDiameter > craters.minDiameter)) {
        throw ConfigError(Child(path, "max_diameter_m"), "must exceed min_diameter_m");
    }
}

/// Checks a built crater layer's models: its manual shapes and, when it generates craters, the
/// count expected.
void CheckBuiltCraters(const CraterLayerSpec& craters, const CraterLayer& built,
                       const std::string& path, LayerChecks& context) {
    const CraterModels& models = built.Models();
    CheckExplicitShapes(craters.manual, models, Child(path, "manual"));
    if (craters.generateRandom) {
        AddExpectedCraters(craters, models, path, context);
    }
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
    CheckManualBoulders(spec.manual, Child(path, "manual"));
    context.boulders = true;
}

/// Checks the candidates a built boulder layer that generates boulders is expected to draw over
/// each extent.
void CheckBuiltBoulders(const BoulderLayer& built, const std::string& path,
                        const LayerChecks& context) {
    const BoulderLayerOptions& boulders = built.Options();
    if (!boulders.generateRandom) {
        return;
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
    At(path, [&] { ValidateSecondaryCraterOptions(layer.secondaries); });
}

/// Requires a source with posts for a layer at `path` that estimates its settings from it.
/// @throws ConfigError at the layer's "estimate_from_source" without one.
void CheckEstimateSource(bool estimate, const std::string& path, const LayerChecks& context) {
    if (estimate && !context.posts) {
        throw ConfigError(Child(path, "estimate_from_source"),
                          "needs a source with posts to estimate from, a raster or synthetic noise "
                          "with a pixel size; without one give the values instead");
    }
}

void CheckLayerSpec(const NoiseLayerSpec& noise, const std::string& path, LayerChecks& context) {
    CheckNoiseStack(noise.stack, Child(path, "stack"));
    CheckEstimateSource(noise.estimateFromSource, path, context);
}

void CheckLayerSpec(const SandShadowLayerSpec&, const std::string& path, LayerChecks& context) {
    if (!context.boulders) {
        throw ConfigError(path, "sand shadows need a boulder layer before this one");
    }
}

void CheckLayerSpec(const RoughnessLayerSpec& layer, const std::string& path,
                    LayerChecks& context) {
    At(path, [&] { ValidateRoughnessOptions(layer.Options()); });
    CheckEstimateSource(layer.estimateFromSource, path, context);
}

/// A layer whose constructor checks all its options, reported at the layer's path as it is built.
template <typename Spec> void CheckLayerSpec(const Spec&, const std::string&, LayerChecks&) {}

/// @throws ConfigError at `path` for a band the GeoTIFF `info` describes does not have.
void CheckBand(std::size_t band, const GeoTiffInfo& info, const std::string& path) {
    if (band >= info.bands) {
        throw ConfigError(path, "the file has " + std::to_string(info.bands) +
                                    (info.bands == 1 ? " band" : " bands"));
    }
}

void CheckLayerSpec(const SpectralContinuationLayerSpec& layer, const std::string& path,
                    LayerChecks& context) {
    if (!context.posts && !(layer.pixelSize && layer.slope && layer.octaveRms)) {
        throw ConfigError(path, "without a source with posts to fit its spectrum to needs "
                                "pixel_size_m, slope and octave_rms_m");
    }
    At(path, [&] {
        ValidateSpectrumFitBand(layer.fit);
        ValidateSpectralContinuationOptions(layer.Options());
    });
}

void CheckLayerSpec(const MaskedLayerSpec& layer, const std::string& path, LayerChecks& context) {
    At(Child(path, "shape"), [&] { ValidateRegionShape(BuildRegionShape(layer.shape)); });
    const bool empty = std::visit(
        Overloaded{
            [](const RectangleRegionSpec& r) { return !(r.maxX > r.minX && r.maxY > r.minY); },
            [](const CircleRegionSpec& c) { return !(c.radius > 0.0); },
            [](const PolygonRegionSpec&) { return false; },
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

/// Checks what a layer needs built to check, `built` being the layer `spec` built, inside a masked
/// layer too.
void CheckBuiltLayer(const LayerSpec& spec, const Layer& built, const std::string& path,
                     LayerChecks& context) {
    if (const auto* craters = std::get_if<CraterLayerSpec>(&spec)) {
        CheckBuiltCraters(*craters, dynamic_cast<const CraterLayer&>(built), path, context);
    } else if (std::holds_alternative<BoulderLayerSpec>(spec)) {
        CheckBuiltBoulders(dynamic_cast<const BoulderLayer&>(built), path, context);
    } else if (const auto* masked = std::get_if<MaskedLayerSpec>(&spec)) {
        LayerChecks inner = context;
        CheckBuiltLayer(*masked->layer, dynamic_cast<const MaskedLayer&>(built).Inner(),
                        Child(path, "layer"), inner);
    }
}

/// The upsampler a spec names, its lengths in the surface's units: a kernel, diamond-square or a
/// type registered with `RegisterUpsampler`.
/// @param path The spec's JSON path, which errors are reported under.
/// @throws ConfigError at `path` for an unknown type or settings its type rejects.
std::shared_ptr<const Upsampler> BuildUpsampler(const UpsamplingSpec& spec, const Surface& surface,
                                                const std::string& path) {
    if (const std::optional<ResampleMethod> method = KernelUpsamplerMethod(spec.type)) {
        return std::make_shared<KernelUpsampler>(*method);
    }
    if (spec.type == "diamond_square") {
        DiamondSquareOptions options = spec.diamondSquare;
        options.minPixelSize /= surface.UnitLength();
        return At(path, [&] {
            ValidateDiamondSquareOptions(options);
            return std::make_shared<DiamondSquareUpsampler>(options);
        });
    }
    if (IsUpsamplerRegistered(spec.type)) {
        return At(path, [&] {
            return MakeRegisteredUpsampler(spec.type, spec.settings.empty() ? "{}" : spec.settings);
        });
    }
    std::string expected;
    for (const std::string& name : KernelUpsamplerNames()) {
        expected += name + ", ";
    }
    expected += "diamond_square";
    for (const std::string& name : RegisteredUpsamplerNames()) {
        expected += ", " + name;
    }
    throw ConfigError(path, "unknown upsampling type \"" + spec.type +
                                "\"; expected one of: " + expected);
}

/// The source a spec describes, its lengths in the surface's units, each part checked before it
/// is built: a raster's file and grid, a chain's sources and seams, a synthetic stack and every
/// upsampling. Its grid is `BoundedGrid` of it: a raster's, a chain's first source's, none for
/// synthetic noise or a shape model.
/// @param path The spec's JSON path, which errors are reported under.
/// @throws ConfigError at `path`, or below it, for what the checks or the sources reject.
std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory,
                                          const std::string& path, const Surface& surface);

/// A raster source: a path to a readable GeoTIFF with the band, in coordinates that suit the
/// surface, over a grid of a size a configuration may request.
std::shared_ptr<const Source> BuildRaster(const RasterSourceSpec& raster,
                                          const std::filesystem::path& directory,
                                          const std::string& path, const Surface& surface) {
    GeoTiffSourceOptions read = raster.read;
    if (read.upsampler) {
        throw ConfigError(Child(path, "upsampling"),
                          "gives the raster's upsampler; leave read.upsampler unset");
    }
    const std::string filePath = Child(path, "path");
    CheckNonEmpty(raster.path, filePath);
    const std::filesystem::path file = Resolve(raster.path, directory);
    const GeoTiffInfo info = AtFile(filePath, [&] { return ReadGeoTiffInfo(file); });
    CheckBand(read.band, info, Child(path, "band"));
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
    if (read.effectivePixelSize) {
        *read.effectivePixelSize /= surface.UnitLength();
    }
    read.upsampler = BuildUpsampler(raster.upsampling, surface, Child(path, "upsampling"));
    const std::string gridPath = read.crop ? Child(path, "crop") : filePath;
    std::shared_ptr<const Source> source =
        At(gridPath, [&] { return std::make_shared<GeoTiffSource>(file, read); });
    const RasterGrid grid = source->Grid();
    CheckGridSize(grid.width, grid.height, gridPath);
    return source;
}

/// A chain of sources, each checked and built in turn, with a seam lattice of a size a
/// configuration may request after each source with bounds but the last.
std::shared_ptr<const Source> BuildChain(const ChainSourceSpec& chain,
                                         const std::filesystem::path& directory,
                                         const std::string& path, const Surface& surface) {
    const std::string sources = Child(path, "sources");
    const ChainSeams seams = config::SurfaceSeams(chain.seams, surface);
    const std::string reachPath = Child(
        Child(path, "seams"), seams.mode == ChainSeamMode::Feather ? "width_m" : "decay_length_m");
    std::vector<std::shared_ptr<const Source>> members;
    for (std::size_t n = 0; n < chain.sources.size(); ++n) {
        members.push_back(BuildSource(chain.sources[n], directory, Element(sources, n), surface));
        const std::optional<RasterGrid> grid = BoundedGrid(*members.back());
        if (grid && seams.mode != ChainSeamMode::None && n + 1 < chain.sources.size()) {
            const RasterGrid lattice = At(reachPath, [&] { return SeamLattice(*grid, seams); });
            CheckGridSize(lattice.width, lattice.height, reachPath);
        }
    }
    return At(path, [&] { return std::make_shared<SourceChain>(std::move(members), seams); });
}

/// Synthetic noise: its stack, and upsampling only with a pixel size.
std::shared_ptr<const Source> BuildSynthetic(const SyntheticSourceSpec& synthetic,
                                             const std::string& path, const Surface& surface) {
    CheckNoiseStack(synthetic.stack, Child(path, "stack"));
    std::shared_ptr<const Upsampler> upsampler;
    if (synthetic.upsampling) {
        const std::string at = Child(path, "upsampling");
        if (!synthetic.pixelSize) {
            throw ConfigError(at, "needs pixel_size_m: only noise sampled at posts is upsampled");
        }
        upsampler = BuildUpsampler(*synthetic.upsampling, surface, at);
    }
    return At(path, [&] {
        return std::make_shared<SyntheticSource>(StackFractals(synthetic.stack),
                                                 static_cast<float>(synthetic.elevation),
                                                 synthetic.pixelSize, upsampler, surface);
    });
}

std::shared_ptr<const Source> BuildSource(const SourceSpec& spec,
                                          const std::filesystem::path& directory,
                                          const std::string& path, const Surface& surface) {
    using Result = std::shared_ptr<const Source>;
    return std::visit(
        Overloaded{
            [&](const RasterSourceSpec& s) { return BuildRaster(s, directory, path, surface); },
            [&](const ChainSourceSpec& s) { return BuildChain(s, directory, path, surface); },
            [&](const SyntheticSourceSpec& s) { return BuildSynthetic(s, path, surface); },
            [&](const ShapeModelSourceSpec& s) -> Result {
                const std::shared_ptr<const Upsampler> upsampler =
                    BuildUpsampler(s.upsampling, surface, Child(path, "upsampling"));
                return At(path,
                          [&] { return std::make_shared<ShapeModelSource>(surface, upsampler); });
            },
        },
        spec);
}

/// Whether a source has posts that layers can estimate settings from: a raster, a chain whose
/// first source has, a shape model, or synthetic noise with a pixel size.
bool HasPosts(const SourceSpec& source) {
    return std::visit(Overloaded{
                          [](const RasterSourceSpec&) { return true; },
                          [](const ShapeModelSourceSpec&) { return true; },
                          [](const ChainSourceSpec& s) {
                              return !s.sources.empty() && HasPosts(s.sources.front());
                          },
                          [](const SyntheticSourceSpec& s) { return s.pixelSize.has_value(); },
                      },
                      source);
}

/// Rows of a whole-body list's grid above which its spacing is too fine.
constexpr double kMaxWholeBodyRows = 1e9;

/// Checks an extent at `path`, whose pairs its fields check, and returns its box (`ExtentBox`).
/// @throws ConfigError at `path` or below it for an incomplete, mixed or empty box.
Bounds2d CheckExtent(const ExtentSpec& extent, const std::string& path) {
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
    } else if (extent.center.empty() || extent.size.empty()) {
        throw ConfigError(Child(path, extent.center.empty() ? "center" : "size"),
                          "required with " +
                              std::string(extent.center.empty() ? "size" : "center"));
    }
    return *config::ExtentBox(extent);
}

/// @throws ConfigError at an area's extent in the other unit: meters on a body, degrees on a plane.
void CheckExtentUnit(const OutputAreaSpec& area, const std::string& path, bool body) {
    if (body ? area.extentM : area.extentDeg) {
        throw ConfigError(Child(path, body ? "extent_m" : "extent_deg"),
                          body ? "on a body give extent_deg, in degrees of longitude and latitude"
                               : "only on a body; give extent_m, in meters, on a plane");
    }
}

/// Checks the grid `cells` snaps to: at most `kMaxConfigGridSize` cells a side and, on a body,
/// within the latitudes and one turn of longitude.
/// @throws ConfigError at `sizePath` for a grid too large or small, or at `extentPath` off the
/// body.
void CheckAreaGrid(const config::AreaCells& cells, const std::string& sizePath,
                   const std::string& extentPath, bool body) {
    const double columns = SnappedCells(cells.box.maxX - cells.box.minX, cells.cell);
    const double rows = SnappedCells(cells.box.maxY - cells.box.minY, cells.cell);
    const auto limit = static_cast<double>(kMaxConfigGridSize);
    if (columns > limit || rows > limit) {
        throw ConfigError(sizePath, "gives " + FormatNumber(columns, 17) + "x" +
                                        FormatNumber(rows, 17) + " cells; each side must be " +
                                        config::CountText(kMinConfigGridSize, kMaxConfigGridSize));
    }
    const RasterGrid grid = RasterGrid::Covering(cells.box, cells.cell);
    CheckGridSize(grid.width, grid.height, sizePath);
    const Bounds2d bounds = grid.Bounds();
    const double slack = kGridSnapTolerance * cells.cell;
    if (body && !(bounds.minY >= -90.0 - slack && bounds.maxY <= 90.0 + slack &&
                  bounds.maxX - bounds.minX <= 360.0 + slack)) {
        throw ConfigError(extentPath,
                          "the grid must lie within latitudes -90 to 90 and one turn of longitude");
    }
}

/// Checks an output's area at `path`: its extent, pixel size and the grid they give.
/// @param listed Whether the output lists features, which may cover the whole body.
/// @throws ConfigError below `path` for an extent of the wrong unit or an invalid one, a missing
/// extent or pixel size without a bounded source, a grid of too many cells or off the body, or a
/// pixel size too fine to list the whole body.
void CheckOutputArea(const OutputAreaSpec& area, const std::string& path, bool listed,
                     const std::optional<RasterGrid>& source, const Surface& surface) {
    const bool body = surface.IsBody();
    CheckExtentUnit(area, path, body);
    const std::string extentPath = Child(path, body ? "extent_deg" : "extent_m");
    const std::string pixelPath = Child(path, "pixel_size_m");
    const std::optional<ExtentSpec>& extent = config::SurfaceExtent(area, surface);
    if (extent) {
        CheckExtent(*extent, extentPath);
    }
    if (listed && body && !source && !extent && area.pixelSize) {
        if (180.0 * surface.UnitLength() / *area.pixelSize > kMaxWholeBodyRows) {
            throw ConfigError(pixelPath, "is too fine to list features over the body");
        }
        return;
    }
    if (!extent && !source) {
        throw ConfigError(extentPath,
                          listed && body
                              ? "required without a raster or chain source, or give pixel_size_m "
                                "alone to list the whole body"
                              : "required without a raster or chain source: the area to cover");
    }
    if (!area.pixelSize && !source) {
        throw ConfigError(pixelPath, "required without a raster or chain source");
    }
    if (extent || area.pixelSize) {
        CheckAreaGrid(*config::CoveredCells(area, source, surface),
                      area.pixelSize ? pixelPath : extentPath, extentPath, body);
    }
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
        throw ConfigError(Child(path, "tile_size_px"),
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
    if (!output.siteDeg.empty() && !context.body) {
        throw ConfigError(Child(path, "site_deg"),
                          "only on a body, where the mesh's frame is centered on the site");
    }
}

void CheckOutput(const ReportOutput& /*output*/, const std::string& /*path*/, const OutputChecks&) {
}

/// Checks that outputs sharing one result agree on it: body meshes of one kind (mesh or ICQ) on
/// their cells and spacing, and terrain meshes over one grid on their cutouts, skirt and site.
/// @throws ConfigError at the first output that disagrees with an earlier one.
void CheckSharedResults(const ConfigSpec& config, const ConfigSummary& summary) {
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const std::string path = Element("outputs", i);
        for (std::size_t j = 0; j < i; ++j) {
            const auto* mesh = std::get_if<MeshOutput>(&config.outputs[i]);
            const auto* earlier = std::get_if<MeshOutput>(&config.outputs[j]);
            if (mesh && earlier && IsIcqPath(mesh->path) == IsIcqPath(earlier->path) &&
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
std::vector<Extent> CheckOutputs(const ConfigSpec& config, const Surface& surface,
                                 ConfigSummary& summary) {
    const OutputChecks checks{surface.IsBody(), config.albedo.has_value(),
                              config.materials.has_value()};
    std::vector<Extent> extents;
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const OutputSpec& output = config.outputs[i];
        const std::string path = Element("outputs", i);
        std::visit([&](const auto& o) { CheckOutput(o, path, checks); }, output);
        std::optional<RasterGrid> grid;
        if (const OutputAreaSpec* area = config::OutputArea(output)) {
            CheckOutputArea(*area, path, config::ListsFeatures(output), summary.source, surface);
            grid = config::OutputGrid(output, summary.source, surface);
        }
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

/// Checks and builds each layer in order over the output extents, and records in `summary` the
/// most craters the top-level crater layers are expected to generate.
/// @throws ConfigError under "layers" for an invalid layer.
void CheckLayers(const ConfigSpec& config, const std::vector<Extent>& extents,
                 const std::filesystem::path& directory, const Surface& surface,
                 config::GeneratorBuilder& builder, ConfigSummary& summary) {
    LayerChecks layers{extents, directory, surface};
    const std::optional<SourceSpec> source = EffectiveSource(config);
    layers.posts = source && HasPosts(*source);
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        CheckLayer(config.layers[i], Element("layers", i), layers);
    }
    for (std::size_t i = 0; i < config.layers.size(); ++i) {
        CheckBuiltLayer(config.layers[i], builder.AddLayer(i), Element("layers", i), layers);
    }
    if (!layers.expectedCraters.empty()) {
        summary.expectedCraters =
            *std::max_element(layers.expectedCraters.begin(), layers.expectedCraters.end());
    }
}

/// The material classes a terrain's surface can have: soil, bedrock where exposed, dust patches,
/// and what its layers lay.
std::vector<MaterialClass> PresentMaterials(const MaterialOptions& options,
                                            const TerrainGenerator& generator) {
    std::vector<MaterialClass> present{MaterialClass::Soil};
    if (options.exposure) {
        present.push_back(MaterialClass::Bedrock);
    }
    if (options.dustCover > 0.0) {
        present.push_back(MaterialClass::Dust);
    }
    ForEachLayer<MaterialSource>(generator, [&](const MaterialSource& source, std::size_t) {
        present.push_back(source.Material());
    });
    return present;
}

/// @throws ConfigError under "materials" for invalid options or photometry, or a photometry output
/// without parameters for a class the surface can have.
void CheckMaterials(const ConfigSpec& config, const TerrainGenerator& generator) {
    const MaterialsSpec& materials = *config.materials;
    At("materials", [&] { ValidateMaterialOptions(materials.options); });
    const MaterialPhotometry photometry = config::BuildPhotometry(materials.photometry);
    At("materials.photometry", [&] { ValidateMaterialPhotometry(photometry); });
    if (!config::HasOutput<PhotometryOutput>(config.outputs)) {
        return;
    }
    for (const MaterialClass material : PresentMaterials(materials.options, generator)) {
        const auto index = static_cast<std::size_t>(material);
        if (!photometry[index]) {
            throw ConfigError(Child("materials.photometry", config::kMaterialClassKeys[index]),
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
    CheckNoiseStack(spec.hueNoiseDegrees, Child(path, "hue_noise_deg"));
    CheckNoiseStack(spec.saturationNoise, Child(path, "saturation_noise"));
    CheckNoiseStack(spec.valueNoise, Child(path, "value_noise"));
    At(path, [&] { ValidateSyntheticAlbedoOptions(spec.Options()); });
}

void CheckAlbedoSource(const RasterAlbedoSpec& spec, const std::filesystem::path& directory,
                       const std::string& path) {
    const std::string file = Child(path, "path");
    const std::string bands = Child(path, "bands");
    CheckNonEmpty(spec.path, file);
    const GeoTiffInfo info =
        AtFile(file, [&] { return ReadGeoTiffInfo(Resolve(spec.path, directory)); });
    for (const std::size_t band : spec.bands) {
        CheckBand(band, info, bands);
    }
    if (info.transform.IsRotated()) {
        throw ConfigError(file, "rotated albedo maps are not supported");
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

namespace config {

void ValidateAlbedo(const ConfigSpec& config, const std::filesystem::path& directory) {
    const AlbedoSpec& albedo = *config.albedo;
    std::visit([&](const auto& source) { CheckAlbedoSource(source, directory, "albedo.source"); },
               albedo.source);
    const AlbedoChecks context{CountLayers<CraterLayerSpec>(config.layers),
                               CountLayers<BoulderLayerSpec>(config.layers)};
    for (std::size_t i = 0; i < albedo.layers.size(); ++i) {
        const std::string path = Element("albedo.layers", i);
        std::visit([&](const auto& layer) { CheckAlbedoLayer(layer, path, context); },
                   albedo.layers[i]);
    }
}

CheckedGenerator BuildCheckedGenerator(const ConfigSpec& config,
                                       const std::filesystem::path& directory,
                                       const std::shared_ptr<const Source>& external) {
    CheckConfigRanges(config);
    CheckBody(config.body, "body");
    CheckedGenerator checked;
    ConfigSummary& summary = checked.summary;
    const Surface surface = BuildSurface(config.body, directory);
    std::shared_ptr<const Source> source = external;
    if (!external) {
        source = BuildSource(EffectiveSource(config).value_or(SourceSpec{SyntheticSourceSpec{}}),
                             directory, "source", surface);
        summary.source = BoundedGrid(*source);
    }
    const std::vector<Extent> extents = CheckOutputs(config, surface, summary);
    GeneratorBuilder builder(config, directory, surface, std::move(source), external != nullptr);
    CheckLayers(config, extents, directory, surface, builder, summary);
    checked.generator = builder.Take();
    if (config.albedo) {
        ValidateAlbedo(config, directory);
    }
    if (config.materials) {
        CheckMaterials(config, checked.generator);
    }
    return checked;
}

}  // namespace config

ConfigSummary ValidateConfig(const TerrainGenConfig& value, const std::filesystem::path& given) {
    const ConfigSpec& config = value.Spec();
    return config::BuildCheckedGenerator(config, ConfigDirectory(config, given)).summary;
}

}  // namespace terraingen
