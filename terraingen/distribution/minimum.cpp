#include "terraingen/distribution/minimum.h"

#include "terraingen/distribution/composition.h"

#include <utility>

namespace terraingen {

namespace {

/// The intersection of the checked inputs' valid ranges, whether or not they allow extrapolation.
DiameterRange IntersectValidRanges(
    const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& distributions) {
    std::vector<DiameterRange> ranges;
    for (const auto& distribution : distributions) {
        ranges.push_back(distribution->ValidRange());
    }
    return detail::IntersectRanges(ranges, "The distributions' valid ranges do not overlap");
}

}  // namespace

MinimumSfd::MinimumSfd(std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions)
    : MinimumSfd(detail::CommonKind(detail::Distributions(distributions), "A minimum"),
                 std::move(distributions)) {}

MinimumSfd::MinimumSfd(
    SfdKind kind, std::vector<std::shared_ptr<const SizeFrequencyDistribution>>&& distributions)
    : SizeFrequencyDistribution(kind, IntersectValidRanges(distributions)),
      distributions_(std::move(distributions)) {}

std::pair<const SizeFrequencyDistribution*, double> MinimumSfd::LowestAt(double diameter) const {
    const SizeFrequencyDistribution* lowest = distributions_.front().get();
    double value = UncheckedCumulative(*lowest, diameter);
    for (std::size_t n = 1; n < distributions_.size(); ++n) {
        const auto& distribution = distributions_[n];
        const double candidate = UncheckedCumulative(*distribution, diameter);
        if (candidate < value) {
            value = candidate;
            lowest = distribution.get();
        }
    }
    return {lowest, value};
}

double MinimumSfd::CumulativeAt(double diameter) const {
    return LowestAt(diameter).second;
}

double MinimumSfd::IncrementalAt(double diameter) const {
    return UncheckedIncremental(*LowestAt(diameter).first, diameter);
}

}  // namespace terraingen
