// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The simple-crater profile of Martin, Parkes and Dunstan (2014).
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"

namespace terraingen {

/// Simple-crater profile of Martin, Parkes and Dunstan (2014): bowl, inner rim, outer rim
/// and ejecta polynomials joined continuously; degradation rounds the rim.
class MartinParkesDunstanProfile final : public CraterProfile {
  public:
    /// Reads the fresh and current rim heights and current depth of `state`;
    /// `MartinParkesDunstanHeight` in single precision.
    /// @throws std::invalid_argument if the state's depth is not positive or its current rim height
    /// is negative or exceeds the fresh rim height.
    double Height(double distance, double diameter, const CraterState& state) const override;

    double ReachRadii() const override {
        return 2.0;
    }

    /// The profile as the crater kernel evaluates it.
    const CraterProfileKernel& Kernel() const {
        return kernel_;
    }

  private:
    CraterProfileKernel kernel_ = [] {
        CraterProfileKernel kernel;
        kernel.simple = CraterProfileKind::MartinParkesDunstan;
        return kernel;
    }();
};

}  // namespace terraingen
