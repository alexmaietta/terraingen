#include "terraingen/layers/boulders/boulder_options.h"

#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/summed.h"
#include "terraingen/noise/worley.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numbers>
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
    if (ejecta.gravity && !IsPositive(*ejecta.gravity)) {
        throw std::invalid_argument("Boulder ejecta gravity must be positive");
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
    if (!IsNonNegative(pose.maxLean)) {
        throw std::invalid_argument("Boulder maxLean must not be negative");
    }
    if (!(pose.burialByWear >= 0.0 && pose.burialByWear <= 1.0)) {
        throw std::invalid_argument("Boulder burialByWear must be in [0, 1]");
    }
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
    if (!(fillet.heightShare >= 0.0 && fillet.heightShare <= 1.0)) {
        throw std::invalid_argument("Boulder fillet heightShare must be in [0, 1]");
    }
    if (!IsPositive(fillet.widthRatio)) {
        throw std::invalid_argument("Boulder fillet widthRatio must be positive");
    }
}

void ValidateBoulderClusters(const BoulderClusters& clusters) {
    if (!(clusters.share >= 0.0 && clusters.share <= 1.0)) {
        throw std::invalid_argument("Boulder cluster share must be in [0, 1]");
    }
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

double ClusterBump(double distance, const BoulderClusters& clusters) {
    const double r = distance / clusters.radius;
    return std::exp(-0.5 * r * r);
}

double MeanClusterBump(const BoulderClusters& clusters) {
    constexpr int kSamples = 256;
    constexpr double kCells = 16.0;
    double sum = 0.0;
    for (int j = 0; j < kSamples; ++j) {
        for (int i = 0; i < kSamples; ++i) {
            const double u = (i + 0.5) * kCells / kSamples;
            const double v = (j + 0.5) * kCells / kSamples;
            const WorleyDistances d =
                Worley2(static_cast<float>(u), static_cast<float>(v), 0x434C5553u);
            sum += ClusterBump(static_cast<double>(d.f1) * clusters.spacing, clusters);
        }
    }
    return sum / (kSamples * kSamples);
}

double MeanThomasField(const BoulderClusters& clusters, bool body) {
    const double sigma = clusters.radius;
    const double reach = kThomasReachRadii;
    const double perArea = 1.0 / (clusters.spacing * clusters.spacing);
    if (body) {
        const double perVolume = perArea / (std::sqrt(2.0 * std::numbers::pi) * sigma);
        const double inside =
            std::erf(reach / std::numbers::sqrt2) -
            std::sqrt(2.0 / std::numbers::pi) * reach * std::exp(-0.5 * reach * reach);
        return perVolume * std::pow(2.0 * std::numbers::pi * sigma * sigma, 1.5) * inside;
    }
    return perArea * 2.0 * std::numbers::pi * sigma * sigma *
           (1.0 - std::exp(-0.5 * reach * reach));
}

double MaxClusterAbundance(const BoulderClusters& clusters) {
    if (clusters.share <= 0.0) {
        return 1.0;
    }
    if (clusters.model == BoulderClusterModel::Nearest) {
        return 1.0 - clusters.share + clusters.share / MeanClusterBump(clusters);
    }
    constexpr double kChance = 1e-6;
    const double ratio = clusters.radius / clusters.spacing;
    const double mean = 9.0 * std::numbers::pi * ratio * ratio;
    double term = std::exp(-mean);
    double above = 1.0 - term;
    int crowd = 0;
    while (above >= kChance) {
        ++crowd;
        term *= mean / crowd;
        above -= term;
    }
    return 1.0 - clusters.share +
           clusters.share * std::fmax(1.0, crowd) / MeanThomasField(clusters);
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
    if (!(fragmentation.slabShare >= 0.0 && fragmentation.slabShare <= 1.0)) {
        throw std::invalid_argument("Boulder fragmentation slabShare must be in [0, 1]");
    }
}

void ValidateBoulderVentifacts(const BoulderVentifacts& ventifacts) {
    const auto unit = [](double v) { return v >= 0.0 && v <= 1.0; };
    if (!(unit(ventifacts.share) && unit(ventifacts.keelShare))) {
        throw std::invalid_argument("Boulder ventifact share and keelShare must be in [0, 1]");
    }
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

namespace {

/// Coverages the Golombek-Rapp measures are tabulated at, log-spaced.
constexpr int kCoverageSteps = 512;
constexpr double kLeastCoverage = 1e-6;
constexpr double kMostCoverage = 0.999;

/// What `map` measures of `distribution`, with the areal fraction of Golombek-Rapp boulders in
/// closed form so that it needs no largest diameter.
double BoulderMeasure(const DensityMap& map, const SizeFrequencyDistribution& distribution) {
    const auto* rapp = dynamic_cast<const GolombekRappSfd*>(&distribution);
    if (rapp != nullptr && map.measure == DensityMeasure::ArealFraction) {
        const double beyond =
            std::isinf(map.maxDiameter) ? 0.0 : rapp->FractionalArea(map.maxDiameter);
        return rapp->FractionalArea(map.minDiameter) - beyond;
    }
    return MapMeasure(map, distribution);
}

}  // namespace

BoulderMapAbundance::BoulderMapAbundance(const BoulderLayerOptions& options) {
    if (!options.densityMap) {
        throw std::invalid_argument("Boulder map abundance needs a density map");
    }
    map_ = *options.densityMap;
    ValidateDensityMap(map_);
    const BoulderSizes& sizes = options.sizes;
    if (map_.total && options.ejecta.MinCraterDiameter() > 0.0) {
        ejectaMeasure_ = BoulderMeasure(map_, *EjectaSizes(options));
    }
    if (sizes.distribution || map_.measure == DensityMeasure::Scale) {
        measure_ = sizes.distribution ? BoulderMeasure(map_, *sizes.distribution) : 1.0;
        if (!(measure_ > 0.0 && std::isfinite(measure_))) {
            throw std::invalid_argument("A boulder density map must measure some of the sizes");
        }
    } else {
        coverage_ = sizes.coverage;
        for (int i = 0; i < kCoverageSteps; ++i) {
            const double k =
                kLeastCoverage * std::pow(kMostCoverage / kLeastCoverage,
                                          static_cast<double>(i) / (kCoverageSteps - 1));
            coverages_.push_back(k);
            measures_.push_back(BoulderMeasure(
                map_, GolombekRappSfd(k, sizes.qConstant, sizes.qCoverageCoefficient)));
        }
        if (!(measures_.back() > 0.0)) {
            throw std::invalid_argument("A boulder density map must measure some of the sizes");
        }
    }
    const double top = map_.field->Max();
    const double largest = map_.measure == DensityMeasure::Count ? top / map_.areaUnit : top;
    max_ = std::fmax(1.0, At(largest, 0.0));
}

double BoulderMapAbundance::Coverage(double target) const {
    if (target <= measures_.front()) {
        return coverages_.front() * target / measures_.front();
    }
    if (target >= measures_.back()) {
        return coverages_.back();
    }
    const auto above = std::lower_bound(measures_.begin(), measures_.end(), target);
    const auto i = static_cast<std::size_t>(above - measures_.begin());
    const double t = (target - measures_[i - 1]) / (measures_[i] - measures_[i - 1]);
    return coverages_[i - 1] + t * (coverages_[i] - coverages_[i - 1]);
}

double BoulderMapAbundance::At(double value, double ejecta) const {
    if (std::isnan(value)) {
        return 1.0;
    }
    if (map_.measure == DensityMeasure::Scale) {
        return std::fmax(0.0, value);
    }
    const double target = value - ejecta * ejectaMeasure_;
    if (!(target > 0.0)) {
        return 0.0;
    }
    return coverages_.empty() ? target / measure_ : Coverage(target) / coverage_;
}

double MeanPatchAbundance(const BoulderPatches& patches) {
    if (!(patches.spread > 1.0)) {
        return 1.0;
    }
    constexpr int kSamples = 256;
    constexpr double kPatches = 32.0;
    double sum = 0.0;
    for (int j = 0; j < kSamples; ++j) {
        for (int i = 0; i < kSamples; ++i) {
            const double x = (i + 0.5) * kPatches / kSamples;
            const double y = (j + 0.5) * kPatches / kSamples;
            sum += std::pow(patches.spread, PatchNoise({}, x, y, 1.0, 0x50415443u));
        }
    }
    return sum / (kSamples * kSamples);
}

double MaxBackgroundAbundance(const BoulderLayerOptions& options) {
    const double a = options.bedrock.abundance;
    double patches = options.patches.spread;
    double bedrock = options.bedrock.exposure ? std::fmax(1.0, a) : 1.0;
    double map = 1.0;
    if (options.densityMap) {
        patches /= MeanPatchAbundance(options.patches);
        bedrock = options.bedrock.exposure ? std::fmax(1.0, a) / std::fmin(1.0, a) : 1.0;
        map = BoulderMapAbundance(options).Max();
    }
    return patches * bedrock * MaxClusterAbundance(options.clusters) * map;
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
        if (options.slopeMap) {
            const SizeSlopeMap& map = *options.slopeMap;
            const double d0 = map.referenceDiameter;
            const double count = richest * sizes.distribution->Cumulative(d0);
            const double low = map.minExponent;
            const double high = map.maxExponent;
            terms.push_back(
                {1.0, std::make_shared<PowerLawSfd>(count * std::pow(d0, low) * high / low, low)});
            terms.push_back({1.0, std::make_shared<PowerLawSfd>(count * std::pow(d0, high), high)});
        }
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
