#include "terraingen/bodies/mars/production.h"

#include "terraingen/layers/craters/ages/uniform.h"

#include <cmath>
#include <memory>
#include <stdexcept>

namespace terraingen {

namespace {

void CheckAge(double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= kMaxMarsAgeGyr)) {
        throw std::domain_error("Martian surface age must be between 0 and 4.6 Gyr");
    }
}

}  // namespace

double IvanovMarsChronology(double ageGyr) {
    CheckAge(ageGyr);
    return (2.68e-14 * std::expm1(6.93 * ageGyr) + 4.13e-4 * ageGyr) / 1e6;
}

double IvanovMarsChronologyRate(double ageGyr) {
    CheckAge(ageGyr);
    return (2.68e-14 * 6.93 * std::exp(6.93 * ageGyr) + 4.13e-4) / 1e6;
}

LogPolynomialSfd IvanovMarsProductionFunction() {
    return LogPolynomialSfd({-3.383677, -3.197453, 1.256814, 7.915374e-1, -4.860814e-1,
                             -3.630098e-1, 1.015683e-1, 6.755923e-2, -1.180639e-2, -4.753462e-3,
                             6.232845e-4, 5.805492e-5},
                            DiameterRange{15.0, 362000.0}, SfdKind::ProductionRate, 1000.0, 1e6);
}

SurfaceModel MarsSurface(double ageGyr) {
    if (!(ageGyr > 0.0)) {
        throw std::domain_error("A Martian surface must be older than 0 Gyr");
    }
    CheckAge(ageGyr);
    auto production = std::make_shared<LogPolynomialSfd>(IvanovMarsProductionFunction());
    return {
        std::make_shared<ProductionSurface>(std::move(production), ageGyr, IvanovMarsChronology),
        std::make_shared<UniformAges>(ageGyr)};
}

}  // namespace terraingen
