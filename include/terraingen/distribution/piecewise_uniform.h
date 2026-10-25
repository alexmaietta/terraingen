// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Size-frequency distributions made of bands with diameters spread uniformly.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <vector>

namespace terraingen {

/// One band of a piecewise uniform distribution: `density` features with diameters spread
/// uniformly over [`min`, `max`) meters.
struct UniformBand {
    double min = 1.0;       ///< Smallest diameter, meters; positive.
    double max = 2.0;       ///< Largest diameter, meters; above `min`.
    double density = 0.01;  ///< Features per m^2 (or per m^2 per Gyr), or a relative weight; >= 0.

    /// Memberwise equality.
    bool operator==(const UniformBand& other) const {
        return min == other.min && max == other.max && density == other.density;
    }
    bool operator!=(const UniformBand& other) const {
        return !(*this == other);
    }
};

/// A distribution made of diameter bands, each holding a number of features per m^2 with
/// diameters uniform across it, for scenes that set counts by size class instead of following a
/// power law.
///
/// N(>=D) falls linearly across each band and is flat between bands; overlapping bands add. No
/// features are smaller than the smallest band or larger than the largest, so the valid range is
/// every diameter up to the largest band's `max`, and extrapolation beyond it, where N(>=D) is 0,
/// is allowed from the start.
class PiecewiseUniformSfd final : public SizeFrequencyDistribution {
  public:
    /// The distribution of `bands`.
    /// @param total When positive, the features per m^2 (or per m^2 per Gyr) of all bands
    /// together, shared in proportion to each band's `density`; 0 takes the densities as given.
    /// @throws std::invalid_argument for no bands, a band whose diameters are not positive and
    /// finite with `max` above `min`, a negative or non-finite density or total, or no features.
    PiecewiseUniformSfd(std::vector<UniformBand> bands, SfdKind kind = SfdKind::Density,
                        double total = 0.0);

    /// The bands, with densities per m^2 (or per m^2 per Gyr) after any `total` is applied.
    const std::vector<UniformBand>& Bands() const {
        return bands_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::vector<UniformBand> bands_;
    std::vector<double> edges_;
    std::vector<double> edgeCumulative_;
};

}  // namespace terraingen
