#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <memory>
#include <vector>

namespace terraingen {

/// One term of a `SummedSfd`: `distribution` scaled by `weight`.
struct WeightedSfd {
    double weight = 1.0;
    std::shared_ptr<const SizeFrequencyDistribution> distribution;
};

/// The sum of weighted distributions of one kind, N(>=D) = sum of weight * N_i(>=D), such as
/// boulders between craters plus the blocks thrown out by them, or a population scaled by a factor.
///
/// It is valid where every term is: the intersection of their valid ranges, with a term that allows
/// extrapolation counting as valid everywhere.
class SummedSfd final : public SizeFrequencyDistribution {
  public:
    /// @throws std::invalid_argument for no terms, a null term, mixed kinds, a weight that is not
    /// positive and finite, or terms whose valid ranges do not overlap.
    explicit SummedSfd(std::vector<WeightedSfd> terms);

    const std::vector<WeightedSfd>& Terms() const {
        return terms_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    std::vector<WeightedSfd> terms_;
};

}  // namespace terraingen
