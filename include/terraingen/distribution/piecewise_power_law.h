// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Size-frequency distributions made of power-law segments.
#pragma once

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/size_frequency.h"

#include <limits>
#include <vector>

namespace terraingen {

/// How the pieces of a distribution must meet at their joins.
struct SfdJoins {
    /// Allows a downward jump at a join larger than `tolerance` (for legacy specifications).
    bool allowDiscontinuities = false;
    double tolerance = 1e-3;  ///< Relative mismatch allowed at a join.
};

/// A cumulative distribution of power-law segments, each applying from its `start` to the next.
/// Segments meet within a tolerance unless `SfdJoins::allowDiscontinuities`; a downward jump puts
/// that share of the population exactly at the join diameter.
class PiecewisePowerLawSfd final : public SizeFrequencyDistribution {
  public:
    /// The distribution of `segments`, valid from the first `start` to `maxDiameter` meters.
    /// @param segments At least one segment, with strictly increasing `start`.
    /// @throws std::invalid_argument for bad segments, a rising join, or a break `joins` forbids.
    PiecewisePowerLawSfd(std::vector<PowerLawSegment> segments, SfdKind kind = SfdKind::Density,
                         double maxDiameter = std::numeric_limits<double>::infinity(),
                         SfdJoins joins = {});

    /// The segments in increasing `start` order.
    const std::vector<PowerLawSegment>& Segments() const {
        return segments_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double minDiameter, double maxDiameter) const override;

  private:
    const PowerLawSegment& SegmentAt(double diameter) const;

    std::vector<PowerLawSegment> segments_;
};

}  // namespace terraingen
