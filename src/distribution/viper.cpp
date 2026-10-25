// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/viper.h"

#include <limits>

namespace terraingen {

PiecewisePowerLawSfd ViperSpecification() {
    return PiecewisePowerLawSfd({{0.0, 29174.0 / 1e6, 1.92}, {80.0, 156228.0 / 1e6, 2.389}},
                                SfdKind::Density, std::numeric_limits<double>::infinity(),
                                SfdJoins{true});
}

}  // namespace terraingen
