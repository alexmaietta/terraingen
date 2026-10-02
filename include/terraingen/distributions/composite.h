#pragma once

#include "terraingen/distributions/size_frequency.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace terraingen {

/// One piece of a `SplicedSfd`: `distribution` applies from `start` metres up to the next piece's
/// start.
struct SfdPiece {
    double start = 0.0;
    std::shared_ptr<const SizeFrequencyDistribution> distribution;
};

/// A cumulative distribution made of other distributions over consecutive diameter intervals, such
/// as a production function extended below its fitted range by a power law.
///
/// As in `PiecewisePowerLaw`, the lower piece applies at a join, and pieces must meet there within
/// a relative tolerance.
class SplicedSfd final : public SizeFrequencyDistribution {
  public:
    /// @param pieces At least one piece, with strictly increasing `start`, the first `start` equal
    /// to `range.min`, and each distribution valid (or extrapolating) over its interval.
    /// @throws std::invalid_argument for malformed pieces, mixed kinds, a piece not valid over its
    /// interval, or a join that rises or is discontinuous by more than `tolerance`.
    SplicedSfd(std::vector<SfdPiece> pieces, DiameterRange range, double tolerance = 1e-3);

    const std::vector<SfdPiece>& Pieces() const {
        return pieces_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::size_t PieceAt(double diameter) const;

    std::vector<SfdPiece> pieces_;
};

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
