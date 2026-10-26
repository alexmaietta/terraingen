// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The reflectance at another optical maturity (Lucey et al. 2000).
#pragma once

namespace terraingen {

/// The 750 nm reflectance of the origin of Lucey et al.'s (2000) optical maturity parameter for
/// Clementine data.
inline constexpr double kOpticalMaturityOriginReflectance = 0.08;

/// The reflectance at another optical maturity, scaling `reflectance` - `originReflectance` by
/// `maturity` / `reference`; reflectances below the origin are kept.
/// @param reference Its optical maturity now; positive.
/// @param maturity The optical maturity wanted; not negative.
/// @throws std::invalid_argument for a non-positive reference, negative maturity or NaN input.
double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance = kOpticalMaturityOriginReflectance);

}  // namespace terraingen
