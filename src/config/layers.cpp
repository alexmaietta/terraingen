// SPDX-License-Identifier: BSD-3-Clause

#include "config/layers.h"

#include "terraingen/layers/noise_layer.h"
#include "terraingen/layers/sand_fill.h"
#include "terraingen/layers/sand_shadow.h"
#include "terraingen/layers/spectral_continuation.h"
#include "terraingen/layers/transforms.h"
#include "util/overloaded.h"

#include <type_traits>
#include <utility>

namespace terraingen {

std::vector<Fractal> NoiseLayerSpec::Fractals() const {
    return StackFractals(stack);
}

std::vector<Fractal> NoiseLayerSpec::EstimatedFractals(const BaseRoughness& source) const {
    std::vector<Fractal> fractals;
    for (FractalSpec layer : stack) {
        if (!layer.hurst) {
            layer.hurst = static_cast<float>(source.hurst);
        }
        Fractal effective = layer.EffectiveRelief();
        if (!layer.amplitude && effective.enabled) {
            BaseRoughness continued = source;
            continued.hurst = static_cast<double>(*layer.hurst);
            effective.amplitude = MatchedNoiseAmplitude(effective, continued);
        }
        fractals.push_back(effective);
    }
    return fractals;
}

RoughnessOptions RoughnessLayerSpec::Options() const {
    RoughnessOptions options = roughness;
    options.rmsSlope = rmsSlope.value_or(roughness.rmsSlope);
    options.hurst = hurst.value_or(roughness.hurst);
    return options;
}

RoughnessOptions RoughnessLayerSpec::EstimatedOptions(BaseRoughness source) const {
    RoughnessOptions options = Options();
    source.hurst = hurst.value_or(source.hurst);
    options.hurst = source.hurst;
    options.rmsSlope = rmsSlope.value_or(source.RmsSlope(options.baseline));
    return options;
}

SpectralContinuationOptions SpectralContinuationLayerSpec::Options() const {
    SpectralContinuationOptions options;
    options.pixelSize = pixelSize.value_or(options.pixelSize);
    options.slope = slope.value_or(options.slope);
    options.octaveRms = octaveRms.value_or(options.octaveRms);
    options.minWavelength = minWavelength;
    options.taperOctaves = taperOctaves;
    options.seed = seed;
    options.fade = fade;
    return options;
}

std::string LayerType(const LayerSpec& layer) {
    return std::visit(
        [](const auto& spec) {
            using Spec = std::decay_t<decltype(spec)>;
            std::string type(SpecName<Spec>::kName);
            if constexpr (std::is_same_v<Spec, MaskedLayerSpec>) {
                type += " (" + LayerType(*spec.layer) + ")";
            }
            return type;
        },
        layer);
}

namespace config {

/// @name Layer dispatch
/// Checks, builds or checks the built layer of any layer spec, through the overloads of its kind.
/// @{
void CheckLayer(const LayerSpec& spec, const std::string& path, LayerChecks& checks);
LayerPtr BuildLayer(const LayerSpec& spec, const std::string& path, const LayerInputs& inputs);
void CheckBuiltLayer(const LayerSpec& spec, const Layer& built, const std::string& path,
                     LayerChecks& checks);
/// @}

namespace {

/// The roughness of the source at its native resolution (`EstimateSourceRoughness`).
/// @throws std::invalid_argument without a source, or as `EstimateSourceRoughness`.
BaseRoughness SourceRoughness(const LayerInputs& inputs) {
    if (!inputs.source) {
        throw std::invalid_argument("Estimating from the source needs a source");
    }
    return EstimateSourceRoughness(*inputs.source, inputs.base.surface);
}

void CheckSpec(const NoiseLayerSpec& spec, const std::string& path, LayerChecks& checks) {
    CheckNoiseStack(spec.stack, Child(path, "stack"));
    CheckEstimateSource(spec.estimateFromSource, path, checks);
}

void CheckSpec(const RoughnessLayerSpec& spec, const std::string& path, LayerChecks& checks) {
    At(path, [&] { ValidateRoughnessOptions(spec.Options()); });
    CheckEstimateSource(spec.estimateFromSource, path, checks);
}

void CheckSpec(const SpectralContinuationLayerSpec& spec, const std::string& path,
               LayerChecks& checks) {
    if (!checks.posts && !(spec.pixelSize && spec.slope && spec.octaveRms)) {
        throw ConfigError(path, "without a source with posts to fit its spectrum to needs "
                                "pixel_size_m, slope and octave_rms_m");
    }
    At(path, [&] {
        ValidateSpectrumFitBand(spec.fit);
        ValidateSpectralContinuationOptions(spec.Options());
    });
}

void CheckSpec(const SandShadowOptions& /*spec*/, const std::string& path, LayerChecks& checks) {
    if (!checks.boulders) {
        throw ConfigError(path, "sand shadows need a boulder layer before this one");
    }
}

void CheckSpec(const MaskedLayerSpec& spec, const std::string& path, LayerChecks& checks) {
    At(Child(path, "shape"), [&] { ValidateRegionShape(BuildRegionShape(spec.shape)); });
    const bool empty = std::visit(
        Overloaded{
            [](const RectangleRegionSpec& r) { return !(r.maxX > r.minX && r.maxY > r.minY); },
            [](const CircleRegionSpec& c) { return !(c.radius > 0.0); },
            [](const PolygonRegionSpec&) { return false; },
        },
        spec.shape);
    if (empty) {
        throw ConfigError(Child(path, "shape"),
                          "required: the shape has no area; give the rectangle's bounds or the "
                          "circle's radius");
    }
    LayerChecks inner = checks;
    CheckLayer(*spec.layer, Child(path, "layer"), inner);
}

void CheckBuilt(const MaskedLayerSpec& spec, const Layer& built, const std::string& path,
                LayerChecks& checks) {
    LayerChecks inner = checks;
    CheckBuiltLayer(*spec.layer, dynamic_cast<const MaskedLayer&>(built).Inner(),
                    Child(path, "layer"), inner);
}

LayerPtr BuildSpec(const NoiseLayerSpec& spec, const std::string& path, const LayerInputs& inputs) {
    const std::vector<Fractal> fractals = At(path, [&] {
        return spec.estimateFromSource ? spec.EstimatedFractals(SourceRoughness(inputs))
                                       : spec.Fractals();
    });
    return At(path, [&] {
        return std::make_shared<NoiseLayer>(NoiseLayerOptions{fractals, spec.sourceResolvesPixels});
    });
}

LayerPtr BuildSpec(const RoughnessLayerSpec& spec, const std::string& path,
                   const LayerInputs& inputs) {
    const RoughnessOptions options = At(path, [&] {
        return spec.estimateFromSource ? spec.EstimatedOptions(SourceRoughness(inputs))
                                       : spec.Options();
    });
    return std::make_shared<RoughnessLayer>(options);
}

/// A spectral continuation layer, fitted to the source wherever the spec gives no slope or
/// amplitude; a valid configuration has a source whenever the spec leaves one out.
LayerPtr BuildSpec(const SpectralContinuationLayerSpec& spec, const std::string& path,
                   const LayerInputs& inputs) {
    const Surface& surface = inputs.base.surface;
    SpectralContinuationOptions options = spec.Options();
    const RasterGrid grid = inputs.source ? inputs.source->Grid() : RasterGrid{};
    options.pixelSize = spec.pixelSize.value_or(grid.PixelSize() * surface.UnitLength());
    if (!(spec.slope && spec.octaveRms)) {
        const PowerLawSpectrum fit = At(path, [&] {
            const Raster<float> heights =
                SampleSourceWindow(*inputs.source, options.pixelSize / surface.UnitLength(),
                                   grid.transform.PixelToWorld(0.5, 0.5));
            return EstimatePowerLawSpectrum(heights, spec.fit);
        });
        options.slope = spec.slope.value_or(fit.slope);
        options.octaveRms = spec.octaveRms.value_or(fit.octaveRms);
    }
    return std::make_shared<SpectralContinuationLayer>(options);
}

LayerPtr BuildSpec(const MaskedLayerSpec& spec, const std::string& path,
                   const LayerInputs& inputs) {
    return std::make_shared<MaskedLayer>(BuildLayer(*spec.layer, Child(path, "layer"), inputs),
                                         BuildRegionShape(spec.shape), spec.feather, spec.blend);
}

LayerPtr BuildSpec(const DuneOptions& spec, const std::string& path, const LayerInputs&) {
    return At(path, [&] { return std::make_shared<DuneLayer>(spec); });
}

LayerPtr BuildSpec(const BedformOptions& spec, const std::string& path, const LayerInputs&) {
    return At(path, [&] { return std::make_shared<BedformLayer>(spec); });
}

LayerPtr BuildSpec(const PavementOptions& spec, const std::string& path, const LayerInputs&) {
    return At(path, [&] { return std::make_shared<PavementLayer>(spec); });
}

LayerPtr BuildSpec(const StrataOptions& spec, const std::string& path, const LayerInputs&) {
    return At(path, [&] { return std::make_shared<StrataLayer>(spec); });
}

LayerPtr BuildSpec(const SandShadowOptions& spec, const std::string& path,
                   const LayerInputs& inputs) {
    return At(path, [&] { return std::make_shared<SandShadowLayer>(inputs.boulders, spec); });
}

LayerPtr BuildSpec(const SandFillOptions& spec, const std::string& path, const LayerInputs&) {
    return At(path, [&] { return std::make_shared<SandFillLayer>(spec); });
}

LayerPtr BuildSpec(const ScaleLayerSpec& spec, const std::string& path, const LayerInputs&) {
    return At(path, [&] { return std::make_shared<ScaleLayer>(spec.scale, spec.offset); });
}

LayerPtr BuildSpec(const ClampLayerSpec& spec, const std::string& path, const LayerInputs&) {
    return At(path, [&] { return std::make_shared<ClampLayer>(spec.min, spec.max); });
}

/// The secondaries the secondary layers after the crater layer `layers[index]` throw from it, up
/// to the next crater layer; none when `layers[index]` is not a crater layer.
std::vector<SecondaryCraterOptions> ThrownSecondaries(const std::vector<LayerSpec>& layers,
                                                      std::size_t index) {
    std::vector<SecondaryCraterOptions> thrown;
    if (!std::holds_alternative<CraterLayerSpec>(layers[index])) {
        return thrown;
    }
    for (std::size_t j = index + 1;
         j < layers.size() && !std::holds_alternative<CraterLayerSpec>(layers[j]); ++j) {
        if (const auto* secondaries = std::get_if<SecondaryLayerSpec>(&layers[j])) {
            thrown.push_back(secondaries->secondaries);
        }
    }
    return thrown;
}

}  // namespace

void CheckLayer(const LayerSpec& spec, const std::string& path, LayerChecks& checks) {
    std::visit([&](const auto& layer) { CheckSpec(layer, path, checks); }, spec);
}

LayerPtr BuildLayer(const LayerSpec& spec, const std::string& path, const LayerInputs& inputs) {
    return std::visit([&](const auto& layer) { return BuildSpec(layer, path, inputs); }, spec);
}

void CheckBuiltLayer(const LayerSpec& spec, const Layer& built, const std::string& path,
                     LayerChecks& checks) {
    std::visit([&](const auto& layer) { CheckBuilt(layer, built, path, checks); }, spec);
}

void CheckEstimateSource(bool estimate, const std::string& path, const LayerChecks& checks) {
    if (estimate && !checks.posts) {
        throw ConfigError(Child(path, "estimate_from_source"),
                          "needs a source with posts to estimate from, a raster or synthetic noise "
                          "with a pixel size; without one give the values instead");
    }
}

RegionShape BuildRegionShape(const RegionShapeSpec& spec) {
    return std::visit(
        Overloaded{
            [](const RectangleRegionSpec& s) {
                return RegionShape::Rectangle({s.minX, s.minY, s.maxX, s.maxY});
            },
            [](const CircleRegionSpec& s) { return RegionShape::Circle({s.x, s.y}, s.radius); },
            [](const PolygonRegionSpec& s) {
                const auto vertices = [](const std::vector<std::array<double, 2>>& pairs) {
                    std::vector<Vec2d> ring;
                    ring.reserve(pairs.size());
                    for (const std::array<double, 2>& p : pairs) {
                        ring.push_back({p[0], p[1]});
                    }
                    return ring;
                };
                std::vector<std::vector<Vec2d>> holes;
                holes.reserve(s.holes.size());
                for (const auto& hole : s.holes) {
                    holes.push_back(vertices(hole));
                }
                return RegionShape::Polygon(vertices(s.points), std::move(holes));
            },
        },
        spec);
}

BuiltLayers BuildLayers(const std::vector<LayerSpec>& layers,
                        const std::vector<OutputExtent>& extents, const LayerSources& sources,
                        const std::filesystem::path& directory, const Surface& surface) {
    LayerChecks checks{extents};
    checks.posts = sources.posts;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        CheckLayer(layers[i], Element("layers", i), checks);
    }
    BuiltLayers built;
    std::vector<std::shared_ptr<const CraterLayer>> craters;
    std::shared_ptr<const BoulderLayer> boulders;
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const std::string path = Element("layers", i);
        const LayerInputs inputs{TerrainBase{surface, sources.measured},
                                 directory,
                                 craters,
                                 boulders,
                                 sources.given,
                                 ThrownSecondaries(layers, i)};
        const LayerPtr layer = BuildLayer(layers[i], path, inputs);
        built.layers.push_back(layer);
        CheckBuiltLayer(layers[i], *layer, path, checks);
        if (std::holds_alternative<CraterLayerSpec>(layers[i])) {
            craters.push_back(std::static_pointer_cast<const CraterLayer>(layer));
        }
        if (std::holds_alternative<BoulderLayerSpec>(layers[i])) {
            boulders = std::static_pointer_cast<const BoulderLayer>(layer);
        }
    }
    if (!checks.expectedCraters.empty()) {
        built.expectedCraters =
            *std::max_element(checks.expectedCraters.begin(), checks.expectedCraters.end());
    }
    return built;
}

}  // namespace config

}  // namespace terraingen
