#include "terraingen/albedo/maturity.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double OpticalMaturity(double r750, double r950, const OpticalMaturityOrigin& origin) {
    if (!(std::isfinite(r750) && r750 > 0.0 && std::isfinite(r950))) {
        throw std::invalid_argument("Optical maturity needs a positive 750 nm reflectance");
    }
    return std::hypot(r750 - origin.reflectance, r950 / r750 - origin.ratio);
}

double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance) {
    if (!(std::isfinite(reference) && reference > 0.0 && std::isfinite(maturity) &&
          maturity >= 0.0 && std::isfinite(originReflectance))) {
        throw std::invalid_argument(
            "Reflectance at a maturity needs a positive reference and a non-negative maturity");
    }
    const double origin = std::fmin(originReflectance, reflectance);
    return origin + (reflectance - origin) * maturity / reference;
}

}  // namespace terraingen
