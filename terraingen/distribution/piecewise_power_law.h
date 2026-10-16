/// @file
/// @brief Size-frequency distributions made of power-law segments.
#pragma once

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/size_frequency.h"

#include <vector>

namespace terraingen {

/// @brief A cumulative distribution made of power-law segments in increasing diameter order, each
/// applying from its `start` up to the next segment's start.
///
/// Segments must meet at their joins within a relative tolerance unless `allowDiscontinuities` is
/// set (for legacy specifications). A downward jump at a join gives a finite fraction of the
/// population exactly the join diameter, which `SampleDiameter` returns for that fraction.
class PiecewisePowerLawSfd final : public SizeFrequencyDistribution {
  public:
    /// @brief The distribution made of `segments`.
    /// @param segments At least one segment, with strictly increasing `start` and the first `start`
    /// equal to `range.min`.
    /// @param kind What the values measure.
    /// @param range The valid range, in metres.
    /// @param allowDiscontinuities Whether downward jumps beyond `tolerance` are allowed.
    /// @param tolerance Relative mismatch allowed at a join.
    /// @throws std::invalid_argument for no segments, a coefficient or exponent that is not
    /// positive and finite, starts that do not increase within the range, a join that rises, or a
    /// discontinuity larger than `tolerance` without `allowDiscontinuities`.
    PiecewisePowerLawSfd(std::vector<PowerLawSegment> segments, SfdKind kind = SfdKind::Density,
                         DiameterRange range = {}, bool allowDiscontinuities = false,
                         double tolerance = 1e-3);

    /// @brief The segments.
    /// @return The segments in increasing `start` order.
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
