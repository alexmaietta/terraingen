#include "terraingen/distribution/minimum.h"

#include "terraingen/distribution/combined.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

SfdKind
CommonKind(const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& distributions) {
    std::vector<const SizeFrequencyDistribution*> pointers;
    for (const auto& distribution : distributions) {
        pointers.push_back(distribution.get());
    }
    return detail::CommonKind(pointers, "A minimum");
}

/// The intersection of the inputs' valid ranges, whether or not they allow extrapolation.
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
    for (std::size_t n = 1; n < distributions_.size(); ++n) {
        const auto& distribution = distributions_[n];
        const double candidate = CumulativeOf(*distribution, diameter);
        if (candidate < value) {
            value = candidate;
            lowest = distribution.get();
        }
    }
    return *lowest;
}

double MinimumSfd::CumulativeAt(double diameter) const {
    double value = CumulativeOf(*distributions_.front(), diameter);
    for (std::size_t n = 1; n < distributions_.size(); ++n) {
        const double candidate = CumulativeOf(*distributions_[n], diameter);
        if (candidate < value) {
            value = candidate;
        }
    }
    return value;
}

double MinimumSfd::IncrementalAt(double diameter) const {
    return IncrementalOf(LowestAt(diameter), diameter);
}

}  // namespace terraingen
