/// @file
/// @brief The Neukum et al. (2001) lunar chronology.
#pragma once

namespace terraingen {

/// @brief Oldest surface age, in Gyr, accepted by `NeukumLunarChronology`.
inline constexpr double kMaxLunarAgeGyr = 4.6;

/// @brief Neukum et al. (2001) lunar chronology, N1(T) = 5.44e-14 (e^(6.93 T) - 1) + 8.38e-4 T
/// per km^2.
/// @param ageGyr Surface age T in Gyr.
/// @return Craters at least 1 km across per m^2.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronology(double ageGyr);

/// @brief dN1/dT of `NeukumLunarChronology`: the formation rate of craters at least 1 km across.
/// @param ageGyr Time before present in Gyr.
/// @return Craters per m^2 per Gyr.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronologyRate(double ageGyr);

}  // namespace terraingen
