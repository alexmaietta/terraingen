/// @file
/// @brief Checks shared by the distributions built from others or from pieces.
#include "terraingen/distribution/combined.h"

#include <stdexcept>

namespace terraingen::detail {

SfdKind CommonKind(std::span<const SizeFrequencyDistribution* const> distributions,
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
