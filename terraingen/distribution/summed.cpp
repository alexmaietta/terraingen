#include "terraingen/distribution/summed.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

const std::vector<WeightedSfd>& Checked(const std::vector<WeightedSfd>& terms) {
    if (terms.empty()) {
        throw std::invalid_argument("A summed distribution needs at least one term");
    }
    for (const WeightedSfd& term : terms) {
        if (!term.distribution) {
            throw std::invalid_argument("A summed distribution cannot hold a null term");
        }
        if (!(std::isfinite(term.weight) && term.weight > 0.0)) {
            throw std::invalid_argument("Summed distribution weights must be positive and finite");
        }
        if (term.distribution->Kind() != terms.front().distribution->Kind()) {
            throw std::invalid_argument("Summed terms must be distributions of one kind");
        }
    }
    return terms;
}

DiameterRange Overlap(const std::vector<WeightedSfd>& terms) {
    DiameterRange range{0.0, std::numeric_limits<double>::infinity()};
    for (const WeightedSfd& term : Checked(terms)) {
        if (term.distribution->AllowsExtrapolation()) {
            continue;
        }
        range.min = std::fmax(range.min, term.distribution->ValidRange().min);
        range.max = std::fmin(range.max, term.distribution->ValidRange().max);
    }
    if (!(range.min < range.max)) {
        throw std::invalid_argument("Summed terms must have overlapping valid ranges");
    }
    return range;
}

}  // namespace

SummedSfd::SummedSfd(std::vector<WeightedSfd> terms)
    : SizeFrequencyDistribution(Checked(terms).front().distribution->Kind(), Overlap(terms)),
      terms_(std::move(terms)) {}

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
