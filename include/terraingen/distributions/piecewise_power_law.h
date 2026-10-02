#pragma once

#include "terraingen/distributions/size_frequency.h"

#include <vector>

namespace terraingen {

/// One segment of a `PiecewisePowerLawSfd`: N(>=D) = `coefficient` * D^(-`exponent`) from `start`
/// metres up to the next segment's start.
struct PowerLawSegment {
    double start = 0.0;
    double coefficient = 1.0;
    double exponent = 2.0;
};

/// A cumulative distribution made of power-law segments in increasing diameter order.
///
/// Segments must meet at their joins within a relative tolerance unless `allowDiscontinuities` is
/// set, which exists for legacy specifications. A downward jump at a join means a finite fraction
/// of the population has exactly the join diameter, and `SampleDiameter` returns it for that
/// fraction.
class PiecewisePowerLawSfd final : public SizeFrequencyDistribution {
  public:
    /// @param segments At least one segment, with strictly increasing `start` and the first `start`
    /// equal to `range.min`.
    /// @throws std::invalid_argument for malformed segments, a join that rises, or a discontinuity
    /// larger than `tolerance` without `allowDiscontinuities`.
    PiecewisePowerLawSfd(std::vector<PowerLawSegment> segments, SfdKind kind = SfdKind::Density,
                         DiameterRange range = {}, bool allowDiscontinuities = false,
                         double tolerance = 1e-3);

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
