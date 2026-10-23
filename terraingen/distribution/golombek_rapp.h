/// @file
/// @brief Boulder sizes after Golombek and Rapp (1997), from the fraction of ground boulders cover.
#pragma once

#include "terraingen/distribution/size_frequency.h"

namespace terraingen {

/// Boulder densities whose cumulative fractional area is F(D) = k exp(-q D), with q = `qa` +
/// `qb` / k per meter and k the fraction of ground the boulders cover (`coverage`); N(>=D) =
/// (4 k q / pi) E2(q D) / D per m^2.
class GolombekRappSfd final : public SizeFrequencyDistribution {
  public:
    /// The distribution for coverage k.
    /// @param coverage k, the fractional area covered by boulders, in (0, 1).
    /// @param qa The constant part of q, per meter; finite and not negative.
    /// @param qb The part of q that scales with 1 / k, per meter; finite and not negative.
    /// @param range Diameters in meters where it is valid.
    /// @throws std::invalid_argument for a coverage outside (0, 1), a negative or non-finite `qa`
    /// or `qb`, a q that is not positive, or an invalid range.
    explicit GolombekRappSfd(double coverage, double qa = 1.79, double qb = 0.152,
                             DiameterRange range = {});

    /// F(D) = k exp(-q D), the fractional area covered by boulders at least `diameter` meters
    /// across.
    double FractionalArea(double diameter) const;

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    double coverage_;
    double q_;
};

/// The Golombek-Rapp density -dN/dD per m^2 per meter of diameter at `diameter` meters, for
/// coverage k and q = qa + qb / k per meter.
double GolombekRappIncremental(double diameter, double coverage, double q);

}  // namespace terraingen
