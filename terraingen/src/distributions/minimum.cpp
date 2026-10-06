#include "terraingen/distributions/minimum.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

SfdKind
CommonKind(const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& distributions) {
    if (distributions.empty() ||
        std::any_of(distributions.begin(), distributions.end(),
                    [](const auto& distribution) { return !distribution; })) {
        throw std::invalid_argument("A minimum needs at least one distribution and no nulls");
    }
    const SfdKind kind = distributions.front()->Kind();
    if (std::any_of(distributions.begin(), distributions.end(),
                    [kind](const auto& distribution) { return distribution->Kind() != kind; })) {
        throw std::invalid_argument("A minimum needs distributions of one kind");
    }
    return kind;
}

DiameterRange IntersectRanges(
    const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& distributions) {
    CommonKind(distributions);
    DiameterRange range;
    for (const auto& distribution : distributions) {
        range.min = std::fmax(range.min, distribution->ValidRange().min);
        range.max = std::fmin(range.max, distribution->ValidRange().max);
    }
    if (!(range.max > range.min)) {
        throw std::invalid_argument("The distributions' valid ranges do not overlap");
    }
    return range;
}

}  // namespace

MinimumSfd::MinimumSfd(std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions)
    : SizeFrequencyDistribution(CommonKind(distributions), IntersectRanges(distributions)),
      distributions_(std::move(distributions)) {}

const SizeFrequencyDistribution& MinimumSfd::LowestAt(double diameter) const {
    const SizeFrequencyDistribution* lowest = distributions_.front().get();
    double value = CumulativeOf(*lowest, diameter);
    for (const auto& distribution : distributions_) {
        const double candidate = CumulativeOf(*distribution, diameter);
        if (candidate < value) {
            value = candidate;
            lowest = distribution.get();
        }
    }
    return *lowest;
}

double MinimumSfd::CumulativeAt(double diameter) const {
    return CumulativeOf(LowestAt(diameter), diameter);
}

double MinimumSfd::IncrementalAt(double diameter) const {
    return IncrementalOf(LowestAt(diameter), diameter);
}

}  // namespace terraingen
