// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/summed.h"

#include "terraingen/distribution/composition.h"
#include "terraingen/util/checks.h"

#include <utility>

namespace terraingen {

namespace {

/// The kind of `terms`, after checking them.
SfdKind CheckedKind(const std::vector<WeightedSfd>& terms) {
    const SfdKind kind = detail::CommonKind(detail::Distributions(terms), "A summed distribution");
    for (const WeightedSfd& term : terms) {
        RequirePositive(term.weight, "Summed distribution weights");
    }
    return kind;
}

}  // namespace

SummedSfd::SummedSfd(std::vector<WeightedSfd> terms)
    : SummedSfd(CheckedKind(terms), std::move(terms)) {}

SummedSfd::SummedSfd(SfdKind kind, std::vector<WeightedSfd>&& terms)
    : SizeFrequencyDistribution(
          kind, detail::IntersectRanges(terms, detail::UsableRange,
                                        "Summed terms must have overlapping valid ranges")),
      terms_(std::move(terms)) {}

double SummedSfd::CumulativeAt(double diameter) const {
    double sum = 0.0;
    for (const WeightedSfd& term : terms_) {
        sum += term.weight * UncheckedCumulative(*term.distribution, diameter);
    }
    return sum;
}

double SummedSfd::IncrementalAt(double diameter) const {
    double sum = 0.0;
    for (const WeightedSfd& term : terms_) {
        sum += term.weight * UncheckedIncremental(*term.distribution, diameter);
    }
    return sum;
}

}  // namespace terraingen
