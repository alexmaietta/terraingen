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

/// A cumulative distribution made of power-law segments in increasing diameter order, each
/// applying from its `start` up to the next segment's start, valid from the first start.
///
/// Segments must meet at their joins within a relative tolerance unless
/// `SfdJoins::allowDiscontinuities` is set. A downward jump at a join gives a finite fraction of
/// the population exactly the join diameter, which `SampleDiameter` returns for that fraction.
class PiecewisePowerLawSfd final : public SizeFrequencyDistribution {
  public:
    /// The distribution made of `segments`, valid from the first one's `start` to `maxDiameter`.
    /// @param segments At least one segment, with strictly increasing `start`.
    /// @param maxDiameter The largest valid diameter, in meters.
    /// @throws std::invalid_argument for no segments, a coefficient or exponent that is not
    /// positive and finite, starts that do not increase below `maxDiameter`, a join that rises,
    /// or a discontinuity larger than the tolerance that `joins` does not allow.
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
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    const PowerLawSegment& SegmentAt(double diameter) const;

    std::vector<PowerLawSegment> segments_;
};

}  // namespace terraingen
