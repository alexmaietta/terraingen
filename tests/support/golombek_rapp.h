// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `GolombekRappBoulders`, boulder layer options sized by Golombek and Rapp's model, for
/// tests of that model.
#pragma once

#include "terraingen/layers/boulders/boulder_options.h"

namespace terraingen::testing {

/// Default boulder layer options with Golombek-Rapp sizes of `coverage` between craters and
/// around them, up to 4 m, in place of the default SP-8023 blocks.
inline BoulderLayerOptions GolombekRappBoulders() {
    BoulderLayerOptions options;
    options.sizes.distribution = nullptr;
    options.sizes.maxDiameter = 4.0;
    options.ejecta.distribution = nullptr;
    return options;
}

}  // namespace terraingen::testing
