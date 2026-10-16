/// @file
/// @brief The pointwise minimum of size-frequency distributions.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <memory>
#include <vector>

namespace terraingen {

/// The pointwise minimum of distributions of one kind, such as the DSNE crater density: the
/// lesser of the production surface and the equilibrium density at every diameter.
///
/// The valid range is the intersection of the inputs' valid ranges, even for inputs that allow
/// extrapolation, which `SummedSfd` and `SplicedSfd` treat as valid everywhere.
class MinimumSfd final : public SizeFrequencyDistribution {
  public:
    /// The minimum of `distributions`, all of one kind.
    /// @throws std::invalid_argument if `distributions` is empty, holds a null pointer, mixes
    /// kinds, or the valid ranges do not overlap.
    explicit MinimumSfd(
        std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions);

    /// The distributions whose minimum this is.
    const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& Distributions() const {
        return distributions_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    const SizeFrequencyDistribution& LowestAt(double diameter) const;

    std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions_;
};

}  // namespace terraingen
