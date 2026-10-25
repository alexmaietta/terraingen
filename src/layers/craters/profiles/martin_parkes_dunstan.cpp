// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/profiles/martin_parkes_dunstan.h"

#include <stdexcept>

namespace terraingen {

double MartinParkesDunstanProfile::Height(double distance, double diameter,
                                          const CraterState& state) const {
    const CraterMorphometry& current = state.current;
    if (!(current.depthRatio > 0.0 && current.rimHeightRatio >= 0.0 &&
          current.rimHeightRatio <= state.fresh.rimHeightRatio)) {
        throw std::invalid_argument(
            "Crater state needs a positive depth and 0 <= rim height <= fresh rim height");
    }
    return static_cast<double>(SimpleProfileHeight(kernel_, static_cast<float>(distance),
                                                   static_cast<float>(diameter),
                                                   KernelStateRecord(diameter, state)));
}

}  // namespace terraingen
