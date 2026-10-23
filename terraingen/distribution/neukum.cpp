// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/neukum.h"

#include "terraingen/distribution/power_law.h"
#include "terraingen/distribution/production_surface.h"

#include <cmath>
#include <memory>
#include <utility>

namespace terraingen {

namespace {

constexpr detail::ExponentialChronology kNeukum{5.44e-14, 6.93, 8.38e-4, kMaxLunarAgeGyr,
                                                "Lunar surface age must be between 0 and 4.6 Gyr"};

}  // namespace

double NeukumLunarChronology(double ageGyr) {
    return kNeukum.Count(ageGyr);
}

double NeukumLunarChronologyRate(double ageGyr) {
    return kNeukum.Rate(ageGyr);
}

LogPolynomialSfd NeukumProductionFunction() {
    return LogPolynomialSfd({-3.076756, -3.557528, 0.781027, 1.021521, -0.156012, -0.444058,
                             0.019977, 0.086850, -0.005874, -0.006809, 8.25e-4, 5.54e-5},
                            DiameterRange{10.0, 300000.0}, SfdKind::ProductionRate, 1000.0, 1e6);
}

SplicedSfd ExtendedNeukumProductionFunction() {
    constexpr double kJoin = 10.0;
    constexpr double kSlope = 2.85;
    auto npf = std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
    const double coefficient = npf->Cumulative(kJoin) * std::pow(kJoin, kSlope);
    auto smallCraters = std::make_shared<PowerLawSfd>(coefficient, kSlope, SfdKind::ProductionRate);
    return SplicedSfd({{2.5, std::move(smallCraters)}, {kJoin, std::move(npf)}},
                      DiameterRange{2.5, 300000.0});
}

}  // namespace terraingen
