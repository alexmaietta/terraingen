// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_options.h"

#include "terraingen/distribution/sp8023.h"
#include "terraingen/layers/boulders/boulder_abundance.h"
#include "terraingen/layers/boulders/boulder_fields.h"
#include "terraingen/util/bits.h"
#include "util/checks.h"

#include <algorithm>
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

/// Checks what boulder sizes tie together, their ranges checked: min < max diameter, and a density
/// distribution covering them or a coverage in (0, 1).
void CheckBoulderSizes(const BoulderSizes& sizes) {
    if (!(sizes.maxDiameter > sizes.minDiameter)) {
        throw std::invalid_argument("Boulder diameters must satisfy minDiameter < maxDiameter");
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

/// Checks what crater blocks that craters throw tie together, their ranges checked: a
/// distribution that covers `sizes`, or else a coverage.
void CheckBoulderEjecta(const BoulderEjecta& ejecta, const BoulderSizes& sizes) {
    if (!ejecta.ThrowingDiameter()) {
        return;
    }
    if (ejecta.distribution) {
        CheckDistribution(*ejecta.distribution, sizes, "The boulder ejecta distribution");
    } else if (!(ejecta.Coverage() > 0.0 && ejecta.Coverage() < 1.0)) {
        throw std::invalid_argument("Boulder ejecta coverage must be between 0 and 1");
    }
}

/// Checks what a boulder pose ties together, its ranges checked.
void CheckBoulderPose(const BoulderPose& pose) {
    if (!(pose.maxHeightRatio >= pose.minHeightRatio)) {
        throw std::invalid_argument("Boulder height ratios must satisfy min <= max");
    }
    if (!(pose.maxBurial >= pose.minBurial)) {
        throw std::invalid_argument("Boulder burial must satisfy min <= max");
    }
    if (!(pose.sideShare + pose.endShare <= 1.0)) {
        throw std::invalid_argument("Boulder sideShare and endShare must sum to at most 1");
    }
    if (pose.rollDistance && !pose.maxSlopeDegrees) {
        throw std::invalid_argument("Boulder rollDistance needs a maxSlopeDegrees");
    }
}

/// Checks what the drawing of boulders ties together, its ranges checked.
void CheckBoulderRendering(const BoulderRendering& rendering) {
    const BoulderReliefRange& range = rendering.reliefRange;
    if (range.maxDiameter && !(*range.maxDiameter > range.minDiameter)) {
        throw std::invalid_argument("Boulder relief maxDiameter must exceed its minDiameter");
    }
    ValidateBoulderShapeOptions(rendering.shapes);
}

}  // namespace

bool BoulderModelThreshold::IsModel(double diameter, double spacing) const {
    const bool large = (minDiameter && diameter >= *minDiameter) ||
                       (minPixels && spacing > 0.0 && diameter >= *minPixels * spacing);
    return large && !(maxDiameter && diameter >= *maxDiameter) &&
           !(maxPixels && spacing > 0.0 && diameter >= *maxPixels * spacing);
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
    CheckRanges(patches);
}

void ValidateBoulderFillet(const BoulderFillet& fillet) {
    CheckRanges(fillet);
}

void ValidateBoulderClusters(const BoulderClusters& clusters) {
    CheckRanges(clusters);
    if (clusters.model == BoulderClusterModel::Thomas) {
        if (!(clusters.radius <= 2.0 * clusters.centerSpacing)) {
            throw std::invalid_argument("Thomas boulder clusters must satisfy radius <= 2 spacing");
        }
    } else if (!(clusters.radius < clusters.centerSpacing)) {
        throw std::invalid_argument("Boulder clusters must satisfy radius < spacing");
    }
}

void ValidateBoulderFragmentation(const BoulderFragmentation& fragmentation) {
    CheckRanges(fragmentation);
    if (!(fragmentation.rubbleWear >= fragmentation.splitWear)) {
        throw std::invalid_argument("Boulder fragmentation must satisfy splitWear <= rubbleWear");
    }
    if (!(fragmentation.maxPieces >= fragmentation.minPieces)) {
        throw std::invalid_argument("Boulder fragments must satisfy minPieces <= maxPieces");
    }
}

void ValidateBoulderVentifacts(const BoulderVentifacts& ventifacts) {
    CheckRanges(ventifacts);
    if (!(ventifacts.minAngleDegrees <= ventifacts.maxAngleDegrees)) {
        throw std::invalid_argument(
            "Boulder ventifact facet angles must satisfy minAngleDegrees <= maxAngleDegrees");
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

FilletContact DomeFilletContact(const Boulder& boulder, const BoulderFillet& fillet) {
    const double share = std::clamp(fillet.heightShare, 0.0, 1.0);
    return {0.5 * boulder.diameter * std::sqrt(1.0 - share * share),
            share * boulder.ExposedHeight()};
}

double FilletHeight(const Boulder& boulder, double distance, const BoulderFillet& fillet,
                    const FilletContact& contact) {
    if (distance < contact.distance || !(contact.height > 0.0)) {
        return 0.0;
    }
    const double reach =
        std::fmax(0.5 * boulder.diameter, contact.distance) + fillet.widthRatio * boulder.diameter;
    const double t = (distance - contact.distance) / (reach - contact.distance);
    return t >= 1.0 ? 0.0 : contact.height * (1.0 - t) * (1.0 - t);
}

double FilletHeight(const Boulder& boulder, double distance, const BoulderFillet& fillet) {
    return FilletHeight(boulder, distance, fillet, DomeFilletContact(boulder, fillet));
}

void ValidateManualBoulder(const ManualBoulder& boulder) {
    CheckRanges(boulder);
}

std::uint64_t ManualBoulderId(const ManualBoulder& boulder) {
    return boulder.id.value_or(0u) != 0 ? *boulder.id
                                        : ManualCraterId(boulder.x, boulder.y, boulder.diameter);
}

void ValidateBoulderLayerOptions(const BoulderLayerOptions& options) {
    CheckRanges(options);
    std::unordered_set<std::uint64_t> ids;
    for (const ManualBoulder& boulder : options.manual) {
        ValidateManualBoulder(boulder);
        if (!ids.insert(ManualBoulderId(boulder)).second) {
            throw std::invalid_argument("Manual boulders must not share an ID");
        }
    }
    if (options.generateRandom) {
        CheckBoulderSizes(options.sizes);
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
    ValidateBoulderClusters(options.clusters);
    if (options.fragmentation) {
        ValidateBoulderFragmentation(*options.fragmentation);
    }
    if (options.bedrock.exposure) {
        ValidateExposureParams(*options.bedrock.exposure);
    }
    if (options.generateRandom) {
        if (!options.sizes.distribution &&
            options.sizes.Coverage() * MaxBackgroundAbundance(options) >= 1.0) {
            throw std::invalid_argument("Boulder coverage times the patch spread, cluster and "
                                        "bedrock abundance must stay below 1");
        }
        CheckBoulderEjecta(options.ejecta, options.sizes);
    }
    CheckBoulderPose(options.pose);
    ValidateBoulderVentifacts(options.ventifacts);
    ValidateResolutionFade(options.fade);
    CheckBoulderRendering(options.rendering);
}
}  // namespace terraingen
