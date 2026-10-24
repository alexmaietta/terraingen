// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_abundance.h"

#include "terraingen/distribution/golombek_rapp.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/summed.h"
#include "terraingen/layers/boulders/boulder_clusters.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

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

/// The largest background abundance with patches averaging `patchMean` and a density map whose
/// largest abundance is `mapMax`, if there is a map.
double MaxBackground(const BoulderLayerOptions& options, double patchMean,
                     std::optional<double> mapMax) {
    const double a = options.bedrock.abundance;
    double patches = options.patches.spread;
    double bedrock = options.bedrock.exposure ? std::fmax(1.0, a) : 1.0;
    double map = 1.0;
    if (mapMax) {
        patches /= patchMean;
        bedrock = options.bedrock.exposure ? std::fmax(1.0, a) / std::fmin(1.0, a) : 1.0;
        map = *mapMax;
    }
    return patches * bedrock * MaxClusterAbundance(options.clusters) * map;
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
        coverage_ = sizes.Coverage();
        for (int i = 0; i < kCoverageSteps; ++i) {
            const double k =
                kLeastCoverage * std::pow(kMostCoverage / kLeastCoverage,
                                          static_cast<double>(i) / (kCoverageSteps - 1));
            coverages_.push_back(k);
            measures_.push_back(BoulderMeasure(
                map_, GolombekRappSfd(k, sizes.QConstant(), sizes.QCoverageCoefficient())));
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
    return LatticeMean(32.0, [&](double x, double y) {
        return std::pow(patches.spread, PatchNoise({}, x, y, 1.0, kBoulderPatchStream));
    });
}

BoulderBackground::BoulderBackground(const BoulderLayerOptions& options) {
    if (options.densityMap) {
        patchMean = MeanPatchAbundance(options.patches);
        map.emplace(options);
    }
    max = MaxBackground(options, patchMean, map ? std::optional<double>(map->Max()) : std::nullopt);
}

double MaxBackgroundAbundance(const BoulderLayerOptions& options) {
    return BoulderBackground(options).max;
}

std::shared_ptr<const SizeFrequencyDistribution> EjectaSizes(const BoulderLayerOptions& options) {
    const BoulderEjecta& ejecta = options.ejecta;
    if (ejecta.distribution) {
        return ejecta.distribution;
    }
    return std::make_shared<GolombekRappSfd>(ejecta.Coverage(), options.sizes.QConstant(),
                                             options.sizes.QCoverageCoefficient());
}

std::shared_ptr<const SizeFrequencyDistribution>
BoulderCandidates(const BoulderLayerOptions& options) {
    ValidateBoulderLayerOptions(options);
    if (!options.generateRandom) {
        throw std::invalid_argument(
            "A boulder layer without generateRandom draws no candidate boulders");
    }
    return BoulderCandidates(options, MaxBackgroundAbundance(options));
}

std::shared_ptr<const SizeFrequencyDistribution>
BoulderCandidates(const BoulderLayerOptions& options, double richest) {
    const BoulderSizes& sizes = options.sizes;
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
            {1.0, std::make_shared<GolombekRappSfd>(sizes.Coverage() * richest, sizes.QConstant(),
                                                    sizes.QCoverageCoefficient())});
    }
    if (options.ejecta.MinCraterDiameter() > 0.0) {
        terms.push_back({1.0, EjectaSizes(options)});
    }
    return std::make_shared<SummedSfd>(std::move(terms));
}

}  // namespace terraingen
