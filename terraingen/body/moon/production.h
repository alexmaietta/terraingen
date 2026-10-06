#pragma once

#include "terraingen/body/moon/chronology.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/size_frequency.h"
#include "terraingen/distribution/spliced.h"

#include <memory>

namespace terraingen {

/// The cumulative density of craters on a surface of a given age: a
/// production function scaled so that N(>=1 km) follows the Neukum et al. (2001) lunar chronology,
/// N(D, T) = Phi(D) * N1(T) / Phi(1 km).
///
/// This is the population a surface would have with no erosion; small craters exceed equilibrium
/// on old surfaces.
class ProductionSurface final : public SizeFrequencyDistribution {
  public:
    /// @param production A `ProductionRate` distribution defined at 1 km.
    /// @param ageGyr Surface age in Gyr; see `NeukumLunarChronology`. The surface takes the
    /// production function's valid range and extrapolation setting.
    /// @throws std::invalid_argument if `production` is null or not a production rate.
    /// @throws std::domain_error if the age is out of range or `production` is not defined at 1 km.
    ProductionSurface(std::shared_ptr<const SizeFrequencyDistribution> production, double ageGyr);

    const SizeFrequencyDistribution& Production() const {
        return *production_;
    }

    double AgeGyr() const {
        return ageGyr_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::shared_ptr<const SizeFrequencyDistribution> production_;
    double ageGyr_;
    double scale_;
};

/// Neukum et al. (2001) lunar production function (eq. 2, table 1 "new" N(D)), valid from 10 m to
/// 300 km, as a `ProductionRate` per m^2 per Gyr at the present-day flux.
///
/// a_0 is log10(8.38e-4) = -3.076756, as in synthterrain; the paper prints -3.0876, which
/// craterstats treats as a typo for -3.0768. Combined with `NeukumLunarChronology` in a
/// `ProductionSurface` it reproduces the large-crater values of the DSNE 3.5 Ga mare table (NASA
/// SLS-SPEC-159 Revision I, Table 3.4.1.1-1).
///
/// Reference: Neukum, Ivanov and Hartmann (2001); see `NeukumLunarChronology`.
LogPolynomialSfd NeukumProductionFunction();

/// `NeukumProductionFunction` extended below 10 m as a cumulative power law of slope -2.85,
/// continuous at 10 m, and valid from 2.5 to 300000 m.
///
/// Oetting et al. (2025) measured a mean cumulative slope of -2.85 for craters of at most 10 m on
/// young impact melt deposits, fitted by maximum likelihood on unbinned data, and concluded that
/// production functions can be extended down to 2.5 m.
///
/// Reference: A. Oetting et al., "Slopes of Lunar Crater Size-Frequency Distributions on Exterior
/// Impact Melt Deposits of Young Craters", JGR Planets 130, e2024JE008589, 2025.
/// https://doi.org/10.1029/2024JE008589
SplicedSfd ExtendedNeukumProductionFunction();

}  // namespace terraingen
