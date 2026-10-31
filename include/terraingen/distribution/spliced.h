// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Size-frequency distributions spliced from others over consecutive diameter intervals.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace terraingen {

/// One piece of a `SplicedSfd`: `distribution` applies from `start` meters up to the next
/// piece's start.
struct SfdPiece {
    double start = 0.0;                                             ///< First diameter, meters.
    std::shared_ptr<const SizeFrequencyDistribution> distribution;  ///< Applies from `start`.
};

/// A cumulative distribution made of other distributions over consecutive diameter
/// intervals, such as a production function extended below its fitted range by a power law.
///
/// As in `PiecewisePowerLawSfd`, the lower piece applies at a join, and pieces must meet there
/// within a relative tolerance.
class SplicedSfd final : public SizeFrequencyDistribution {
  public:
    /// The distribution of `pieces`, valid from the first `start` to `maxDiameter` meters.
    /// @param pieces At least one piece, with strictly increasing `start`.
    /// @param tolerance Relative mismatch allowed at a join.
    /// @throws std::invalid_argument for malformed or mixed pieces, or a join that rises or breaks.
    explicit SplicedSfd(std::vector<SfdPiece> pieces,
                        double maxDiameter = std::numeric_limits<double>::infinity(),
                        double tolerance = 1e-3);

    /// The pieces in increasing `start` order.
    const std::vector<SfdPiece>& Pieces() const {
        return pieces_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double minDiameter, double maxDiameter) const override;

  private:
    std::size_t PieceAt(double diameter) const;

    std::vector<SfdPiece> pieces_;
};

}  // namespace terraingen
