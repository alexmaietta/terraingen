#pragma once

namespace terraingen {

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

}  // namespace terraingen
