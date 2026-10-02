#pragma once

#include "terraingen/distributions/size_frequency.h"

#include <memory>
#include <vector>

namespace terraingen {

/// The pointwise minimum of distributions of one kind, such as the DSNE construction of a surface's
/// crater density: the lesser of the production surface and the equilibrium density at every
/// diameter.
///
/// The valid range is the intersection of the inputs' ranges.
class MinimumSfd final : public SizeFrequencyDistribution {
  public:
    /// @throws std::invalid_argument if `distributions` is empty, holds a null pointer, mixes
    /// kinds, or the valid ranges do not overlap.
    explicit MinimumSfd(
        std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions);

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
