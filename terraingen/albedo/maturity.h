// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The reflectance at another optical maturity (Lucey et al. 2000).
#pragma once

namespace terraingen {

/// The 750 nm reflectance of the origin of Lucey et al.'s (2000) optical maturity parameter for
/// Clementine data.
inline constexpr double kOpticalMaturityOriginReflectance = 0.08;

/// The reflectance of the same material at another optical maturity, at fixed iron
/// content: x0 + (`reflectance` - x0) `maturity` / `reference`, with x0 `originReflectance`.
/// Below x0, which the relation does not cover, `reflectance` is kept.
/// @param reference Its optical maturity now; positive.
/// @param maturity The optical maturity wanted; not negative.
/// @throws std::invalid_argument unless `reference` is positive, `maturity` is not negative and
/// all are finite.
double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance = kOpticalMaturityOriginReflectance);

}  // namespace terraingen
