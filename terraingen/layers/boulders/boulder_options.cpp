#include "terraingen/layers/boulders/boulder_options.h"

#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/summed.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Whether `distribution` may be evaluated from `low` to `high` metres.
bool Covers(const SizeFrequencyDistribution& distribution, double low, double high) {
    return distribution.AllowsExtrapolation() ||
           (distribution.ValidRange().min <= low && distribution.ValidRange().max >= high);
}

void CheckDistribution(const SizeFrequencyDistribution& distribution, const BoulderSizes& sizes,
                       const char* what) {
    if (distribution.Kind() != SfdKind::Density) {
        throw std::invalid_argument(std::string(what) + " must be a density, not a rate");
    }
    if (!Covers(distribution, sizes.minDiameter, sizes.maxDiameter)) {
        throw std::invalid_argument(std::string(what) +
                                    " must be valid from minDiameter to maxDiameter, or allow "
                                    "extrapolation");
    }
}

/// Checks the settings that only matter when craters throw blocks.
void CheckThrownBlocks(const BoulderEjecta& ejecta, const BoulderSizes& sizes) {
    if (ejecta.distribution) {
        CheckDistribution(*ejecta.distribution, sizes, "The boulder ejecta distribution");
    } else if (!(ejecta.coverage > 0.0 && ejecta.coverage < 1.0)) {
        throw std::invalid_argument("Boulder ejecta coverage must be between 0 and 1");
    }
    if (!(std::isfinite(ejecta.reach) && ejecta.reach > 1.0)) {
        throw std::invalid_argument("Boulder ejecta need a reach over 1 radius");
    }
    if (!(IsPositive(ejecta.decayRadii) && IsInUnitInterval(ejecta.floorShare))) {
        throw std::invalid_argument(
            "Boulder ejecta need a positive decayRadii and a floorShare in [0, 1]");
    }
    if (!(ejecta.minFreshness >= 0.0 && ejecta.minFreshness < 1.0 &&
          IsPositive(ejecta.lifetimeGyr) && IsNonNegative(ejecta.agingExponent))) {
        throw std::invalid_argument("Boulder ejecta need minFreshness in [0, 1), a positive "
                                    "lifetimeGyr and a non-negative agingExponent");
    }
    if (ejecta.gravity && !IsPositive(*ejecta.gravity)) {
        throw std::invalid_argument("Boulder ejecta gravity must be positive");
    }
}

}  // namespace

bool BoulderModelThreshold::IsModel(double diameter, double spacing) const {
    return (minDiameter && diameter >= *minDiameter) ||
           (minSamples && spacing > 0.0 && diameter >= *minSamples * spacing);
}

double BoulderEjecta::MinCraterDiameter() const {
    const double excavated = regolithThickness ? *regolithThickness / excavationDepthRatio : 0.0;
    return std::fmax(minCraterDiameter, excavated);
}

double BoulderThrowDistance(double craterDiameter, double regolithThickness, double gravity) {
    for (const double value : {craterDiameter, regolithThickness, gravity}) {
        RequirePositive(value, "Crater diameter, regolith thickness and gravity");
    }
    return 138.5 / gravity * std::pow(craterDiameter / regolithThickness, 0.7086);
}

void ValidateBoulderSizes(const BoulderSizes& sizes) {
    if (!(IsPositive(sizes.minDiameter) && std::isfinite(sizes.maxDiameter) &&
          sizes.maxDiameter > sizes.minDiameter)) {
        throw std::invalid_argument("Boulder diameters must satisfy 0 < minDiameter < maxDiameter");
    }
    if (sizes.distribution) {
        CheckDistribution(*sizes.distribution, sizes, "The boulder distribution");
    } else if (!(sizes.coverage > 0.0 && sizes.coverage < 1.0)) {
        throw std::invalid_argument("Boulder coverage must be between 0 and 1");
    }
}

void ValidateBoulderPatches(const BoulderPatches& patches) {
    if (!(std::isfinite(patches.spread) && patches.spread >= 1.0 && IsPositive(patches.size))) {
        throw std::invalid_argument("Boulder patches need spread >= 1 and a positive size");
    }
}

void ValidateBoulderEjecta(const BoulderEjecta& ejecta, const BoulderSizes& sizes) {
    if (!IsNonNegative(ejecta.minCraterDiameter)) {
        throw std::invalid_argument("Boulder ejecta minCraterDiameter must not be negative");
    }
    if (ejecta.regolithThickness && !IsPositive(*ejecta.regolithThickness)) {
        throw std::invalid_argument("Boulder ejecta regolithThickness must be positive");
    }
    if (!(ejecta.excavationDepthRatio > 0.0 && ejecta.excavationDepthRatio <= 1.0)) {
        throw std::invalid_argument("Boulder ejecta excavationDepthRatio must be in (0, 1]");
    }
    if (ejecta.MinCraterDiameter() > 0.0) {
        CheckThrownBlocks(ejecta, sizes);
    }
}

void ValidateBoulderBedrock(const BoulderBedrock& bedrock) {
    RequirePositive(bedrock.abundance, "Boulder bedrockAbundance");
    if (bedrock.exposure) {
        ValidateExposureParams(*bedrock.exposure);
    }
    if (!IsPositive(bedrock.exposureSpacing)) {
        throw std::invalid_argument("Boulder exposureSpacing must be positive");
    }
}

void ValidateBoulderPose(const BoulderPose& pose) {
    if (!(pose.minHeightRatio > 0.0 && pose.maxHeightRatio >= pose.minHeightRatio &&
          std::isfinite(pose.maxHeightRatio))) {
        throw std::invalid_argument("Boulder height ratios must satisfy 0 < min <= max");
    }
    if (!(pose.minBurial >= 0.0 && pose.maxBurial >= pose.minBurial && pose.maxBurial < 1.0)) {
        throw std::invalid_argument("Boulder burial must satisfy 0 <= min <= max < 1");
    }
    if (!IsNonNegative(pose.maxLean)) {
        throw std::invalid_argument("Boulder maxLean must not be negative");
    }
}

void ValidateBoulderRendering(const BoulderRendering& rendering) {
    for (const auto& threshold : {rendering.models.minDiameter, rendering.models.minSamples}) {
        if (threshold) {
            RequirePositive(*threshold, "Boulder model thresholds");
        }
    }
    ValidateBoulderShapeOptions(rendering.shapes);
}

void ValidateBoulderLayerOptions(const BoulderLayerOptions& options) {
    ValidateBoulderSizes(options.sizes);
    ValidateBoulderPatches(options.patches);
    ValidateBoulderBedrock(options.bedrock);
    if (!options.sizes.distribution &&
        options.sizes.coverage * MaxBackgroundAbundance(options) >= 1.0) {
        throw std::invalid_argument(
            "Boulder coverage times the patch spread and bedrock abundance must stay below 1");
    }
    ValidateBoulderEjecta(options.ejecta, options.sizes);
    ValidateBoulderPose(options.pose);
    ValidateResolutionFade(options.fade);
    ValidateBoulderRendering(options.rendering);
}

double MaxBackgroundAbundance(const BoulderLayerOptions& options) {
    return options.patches.spread *
           (options.bedrock.exposure ? std::fmax(1.0, options.bedrock.abundance) : 1.0);
}

std::shared_ptr<const SizeFrequencyDistribution> EjectaSizes(const BoulderLayerOptions& options) {
    const BoulderEjecta& ejecta = options.ejecta;
    if (ejecta.distribution) {
        return ejecta.distribution;
    }
    return std::make_shared<GolombekRappSfd>(ejecta.coverage, options.sizes.qConstant,
                                             options.sizes.qCoverageCoefficient);
}

std::shared_ptr<const SizeFrequencyDistribution>
BoulderCandidates(const BoulderLayerOptions& options) {
    ValidateBoulderLayerOptions(options);
    const BoulderSizes& sizes = options.sizes;
    const double richest = MaxBackgroundAbundance(options);
    std::vector<WeightedSfd> terms;
    if (sizes.distribution) {
        terms.push_back({richest, sizes.distribution});
    } else {
        terms.push_back(
            {1.0, std::make_shared<GolombekRappSfd>(sizes.coverage * richest, sizes.qConstant,
                                                    sizes.qCoverageCoefficient)});
    }
    if (options.ejecta.MinCraterDiameter() > 0.0) {
        terms.push_back({1.0, EjectaSizes(options)});
    }
    return std::make_shared<SummedSfd>(std::move(terms));
}

}  // namespace terraingen
