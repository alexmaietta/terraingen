/// @file
/// @brief Lucey et al.'s (2000) optical maturity parameter and the reflectance it implies.
#pragma once

namespace terraingen {

/// @brief The origin of Lucey et al.'s (2000) optical maturity parameter in the plane of 750 nm
/// reflectance and 950/750 nm ratio; the default is their origin for Clementine data.
struct OpticalMaturityOrigin {
    double reflectance = 0.08;  ///< 750 nm reflectance x0.
    double ratio = 1.19;        ///< 950/750 nm reflectance ratio y0.

    bool operator==(const OpticalMaturityOrigin&) const = default;
};

/// @brief Lucey et al.'s (2000) origin for laboratory spectra, used by their Table 1 of returned
/// soils.
inline constexpr OpticalMaturityOrigin kLaboratoryMaturityOrigin{0.01, 1.26};

/// @brief Lucey et al.'s (2000, eq. 1) optical maturity parameter (OMAT): the distance from
/// `origin` in the plane of reflectance and ratio, sqrt((r750 - x0)^2 + (r950/r750 - y0)^2).
/// Higher is less mature.
/// @param r750 Reflectance at 750 nm.
/// @param r950 Reflectance at 950 nm.
/// @param origin The parameter's origin.
/// @return The optical maturity parameter.
/// @throws std::invalid_argument unless `r750` is positive and both are finite.
double OpticalMaturity(double r750, double r950, const OpticalMaturityOrigin& origin = {});

/// @brief The reflectance of the same material at another optical maturity, at fixed iron
/// content: x0 + (`reflectance` - x0) `maturity` / `reference`, with x0 `originReflectance`.
/// Below x0, which the relation does not cover, `reflectance` is kept.
/// @param reflectance The reflectance now.
/// @param reference Its optical maturity now; positive.
/// @param maturity The optical maturity wanted; not negative.
/// @param originReflectance The reflectance of the maturity origin.
/// @return The reflectance at `maturity`.
/// @throws std::invalid_argument unless `reference` is positive, `maturity` is not negative and
/// all are finite.
double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance = OpticalMaturityOrigin{}.reflectance);

}  // namespace terraingen
