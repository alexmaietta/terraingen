#pragma once

#include "terraingen/distribution/size_frequency.h"

namespace terraingen {

/// Boulder sizes after Golombek and Rapp (1997): the cumulative fractional area covered by boulders
/// at least D metres across is F(D) = k exp(-q D), with q = `qa` + `qb` / k per metre, where k, the
/// total fractional area covered by boulders, sets how rocky the ground is (`coverage`).
///
/// As a number density, -dN/dD = k q exp(-q D) / (pi D^2 / 4) per m^2 per metre, so
/// N(>=D) = (4 k q / pi) E2(q D) / D per m^2, with E2 the exponential integral of order 2. Lunar
/// surfaces have k of about 0.005 to 0.03 away from fresh craters (0.02 is Chrono::Planet's lunar
/// default); Mars landing sites range up to about 0.2.
///
/// References:
/// - M. P. Golombek and D. Rapp, "Size-frequency distributions of rocks on Mars and Earth analog
///   sites: Implications for future landed missions", Journal of Geophysical Research 102(E2):
///   4117-4129, 1997. https://doi.org/10.1029/96JE03319
/// - M. Golombek et al., "Rock size-frequency distributions on Mars and implications for Mars
///   Exploration Rover landing safety and operations", Journal of Geophysical Research 108(E12),
///   2003. https://doi.org/10.1029/2002JE002035
class GolombekRappSfd final : public SizeFrequencyDistribution {
  public:
    /// @throws std::invalid_argument unless 0 < `coverage` < 1 and `qa`, `qb` are finite and not
    /// negative with a positive q, and the range is valid.
    explicit GolombekRappSfd(double coverage, double qa = 1.79, double qb = 0.152,
                             DiameterRange range = {});

    double Coverage() const {
        return coverage_;
    }

    /// q = qa + qb / k, per metre.
    double Q() const {
        return q_;
    }

    /// F(D) = k exp(-q D): the fractional area covered by boulders at least `diameter` metres
    /// across.
    double FractionalArea(double diameter) const;

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    double coverage_;
    double q_;
};

/// The exponential integral E_n(x) = integral from 1 to infinity of exp(-x t) / t^n dt, for n >= 0
/// and x > 0 (or x >= 0 when n > 1).
///
/// Reference: W. H. Press, S. A. Teukolsky, W. T. Vetterling and B. P. Flannery, "Numerical
/// Recipes", 3rd ed., Cambridge University Press, 2007, section 6.3.
/// @throws std::domain_error outside that domain.
double ExponentialIntegral(int n, double x);

}  // namespace terraingen
