// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/moore.h"

#include <cmath>

namespace terraingen {

PiecewisePowerLawSfd MooreDistribution(MooreTerrain terrain) {
    constexpr double kRoundingTolerance = 5e-3;
    switch (terrain) {
    case MooreTerrain::RoughMare:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}}, SfdKind::Density,
                                    DiameterRange{}, false, kRoundingTolerance);
    case MooreTerrain::SmoothMare:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0},
                                     {40.0, std::pow(10.0, 0.602), 3.0},
                                     {100.0, std::pow(10.0, -2.038), 1.68},
                                     {200.0, 10.0, 3.0}},
                                    SfdKind::Density, DiameterRange{}, false, kRoundingTolerance);
    case MooreTerrain::Uplands:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0}, {1000.0, 100.0, 3.0}}, SfdKind::Density,
                                    DiameterRange{}, false, kRoundingTolerance);
    default:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0}}, SfdKind::Density,
                                    DiameterRange{0.0, 10000.0});
    }
}

}  // namespace terraingen
