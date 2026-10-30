// SPDX-License-Identifier: BSD-3-Clause

#include "config/boulders.h"

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/tabulated.h"
#include "terraingen/layers/boulders/boulder_abundance.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "util/overloaded.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace terraingen::config {

namespace {

/// The distribution a boulder size spec names, or null for the layer's Golombek-Rapp boulders.
std::shared_ptr<SizeFrequencyDistribution> BuildBoulderSizes(const BoulderSizesSpec& spec) {
    using Result = std::shared_ptr<SizeFrequencyDistribution>;
    const auto density = [](const DensitySpec& named) {
        return std::const_pointer_cast<SizeFrequencyDistribution>(BuildDensity(named));
    };
    return std::visit(
        Overloaded{
            [](const GolombekRappBouldersSpec&) -> Result { return nullptr; },
            [](const Sp8023IntercraterBouldersSpec& s) -> Result {
                return std::make_shared<TabulatedSfd>(Sp8023IntercraterBlocks(s.curve));
            },
            [](const Sp8023CraterBouldersSpec& s) -> Result {
                return std::make_shared<TabulatedSfd>(Sp8023CraterBlocks(s.curve));
            },
            [](const SynthterrainBouldersSpec& s) -> Result {
                return std::make_shared<PowerLawSfd>(SynthterrainBoulderDistribution(s.model));
            },
            [&](const PowerLawSpec& s) -> Result { return density(s); },
            [&](const PiecewisePowerLawSpec& s) -> Result { return density(s); },
            [&](const TabulatedSpec& s) -> Result { return density(s); },
            [&](const PiecewiseUniformSpec& s) -> Result { return density(s); },
        },
        spec);
}

bool IsGolombekRapp(const BoulderSizesSpec& sizes) {
    return std::holds_alternative<GolombekRappBouldersSpec>(sizes);
}

/// @throws ConfigError at a Golombek-Rapp term that is set, such as "coverage", when no size model
/// of the layer is Golombek-Rapp and so none would use it.
void CheckGolombekRappTerms(const BoulderLayerSpec& spec, const std::string& path) {
    const BoulderSizes& sizes = spec.boulders.sizes;
    const bool layer = IsGolombekRapp(spec.sizes);
    const bool ejecta = IsGolombekRapp(spec.ejectaSizes.value_or(spec.sizes));
    const auto check = [](bool used, const std::optional<double>& value, const std::string& at,
                          const char* fix) {
        if (!used && value) {
            throw ConfigError(at, std::string("is only used by Golombek-Rapp sizes; set ") + fix +
                                      " or leave it out");
        }
    };
    const char* const layerFix = R"("sizes": {"type": "golombek_rapp"})";
    check(layer, sizes.coverage, Child(path, "coverage"), layerFix);
    check(layer || ejecta, sizes.qConstant, Child(path, "qa"), layerFix);
    check(layer || ejecta, sizes.qCoverageCoefficient, Child(path, "qb"), layerFix);
    check(ejecta, spec.boulders.ejecta.coverage, Child(Child(path, "ejecta"), "coverage"),
          R"("ejecta_sizes": {"type": "golombek_rapp"})");
}

/// @throws ConfigError at the Golombek-Rapp terms, a missing coverage, or ejecta without a crater
/// layer before; the layer checks the rest of its options as it is built.
void CheckBoulderOptions(const BoulderLayerSpec& spec, const BoulderLayerOptions& options,
                         const std::string& path, bool cratersBefore) {
    if (!options.generateRandom) {
        return;
    }
    CheckGolombekRappTerms(spec, path);
    if (IsGolombekRapp(spec.sizes) && !(options.sizes.Coverage() > 0.0)) {
        throw ConfigError(Child(path, "coverage"),
                          "must be above 0 to generate random boulders; set generate_random to "
                          "false to place only the manual ones");
    }
    if (options.ejecta.ThrowingDiameter() && !cratersBefore) {
        throw ConfigError(Child(Child(path, "ejecta"), "min_crater_diameter_m"),
                          "ejecta need a crater layer before this one");
    }
}

}  // namespace

BoulderLayerOptions BuildBoulderOptions(const BoulderLayerSpec& spec, const std::string& path,
                                        const std::filesystem::path& directory) {
    BoulderLayerOptions options = spec.boulders;
    if (options.sizes.distribution != DefaultBoulderSizes() ||
        options.ejecta.distribution != DefaultBoulderSizes()) {
        throw ConfigError(Child(path, "sizes"),
                          "gives the boulder sizes; leave boulders.sizes.distribution and "
                          "boulders.ejecta.distribution at their defaults and set sizes and "
                          "ejecta_sizes instead");
    }
    const auto sizes = [&](const BoulderSizesSpec& named, const std::string& key) {
        auto built = At(Child(path, key), [&] { return BuildBoulderSizes(named); });
        if (built && spec.extrapolate) {
            built->SetAllowExtrapolation(true);
        }
        return built;
    };
    options.sizes.distribution = sizes(spec.sizes, "sizes");
    options.ejecta.distribution = sizes(spec.ejectaSizes.value_or(spec.sizes), "ejecta_sizes");
    for (std::size_t i = 0; i < spec.shapeFiles.size(); ++i) {
        options.rendering.shapes.library.push_back(
            AtFile(Element(Child(path, "shape_files"), i), [&] {
                return std::make_shared<const SceneMesh>(
                    ReadMesh(Resolve(spec.shapeFiles[i], directory)));
            }));
    }
    if (spec.densityMap) {
        options.densityMap =
            BuildDensityMap(*spec.densityMap, Child(path, "density_map"), directory);
    }
    if (spec.slopeMap) {
        options.slopeMap = BuildSizeSlopeMap(*spec.slopeMap, Child(path, "slope_map"), directory);
    }
    options.manual = spec.manual;
    if (spec.completeness) {
        options.completeness =
            BuildField(*spec.completeness, Child(path, "completeness"), directory);
    }
    return options;
}

void CheckSpec(const BoulderLayerSpec& spec, const std::string& path, LayerChecks& checks) {
    const std::string manualPath = Child(path, "manual");
    std::unordered_map<std::uint64_t, std::size_t> ids;
    for (std::size_t n = 0; n < spec.manual.size(); ++n) {
        At(Element(manualPath, n), [&] { ValidateManualBoulder(spec.manual[n]); });
        const auto added = ids.emplace(ManualBoulderId(spec.manual[n]), n);
        if (!added.second) {
            throw ConfigError(Element(manualPath, n),
                              "shares its ID with " + Element(manualPath, added.first->second));
        }
    }
    checks.boulders = true;
}

LayerPtr BuildSpec(const BoulderLayerSpec& spec, const std::string& path,
                   const LayerInputs& inputs) {
    BoulderLayerOptions options = BuildBoulderOptions(spec, path, inputs.directory);
    CheckBoulderOptions(spec, options, path, !inputs.craters.empty());
    const bool throws = options.generateRandom && options.ejecta.ThrowingDiameter().has_value();
    return At(path, [&] {
        return std::make_shared<BoulderLayer>(
            std::move(options),
            throws ? inputs.craters : std::vector<std::shared_ptr<const CraterLayer>>{},
            inputs.base);
    });
}

void CheckBuilt(const BoulderLayerSpec& /*spec*/, const Layer& built, const std::string& path,
                LayerChecks& checks) {
    const BoulderLayerOptions& boulders = dynamic_cast<const BoulderLayer&>(built).Options();
    if (!boulders.generateRandom) {
        return;
    }
    const BoulderSizes& sizes = boulders.sizes;
    for (const OutputExtent& extent : checks.extents) {
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

}  // namespace terraingen::config
