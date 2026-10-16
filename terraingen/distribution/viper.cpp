/// @file
/// @brief The VIPER environmental specification's crater distribution.
#include "terraingen/distribution/viper.h"

namespace terraingen {

PiecewisePowerLawSfd ViperSpecification() {
    return PiecewisePowerLawSfd({{0.0, 29174.0 / 1e6, 1.92}, {80.0, 156228.0 / 1e6, 2.389}},
                                SfdKind::Density, DiameterRange{}, true);
}

}  // namespace terraingen
