#pragma once

#include "terraingen/distributions/size_frequency.h"

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

/// Trask (1966) lunar equilibrium, N(>=D) = 10^-1.1 D^-2 (the same in km/km^2 and m/m^2). The DSNE
/// uses it as the upper limit on the density of craters smaller than about 100-200 m on any
/// surface.
///
/// References: N. J. Trask, in Ranger VIII and IX Part II, JPL Technical Report 32-800, 1966; NASA
/// SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.
PowerLaw TraskEquilibrium();

/// Hartmann (1984) lunar equilibrium, N(>=D) = 10^-1.14 D_km^-1.83 per km^2, as given by Fassett et
/// al. (2022, eq. 3b).
///
/// Reference: C. I. Fassett et al., "Topographic Diffusion Revisited", JGR Planets 127, 2022.
/// https://doi.org/10.1029/2022JE007510
PowerLaw HartmannEquilibrium();

/// Moore's terrain-specific cumulative distributions for Lunar Roving Vehicle trafficability (USGS,
/// "Section C"), N per m^2 with D in metres.
enum class MooreTerrain {
    RoughMare,        ///< 10^-1 D^-2 below 100 m, 10 D^-3 above.
    SmoothMare,       ///< 10^-1 D^-2 below 40 m, then 10^0.602 D^-3, 10^-2.038 D^-1.68, 10 D^-3.
    Uplands,          ///< 10^-1 D^-2 below 1 km, 10^2 D^-3 above.
    HeavilyCratered,  ///< 10^-1 D^-2 up to 10 km.
};

/// Moore's distribution for `terrain` (H. J. Moore, USGS Section C, c. 1970). The published
/// coefficients are rounded, so segments agree at their joins only to within 0.2%; the presets
/// accept joins within 0.5%.
PiecewisePowerLaw MooreDistribution(MooreTerrain terrain);

/// The VIPER environmental specification (VIPER-MSE-SPEC-001, 2021) as used by synthterrain:
/// N = 29174 D^-1.92 per km^2 for D <= 80 m and 156228 D^-2.389 above (D in m). Kept for comparison
/// with synthterrain; its segments disagree by a factor of 1.46 at 80 m, and Allan et al. (2017)
/// found it predicts too many craters against a NAC image.
PiecewisePowerLaw ViperSpecification();

/// Oldest surface age, in Gyr, accepted by `NeukumLunarChronology`.
inline constexpr double kMaxLunarAgeGyr = 4.6;

/// Neukum et al. (2001) lunar chronology: cumulative density of craters at least 1 km across on a
/// surface `ageGyr` old, N1(T) = 5.44e-14 (e^(6.93 T) - 1) + 8.38e-4 T per km^2, returned per m^2.
/// The linear term is the present-day rate, which equals 10^a_0 of `NeukumProductionFunction`.
///
/// Reference: G. Neukum, B. A. Ivanov and W. K. Hartmann, "Cratering Records in the Inner Solar
/// System in Relation to the Lunar Reference System", Space Science Reviews 96:55-86, 2001, eq. 5.
/// https://doi.org/10.1023/A:1011989004263
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronology(double ageGyr);

/// dN1/dT of `NeukumLunarChronology`: the formation rate of craters at least 1 km across `ageGyr`
/// ago, per m^2 per Gyr.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronologyRate(double ageGyr);

/// Neukum et al. (2001) lunar production function (eq. 2, table 1 "new" N(D)), valid from 10 m to
/// 300 km, as a `ProductionRate` per m^2 per Gyr at the present-day flux.
///
/// a_0 is log10(8.38e-4) = -3.076756, as in synthterrain; the paper prints -3.0876, which
/// craterstats treats as a typo for -3.0768. Combined with `NeukumLunarChronology` in a
/// `ProductionSurface` it reproduces the large-crater values of the DSNE 3.5 Ga mare table (NASA
/// SLS-SPEC-159 Revision I, Table 3.4.1.1-1).
///
/// Reference: Neukum, Ivanov and Hartmann (2001); see `NeukumLunarChronology`.
LogPolynomial NeukumProductionFunction();

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
