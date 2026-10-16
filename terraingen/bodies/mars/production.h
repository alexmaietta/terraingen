#pragma once

#include "terraingen/bodies/surface_model.h"
#include "terraingen/distribution/log_polynomial.h"
#include "terraingen/distribution/production_surface.h"

namespace terraingen {

/// Oldest surface age, in Gyr, accepted by `IvanovMarsChronology`.
inline constexpr double kMaxMarsAgeGyr = 4.6;

/// Ivanov's (2001) Martian chronology: the cumulative density of craters at least 1 km across on a
/// surface `ageGyr` old, N1(T) = 2.68e-14 (e^(6.93 T) - 1) + 4.13e-4 T per km^2, returned per m^2.
/// The linear term is the present-day rate, 10^a_0 of `IvanovMarsProductionFunction`.
///
/// Reference: B. A. Ivanov, "Mars/Moon cratering rate ratio estimates", Space Science Reviews
/// 96:87-104, 2001 (coefficients as in craterstats, G. Michael,
/// https://github.com/ggmichael/craterstats).
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxMarsAgeGyr`.
double IvanovMarsChronology(double ageGyr);

/// dN1/dT of `IvanovMarsChronology`, per m^2 per Gyr.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxMarsAgeGyr`.
double IvanovMarsChronologyRate(double ageGyr);

/// Ivanov's (2001) Martian production function, derived from the lunar one of Neukum et al. (2001),
/// as a degree-11 polynomial in log D, valid from 15 m to 362 km, as a `ProductionRate` per m^2 per
/// Gyr at the present-day flux.
///
/// Reference: Ivanov (2001); see `IvanovMarsChronology`.
LogPolynomialSfd IvanovMarsProductionFunction();

/// A Martian surface `ageGyr` old with no erosion: `IvanovMarsProductionFunction` scaled by
/// `IvanovMarsChronology`, valid from 15 m to 362 km, with crater ages uniform over the surface's
/// age (`UniformAges`), which suits surfaces younger than about 3 Gyr, when the flux was nearly
/// steady. No equilibrium cap is applied.
/// @throws std::domain_error if `ageGyr` is outside (0, `kMaxMarsAgeGyr`].
SurfaceModel MarsSurface(double ageGyr);

}  // namespace terraingen
