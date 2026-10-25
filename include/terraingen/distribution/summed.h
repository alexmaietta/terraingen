// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Weighted sums of size-frequency distributions.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <memory>
#include <vector>

namespace terraingen {

/// One term of a `SummedSfd`: `distribution` scaled by `weight`.
struct WeightedSfd {
    double weight = 1.0;                                            ///< Positive, finite factor.
    std::shared_ptr<const SizeFrequencyDistribution> distribution;  ///< The scaled distribution.
};

/// The sum of weighted distributions of one kind, N(>=D) = sum of weight * N_i(>=D), such
/// as boulders between craters plus the blocks thrown out by them.
///
/// It is valid where every term is: the intersection of their valid ranges, with a term that allows
/// extrapolation counting as valid everywhere.
class SummedSfd final : public SizeFrequencyDistribution {
  public:
    /// The sum of `terms`, all of one kind.
    /// @throws std::invalid_argument for no terms, a null term, mixed kinds, a weight that is not
    /// positive and finite, or terms whose valid ranges do not overlap.
    explicit SummedSfd(std::vector<WeightedSfd> terms);

    /// The weighted terms of the sum.
    const std::vector<WeightedSfd>& Terms() const {
        return terms_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    /// The sum of `terms`, already checked to share `kind`.
    SummedSfd(SfdKind kind, std::vector<WeightedSfd>&& terms);

    std::vector<WeightedSfd> terms_;
};

}  // namespace terraingen
