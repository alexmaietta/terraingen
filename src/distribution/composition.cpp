// SPDX-License-Identifier: BSD-3-Clause

#include "distribution/composition.h"

#include <stdexcept>

namespace terraingen::detail {

SfdKind CommonKind(Span<const SizeFrequencyDistribution* const> distributions,
                   const std::string& what) {
    if (distributions.empty()) {
        throw std::invalid_argument(what + " needs at least one distribution");
    }
    for (const SizeFrequencyDistribution* distribution : distributions) {
        if (distribution == nullptr) {
            throw std::invalid_argument(what + " cannot hold a null distribution");
        }
        if (distribution->Kind() != distributions.front()->Kind()) {
            throw std::invalid_argument(what + " needs distributions of one kind");
        }
    }
    return distributions.front()->Kind();
}

DiameterRange UsableRange(const SizeFrequencyDistribution& distribution) {
    return distribution.AllowsExtrapolation() ? DiameterRange{} : distribution.ValidRange();
}

DiameterRange IntersectRanges(Span<const DiameterRange> ranges, const std::string& message) {
    DiameterRange range;
    for (const DiameterRange& each : ranges) {
        range.min = std::fmax(range.min, each.min);
        range.max = std::fmin(range.max, each.max);
    }
    if (!(range.min < range.max)) {
        throw std::invalid_argument(message);
    }
    return range;
}

void CheckStarts(Span<const double> starts, const DiameterRange& range, const std::string& what) {
    if (starts.empty() || starts.front() != range.min) {
        throw std::invalid_argument(what + " starts must begin at the range minimum");
    }
    for (std::size_t i = 1; i < starts.size(); ++i) {
        if (!(std::isfinite(starts[i]) && starts[i] > starts[i - 1] && starts[i] < range.max)) {
            throw std::invalid_argument(what + " starts must increase within the range");
        }
    }
}

void CheckJoin(double below, double above, double start, double tolerance,
               bool allowDiscontinuities) {
    if (above > below * (1.0 + tolerance)) {
        throw std::invalid_argument("A cumulative distribution cannot rise at the join at " +
                                    std::to_string(start) + " m");
    }
    if (!allowDiscontinuities && above < below * (1.0 - tolerance)) {
        throw std::invalid_argument("The pieces are discontinuous at " + std::to_string(start) +
                                    " m");
    }
}

}  // namespace terraingen::detail
