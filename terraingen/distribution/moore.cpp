// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/moore.h"

#include <cmath>
#include <limits>

namespace terraingen {

PiecewisePowerLawSfd MooreDistribution(MooreTerrain terrain) {
    constexpr double kRoundingTolerance = 5e-3;
    constexpr double kUnbounded = std::numeric_limits<double>::infinity();
    switch (terrain) {
    case MooreTerrain::RoughMare:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0}, {100.0, 10.0, 3.0}}, SfdKind::Density,
                                    kUnbounded, SfdJoins{false, kRoundingTolerance});
    case MooreTerrain::SmoothMare:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0},
                                     {40.0, std::pow(10.0, 0.602), 3.0},
                                     {100.0, std::pow(10.0, -2.038), 1.68},
                                     {200.0, 10.0, 3.0}},
                                    SfdKind::Density, kUnbounded,
                                    SfdJoins{false, kRoundingTolerance});
    case MooreTerrain::Uplands:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0}, {1000.0, 100.0, 3.0}}, SfdKind::Density,
                                    kUnbounded, SfdJoins{false, kRoundingTolerance});
    default:
        return PiecewisePowerLawSfd({{0.0, 0.1, 2.0}}, SfdKind::Density, 10000.0);
    }
}

}  // namespace terraingen
