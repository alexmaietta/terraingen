#include "terraingen/layers/boulders/boulder_options.h"

#include "terraingen/layers/boulders/boulder_abundance.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace terraingen {

namespace {

/// Whether `distribution` may be evaluated from `low` to `high` metres.
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
    if (ejecta.gravity) {
        RequirePositive(*ejecta.gravity, "Boulder ejecta gravity");
    }
}

}  // namespace

bool BoulderModelThreshold::IsModel(double diameter, double spacing) const {
    const bool large = (minDiameter && diameter >= *minDiameter) ||
                       (minSamples && spacing > 0.0 && diameter >= *minSamples * spacing);
    return large && !(maxDiameter && diameter >= *maxDiameter) &&
           !(maxSamples && spacing > 0.0 && diameter >= *maxSamples * spacing);
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
    RequireNonNegative(ejecta.minCraterDiameter, "Boulder ejecta minCraterDiameter");
    if (ejecta.regolithThickness) {
        RequirePositive(*ejecta.regolithThickness, "Boulder ejecta regolithThickness");
    }
    if (!(ejecta.excavationDepthRatio > 0.0 && ejecta.excavationDepthRatio <= 1.0)) {
        throw std::invalid_argument("Boulder ejecta excavationDepthRatio must be in (0, 1]");
    }
    if (ejecta.MinCraterDiameter() > 0.0) {
        CheckThrownBlocks(ejecta, sizes);
    }
}

void ValidateBoulderBedrock(const BoulderBedrock& bedrock) {
    RequirePositive(bedrock.abundance, "Boulder bedrock abundance");
    if (bedrock.exposure) {
        ValidateExposureParams(*bedrock.exposure);
    }
    RequirePositive(bedrock.meanSize, "Boulder bedrock meanSize");
}

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
    if (!(pose.maxTilt >= 0.0 && pose.maxTilt <= std::numbers::pi)) {
        throw std::invalid_argument("Boulder maxTilt must be in [0, pi]");
    }
    if (pose.maxSlope && !(*pose.maxSlope > 0.0 && *pose.maxSlope < 0.5 * std::numbers::pi)) {
        throw std::invalid_argument("Boulder maxSlope must be in (0, pi / 2)");
    }
    if (pose.rollDistance &&
        !(pose.maxSlope && *pose.rollDistance > 0.0 && std::isfinite(*pose.rollDistance))) {
        throw std::invalid_argument("Boulder rollDistance must be positive and finite, with a "
                                    "maxSlope");
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
          ventifacts.maxAngle < 0.5 * std::numbers::pi)) {
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

void ValidateBoulderLayerOptions(const BoulderLayerOptions& options) {
    ValidateBoulderSizes(options.sizes);
    if (options.densityMap) {
        ValidateDensityMap(*options.densityMap);
    }
    if (options.slopeMap) {
        ValidateSizeSlopeMap(*options.slopeMap);
        if (!options.sizes.distribution) {
            throw std::invalid_argument(
                "A boulder size slope map needs a size distribution, not Golombek-Rapp sizes");
        }
    }
    ValidateBoulderPatches(options.patches);
    ValidateBoulderClusters(options.clusters);
    ValidateBoulderFragmentation(options.fragmentation);
    ValidateBoulderBedrock(options.bedrock);
    RequirePositive(options.groundSpacing, "Boulder groundSpacing");
    if (!options.sizes.distribution &&
        options.sizes.coverage * MaxBackgroundAbundance(options) >= 1.0) {
        throw std::invalid_argument("Boulder coverage times the patch spread, cluster and bedrock "
                                    "abundance must stay below 1");
    }
    ValidateBoulderEjecta(options.ejecta, options.sizes);
    ValidateBoulderPose(options.pose);
    ValidateBoulderFillet(options.fillet);
    ValidateBoulderVentifacts(options.ventifacts);
    ValidateResolutionFade(options.fade);
    ValidateBoulderRendering(options.rendering);
}
}  // namespace terraingen
