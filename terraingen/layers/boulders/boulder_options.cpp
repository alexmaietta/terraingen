// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_options.h"

#include "terraingen/distribution/sp8023.h"
#include "terraingen/layers/boulders/boulder_abundance.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace terraingen {

namespace {

/// Whether `distribution` may be evaluated from `low` to `high` meters.
bool Covers(const SizeFrequencyDistribution& distribution, double low, double high) {
    return distribution.AllowsExtrapolation() ||
           (distribution.ValidRange().min <= low && distribution.ValidRange().max >= high);
}

/// Throws unless `distribution` is a density valid over the boulder sizes.
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
    } else if (!(ejecta.Coverage() > 0.0 && ejecta.Coverage() < 1.0)) {
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
    if (ejecta.gravity) {
        RequirePositive(*ejecta.gravity, "Boulder ejecta gravity");
    }
}

/// Checks boulder sizes: 0 < min < max diameter, and a density distribution covering them or a
/// coverage in (0, 1).
void ValidateBoulderSizes(const BoulderSizes& sizes) {
    if (!(IsPositive(sizes.minDiameter) && std::isfinite(sizes.maxDiameter) &&
          sizes.maxDiameter > sizes.minDiameter)) {
        throw std::invalid_argument("Boulder diameters must satisfy 0 < minDiameter < maxDiameter");
    }
    if (sizes.distribution) {
        CheckDistribution(*sizes.distribution, sizes, "The boulder distribution");
    } else if (!(sizes.Coverage() > 0.0 && sizes.Coverage() < 1.0)) {
        throw std::invalid_argument("Boulder coverage must be between 0 and 1");
    }
}

/// Rejects Golombek-Rapp terms that a size distribution would leave unused.
void CheckGolombekRappTerms(const BoulderLayerOptions& options) {
    const BoulderSizes& sizes = options.sizes;
    const BoulderEjecta& ejecta = options.ejecta;
    if (sizes.distribution && sizes.coverage) {
        throw std::invalid_argument(
            "Boulder coverage is only used by Golombek-Rapp sizes; set sizes.distribution to null "
            "for them or leave the coverage unset");
    }
    if (ejecta.distribution && ejecta.coverage) {
        throw std::invalid_argument(
            "Boulder ejecta coverage is only used by Golombek-Rapp blocks; set ejecta.distribution "
            "to null for them or leave the coverage unset");
    }
    if (sizes.distribution && ejecta.distribution &&
        (sizes.qConstant || sizes.qCoverageCoefficient)) {
        throw std::invalid_argument(
            "The Golombek-Rapp qConstant and qCoverageCoefficient are only used by Golombek-Rapp "
            "sizes or blocks; leave them unset with size distributions");
    }
}

/// Checks crater blocks, whose distribution must cover `sizes`.
void ValidateBoulderEjecta(const BoulderEjecta& ejecta, const BoulderSizes& sizes) {
    if (ejecta.minCraterDiameter) {
        RequireNonNegative(*ejecta.minCraterDiameter, "Boulder ejecta minCraterDiameter");
    }
    if (ejecta.regolithThickness) {
        RequirePositive(*ejecta.regolithThickness, "Boulder ejecta regolithThickness");
    }
    if (!(ejecta.excavationDepthRatio > 0.0 && ejecta.excavationDepthRatio <= 1.0)) {
        throw std::invalid_argument("Boulder ejecta excavationDepthRatio must be in (0, 1]");
    }
    if (ejecta.ThrowingDiameter()) {
        CheckThrownBlocks(ejecta, sizes);
    }
}

/// Checks bedrock boulders.
void ValidateBoulderBedrock(const BoulderBedrock& bedrock) {
    RequirePositive(bedrock.abundance, "Boulder bedrock abundance");
    if (bedrock.exposure) {
        ValidateExposureParams(*bedrock.exposure);
    }
    RequirePositive(bedrock.meanSize, "Boulder bedrock meanSize");
}

/// Checks boulder poses.
void ValidateBoulderPose(const BoulderPose& pose) {
    if (!(pose.minHeightRatio > 0.0 && pose.maxHeightRatio >= pose.minHeightRatio &&
          std::isfinite(pose.maxHeightRatio))) {
        throw std::invalid_argument("Boulder height ratios must satisfy 0 < min <= max");
    }
    if (!(pose.minBurial >= 0.0 && pose.maxBurial >= pose.minBurial && pose.maxBurial < 1.0)) {
        throw std::invalid_argument("Boulder burial must satisfy 0 <= min <= max < 1");
    }
    RequireNonNegative(pose.maxLean, "Boulder maxLean");
    RequireUnitInterval(pose.burialByWear, "Boulder burialByWear");
    if (!(pose.sideShare >= 0.0 && pose.endShare >= 0.0 && pose.sideShare + pose.endShare <= 1.0)) {
        throw std::invalid_argument(
            "Boulder sideShare and endShare must not be negative and must sum to at most 1");
    }
    if (!(pose.maxTilt >= 0.0 && pose.maxTilt <= kPi)) {
        throw std::invalid_argument("Boulder maxTilt must be in [0, pi]");
    }
    if (pose.maxSlope && !(*pose.maxSlope > 0.0 && *pose.maxSlope < 0.5 * kPi)) {
        throw std::invalid_argument("Boulder maxSlope must be in (0, pi / 2)");
    }
    if (pose.rollDistance &&
        !(pose.maxSlope && *pose.rollDistance > 0.0 && std::isfinite(*pose.rollDistance))) {
        throw std::invalid_argument("Boulder rollDistance must be positive and finite, with a "
                                    "maxSlope");
    }
}

/// Checks how boulders are drawn.
void ValidateBoulderRendering(const BoulderRendering& rendering) {
    const BoulderModelThreshold& models = rendering.models;
    for (const auto& threshold :
         {models.minDiameter, models.minSamples, models.maxDiameter, models.maxSamples}) {
        if (threshold) {
            RequirePositive(*threshold, "Boulder model thresholds");
        }
    }
    const BoulderReliefRange& range = rendering.reliefRange;
    RequireNonNegative(range.minDiameter, "Boulder relief minDiameter");
    if (range.maxDiameter && !(*range.maxDiameter > range.minDiameter)) {
        throw std::invalid_argument("Boulder relief maxDiameter must exceed its minDiameter");
    }
    ValidateBoulderShapeOptions(rendering.shapes);
}

}  // namespace

bool BoulderModelThreshold::IsModel(double diameter, double spacing) const {
    const bool large = (minDiameter && diameter >= *minDiameter) ||
                       (minSamples && spacing > 0.0 && diameter >= *minSamples * spacing);
    return large && !(maxDiameter && diameter >= *maxDiameter) &&
           !(maxSamples && spacing > 0.0 && diameter >= *maxSamples * spacing);
}

std::shared_ptr<const SizeFrequencyDistribution> DefaultBoulderSizes() {
    static const std::shared_ptr<const SizeFrequencyDistribution> sizes =
        std::make_shared<TabulatedSfd>(Sp8023IntercraterBlocks(SpecificationCurve::Nominal));
    return sizes;
}

std::optional<double> BoulderEjecta::ThrowingDiameter() const {
    const double excavated = regolithThickness ? *regolithThickness / excavationDepthRatio : 0.0;
    const double smallest = std::fmax(minCraterDiameter.value_or(0.0), excavated);
    return smallest > 0.0 ? std::optional<double>(smallest) : std::nullopt;
}

double BoulderThrowDistance(double craterDiameter, double regolithThickness, double gravity) {
    for (const double value : {craterDiameter, regolithThickness, gravity}) {
        RequirePositive(value, "Crater diameter, regolith thickness and gravity");
    }
    return 138.5 / gravity * std::pow(craterDiameter / regolithThickness, 0.7086);
}

void ValidateBoulderPatches(const BoulderPatches& patches) {
    if (!(std::isfinite(patches.spread) && patches.spread >= 1.0 && IsPositive(patches.size))) {
        throw std::invalid_argument("Boulder patches need spread >= 1 and a positive size");
    }
}

void ValidateBoulderFillet(const BoulderFillet& fillet) {
    RequireUnitInterval(fillet.heightShare, "Boulder fillet heightShare");
    RequirePositive(fillet.widthRatio, "Boulder fillet widthRatio");
}

void ValidateBoulderClusters(const BoulderClusters& clusters) {
    RequireUnitInterval(clusters.share, "Boulder cluster share");
    if (clusters.model == BoulderClusterModel::Thomas) {
        if (!(clusters.radius > 0.0 && clusters.radius <= 2.0 * clusters.spacing &&
              std::isfinite(clusters.spacing))) {
            throw std::invalid_argument(
                "Thomas boulder clusters must satisfy 0 < radius <= 2 spacing");
        }
        return;
    }
    if (!(clusters.radius > 0.0 && clusters.radius < clusters.spacing &&
          std::isfinite(clusters.spacing))) {
        throw std::invalid_argument("Boulder clusters must satisfy 0 < radius < spacing");
    }
}

void ValidateBoulderFragmentation(const BoulderFragmentation& fragmentation) {
    RequirePositive(fragmentation.minDiameter, "Boulder fragmentation minDiameter");
    if (!(fragmentation.splitWear >= 0.0 && fragmentation.rubbleWear >= fragmentation.splitWear &&
          fragmentation.rubbleWear <= 1.0)) {
        throw std::invalid_argument(
            "Boulder fragmentation must satisfy 0 <= splitWear <= rubbleWear <= 1");
    }
    if (!(fragmentation.minPieces >= 2 && fragmentation.maxPieces >= fragmentation.minPieces &&
          fragmentation.maxPieces <= 12)) {
        throw std::invalid_argument("Boulder fragments must satisfy 2 <= min <= max <= 12 pieces");
    }
    RequireNonNegative(fragmentation.gapRatio, "Boulder fragmentation gapRatio");
    RequireNonNegative(fragmentation.spreadRatio, "Boulder fragmentation spreadRatio");
    RequireUnitInterval(fragmentation.slabShare, "Boulder fragmentation slabShare");
}

void ValidateBoulderVentifacts(const BoulderVentifacts& ventifacts) {
    RequireUnitInterval(ventifacts.share, "Boulder ventifact share");
    RequireUnitInterval(ventifacts.keelShare, "Boulder ventifact keelShare");
    if (!(ventifacts.minAngle > 0.0 && ventifacts.minAngle <= ventifacts.maxAngle &&
          ventifacts.maxAngle < 0.5 * kPi)) {
        throw std::invalid_argument(
            "Boulder ventifact facet angles must satisfy 0 < minAngle <= maxAngle < 90 deg");
    }
    if (!(std::isfinite(ventifacts.windAzimuth) &&
          (!ventifacts.secondWindAzimuth || std::isfinite(*ventifacts.secondWindAzimuth)))) {
        throw std::invalid_argument("Boulder ventifact wind azimuths must be finite");
    }
}

BoulderVentifacts GaleVentifacts() {
    BoulderVentifacts ventifacts;
    ventifacts.share = 0.5 * 0.78;
    ventifacts.keelShare = 12.0 / 39.0;
    return ventifacts;
}

double FilletReach(const Boulder& boulder, const BoulderFillet& fillet) {
    return (0.5 + fillet.widthRatio) * boulder.diameter;
}

double FilletHeight(const Boulder& boulder, double distance, const BoulderFillet& fillet) {
    const double radius = 0.5 * boulder.diameter;
    const double top = fillet.heightShare * boulder.ExposedHeight();
    if (distance <= radius) {
        return top;
    }
    const double t = (distance - radius) / (fillet.widthRatio * boulder.diameter);
    return t >= 1.0 ? 0.0 : top * (1.0 - t) * (1.0 - t);
}

void ValidateManualBoulder(const ManualBoulder& boulder) {
    RequireFinite(boulder.x, "A manual boulder's x");
    RequireFinite(boulder.y, "A manual boulder's y");
    RequirePositive(boulder.diameter, "A manual boulder's diameter");
    if (!std::isnan(boulder.height)) {
        RequirePositive(boulder.height, "A manual boulder's height");
    }
    if (!std::isnan(boulder.burial) && !(boulder.burial >= 0.0 && boulder.burial < 1.0)) {
        throw std::invalid_argument("A manual boulder's burial must be in [0, 1)");
    }
    const auto angle = [](double value, const char* what) {
        if (!std::isnan(value)) {
            RequireFinite(value, what);
        }
    };
    angle(boulder.yaw, "A manual boulder's yaw");
    angle(boulder.lean, "A manual boulder's lean");
    angle(boulder.tilt, "A manual boulder's tilt");
    angle(boulder.tiltAzimuth, "A manual boulder's tiltAzimuth");
    if (!std::isnan(boulder.wear)) {
        RequireUnitInterval(boulder.wear, "A manual boulder's wear");
    }
}

std::uint64_t ManualBoulderId(const ManualBoulder& boulder) {
    return boulder.id != 0 ? boulder.id : ManualCraterId(boulder.x, boulder.y, boulder.diameter);
}

void ValidateBoulderLayerOptions(const BoulderLayerOptions& options) {
    std::unordered_set<std::uint64_t> ids;
    for (const ManualBoulder& boulder : options.manual) {
        ValidateManualBoulder(boulder);
        if (!ids.insert(ManualBoulderId(boulder)).second) {
            throw std::invalid_argument("Manual boulders must not share an ID");
        }
    }
    if (options.generateRandom) {
        ValidateBoulderSizes(options.sizes);
        CheckGolombekRappTerms(options);
    }
    if (options.densityMap) {
        ValidateDensityMap(*options.densityMap);
    }
    if (options.slopeMap) {
        ValidateSizeSlopeMap(*options.slopeMap);
        if (options.generateRandom && !options.sizes.distribution) {
            throw std::invalid_argument(
                "A boulder size slope map needs a size distribution, not Golombek-Rapp sizes");
        }
    }
    ValidateBoulderPatches(options.patches);
    ValidateBoulderClusters(options.clusters);
    if (options.fragmentation) {
        ValidateBoulderFragmentation(*options.fragmentation);
    }
    ValidateBoulderBedrock(options.bedrock);
    RequirePositive(options.groundSpacing, "Boulder groundSpacing");
    if (options.generateRandom) {
        if (!options.sizes.distribution &&
            options.sizes.Coverage() * MaxBackgroundAbundance(options) >= 1.0) {
            throw std::invalid_argument("Boulder coverage times the patch spread, cluster and "
                                        "bedrock abundance must stay below 1");
        }
        ValidateBoulderEjecta(options.ejecta, options.sizes);
    }
    ValidateBoulderPose(options.pose);
    ValidateBoulderFillet(options.fillet);
    ValidateBoulderVentifacts(options.ventifacts);
    ValidateResolutionFade(options.fade);
    ValidateBoulderRendering(options.rendering);
    ValidateSourceResolvesPixels(options.sourceResolvesPixels);
}
}  // namespace terraingen
