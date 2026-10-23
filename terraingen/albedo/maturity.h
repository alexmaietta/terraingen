/// @file
/// @brief Lucey et al.'s (2000) optical maturity parameter and the reflectance it implies.
#pragma once

namespace terraingen {

/// The origin of Lucey et al.'s (2000) optical maturity parameter in the plane of 750 nm
/// reflectance and 950/750 nm ratio; the default is their origin for Clementine data.
struct OpticalMaturityOrigin {
    double reflectance = 0.08;  ///< 750 nm reflectance x0.
    double ratio = 1.19;        ///< 950/750 nm reflectance ratio y0.

    constexpr bool operator==(const OpticalMaturityOrigin& other) const {
        return reflectance == other.reflectance && ratio == other.ratio;
    }
    constexpr bool operator!=(const OpticalMaturityOrigin& other) const {
        return !(*this == other);
    }
};

/// The reflectance of the same material at another optical maturity, at fixed iron
/// content: x0 + (`reflectance` - x0) `maturity` / `reference`, with x0 `originReflectance`.
/// Below x0, which the relation does not cover, `reflectance` is kept.
/// @param reference Its optical maturity now; positive.
/// @param maturity The optical maturity wanted; not negative.
/// @throws std::invalid_argument unless `reference` is positive, `maturity` is not negative and
/// all are finite.
double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance = OpticalMaturityOrigin{}.reflectance);

}  // namespace terraingen
