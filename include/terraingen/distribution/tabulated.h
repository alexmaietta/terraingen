// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Size-frequency distributions given as tables, interpolated in log-log space.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// A cumulative distribution given as a table of (D, N(>=D)) points, interpolated linearly
/// in log D and log N, so each interval is a power law. Extrapolation continues the end intervals'
/// slopes.
class TabulatedSfd final : public SizeFrequencyDistribution {
  public:
    /// The distribution through the table's points.
    /// @param diameters At least two strictly increasing positive diameters, in `diameterUnit` m.
    /// @param cumulative N(>=D) per `areaUnit` m^2 at each diameter; positive and not increasing.
    /// @throws std::invalid_argument for too few or mismatched points, or values out of range.
    TabulatedSfd(std::vector<double> diameters, std::vector<double> cumulative,
                 SfdKind kind = SfdKind::Density, double diameterUnit = 1.0, double areaUnit = 1.0);

    /// The table's diameters in meters.
    const std::vector<double>& Diameters() const {
        return diameters_;
    }

    /// The table's N(>=D) values per m^2 (or per m^2 per Gyr).
    const std::vector<double>& CumulativeValues() const {
        return cumulative_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    /// The power-law inverse within the interval holding `target`, ignoring `minDiameter` and
    /// `maxDiameter`; a flat interval, where N(>=D) never reaches `target`, gives its start
    /// diameter.
    double InvertCumulative(double target, double minDiameter, double maxDiameter) const override;

  private:
    std::size_t IntervalAt(double diameter) const;

    std::vector<double> diameters_;
    std::vector<double> cumulative_;
    std::vector<double> slopes_;
};

}  // namespace terraingen
