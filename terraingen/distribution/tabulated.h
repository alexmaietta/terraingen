#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// A cumulative distribution given as a table of (D, N(>=D)) points, interpolated linearly in log D
/// and log N, so each interval is a power law. Extrapolation continues the end intervals' slopes.
class TabulatedSfd final : public SizeFrequencyDistribution {
  public:
    /// @param diameters At least two strictly increasing positive diameters, in `diameterUnit`
    /// metres.
    /// @param cumulative N(>=D) at each diameter, positive, finite and not increasing, per
    /// `areaUnit` m^2.
    /// @throws std::invalid_argument for malformed tables or units.
    TabulatedSfd(std::vector<double> diameters, std::vector<double> cumulative,
                 SfdKind kind = SfdKind::Density, double diameterUnit = 1.0, double areaUnit = 1.0);

    /// Table diameters in metres.
    const std::vector<double>& Diameters() const {
        return diameters_;
    }

    /// Table values per m^2 (or per m^2 per Gyr).
    const std::vector<double>& CumulativeValues() const {
        return cumulative_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::size_t IntervalAt(double diameter) const;

    std::vector<double> diameters_;
    std::vector<double> cumulative_;
    std::vector<double> slopes_;
};

}  // namespace terraingen
