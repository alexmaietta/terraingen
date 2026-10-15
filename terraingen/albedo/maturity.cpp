#include "terraingen/albedo/maturity.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double OpticalMaturity(double r750, double r950, const OpticalMaturityOrigin& origin) {
    if (!(IsPositive(r750) && std::isfinite(r950))) {
        throw std::invalid_argument("Optical maturity needs a positive 750 nm reflectance");
    }
    return std::hypot(r750 - origin.reflectance, r950 / r750 - origin.ratio);
}

double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance) {
    if (!(IsPositive(reference) && IsNonNegative(maturity) && std::isfinite(originReflectance))) {
        throw std::invalid_argument(
            "Reflectance at a maturity needs a positive reference and a non-negative maturity");
    }
    const double origin = std::fmin(originReflectance, reflectance);
    return origin + (reflectance - origin) * maturity / reference;
}

}  // namespace terraingen
