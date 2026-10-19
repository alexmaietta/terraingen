#include "terraingen/distribution/ivanov.h"

#include "terraingen/distribution/exponential_chronology.h"

namespace terraingen {

namespace {

constexpr detail::ExponentialChronology kIvanov{
    2.68e-14, 6.93, 4.13e-4, kMaxMarsAgeGyr, "Martian surface age must be between 0 and 4.6 Gyr"};

}  // namespace

double IvanovMarsChronology(double ageGyr) {
    return kIvanov.Count(ageGyr);
}

double IvanovMarsChronologyRate(double ageGyr) {
    return kIvanov.Rate(ageGyr);
}

LogPolynomialSfd IvanovMarsProductionFunction() {
    return LogPolynomialSfd({-3.383677, -3.197453, 1.256814, 7.915374e-1, -4.860814e-1,
                             -3.630098e-1, 1.015683e-1, 6.755923e-2, -1.180639e-2, -4.753462e-3,
                             6.232845e-4, 5.805492e-5},
                            DiameterRange{15.0, 362000.0}, SfdKind::ProductionRate, 1000.0, 1e6);
}

}  // namespace terraingen
