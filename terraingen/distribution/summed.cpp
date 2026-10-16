/// @file
/// @brief Weighted sums of size-frequency distributions.
#include "terraingen/distribution/summed.h"

#include "terraingen/distribution/combined.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// The kind of `terms`, after checking them.
SfdKind CheckedKind(const std::vector<WeightedSfd>& terms) {
    std::vector<const SizeFrequencyDistribution*> distributions;
    for (const WeightedSfd& term : terms) {
        distributions.push_back(term.distribution.get());
    }
    const SfdKind kind = detail::CommonKind(distributions, "A summed distribution");
    for (const WeightedSfd& term : terms) {
        RequirePositive(term.weight, "Summed distribution weights");
    }
    return kind;
}

/// Where every term may be evaluated, after checking the terms.
DiameterRange Overlap(const std::vector<WeightedSfd>& terms) {
    CheckedKind(terms);
    DiameterRange range;
    for (const WeightedSfd& term : terms) {
        const DiameterRange usable = detail::UsableRange(*term.distribution);
        range.min = std::fmax(range.min, usable.min);
        range.max = std::fmin(range.max, usable.max);
    }
    if (!(range.min < range.max)) {
        throw std::invalid_argument("Summed terms must have overlapping valid ranges");
    }
    return range;
}

}  // namespace

SummedSfd::SummedSfd(std::vector<WeightedSfd> terms)
    : SizeFrequencyDistribution(CheckedKind(terms), Overlap(terms)), terms_(std::move(terms)) {}

double SummedSfd::CumulativeAt(double diameter) const {
    double sum = 0.0;
    for (const WeightedSfd& term : terms_) {
        sum += term.weight * CumulativeOf(*term.distribution, diameter);
    }
    return sum;
}

double SummedSfd::IncrementalAt(double diameter) const {
    double sum = 0.0;
    for (const WeightedSfd& term : terms_) {
        sum += term.weight * IncrementalOf(*term.distribution, diameter);
    }
    return sum;
}

}  // namespace terraingen
