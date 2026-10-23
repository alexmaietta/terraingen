// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/minimum.h"

#include "terraingen/distribution/composition.h"

#include <utility>

namespace terraingen {

MinimumSfd::MinimumSfd(std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions)
    : MinimumSfd(detail::CommonKind(detail::Distributions(distributions), "A minimum"),
                 std::move(distributions)) {}

MinimumSfd::MinimumSfd(
    SfdKind kind, std::vector<std::shared_ptr<const SizeFrequencyDistribution>>&& distributions)
    : SizeFrequencyDistribution(
          kind, detail::IntersectRanges(
                    distributions,
                    [](const SizeFrequencyDistribution& each) { return each.ValidRange(); },
                    "The distributions' valid ranges do not overlap")),
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
