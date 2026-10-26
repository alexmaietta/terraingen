// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Craters formed on slopes: floors offset downslope and raised rims lowered upslope.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/geometry.h"

#include <cstdint>
#include <optional>

namespace terraingen {

/// How craters formed on a slope become asymmetric along it and wear faster, after Plescia et al.
/// (2019, https://www.hou.usra.edu/meetings/lpsc2019/pdf/2984.pdf); needs the `Plane` datum.
struct CraterSlopeResponse {
    /// Shapes craters asymmetrically along the slope; off keeps their shape on any slope. The wear
    /// that `criticalGradient` gives applies either way.
    bool asymmetric = false;
    /// Varies the floor offsets; unset follows the crater layer's `seed`, and is 0 outside a layer.
    std::optional<std::uint32_t> seed{};
    /// How far the deepest point lies downslope of the center, in diameters, drawn per crater;
    /// below 0.23. The default is digitized from Plescia et al. (2019, Fig. 3).
    ValueDistribution floorOffset{ValueDistribution::Kind::Uniform, 0.045, 0.194};
    /// Share of the raised rim and ejecta kept straight upslope, in [0, 1]; the rest returns as
    /// the squared cosine of the angle from upslope falls, all of it across the slope and
    /// downslope.
    double upslopeRim = 0.0;
    /// Slope (degrees) at which the asymmetry is complete, rising from none on level ground along
    /// a smoothstep; 5 degrees, the "few degrees" of Plescia et al. (2019).
    double fullSlopeDegrees = 5.0;
    /// The critical gradient Sc (rise over run): a crater on a regional gradient S wears
    /// 1 / (1 - (S / Sc)^2) times as fast (Roering et al. 1999,
    /// https://doi.org/10.1029/1998WR900090); unset wears craters alike on any slope. Applies
    /// whether or not the craters are `asymmetric`.
    std::optional<double> criticalGradient{};
    /// The fastest a slope makes craters wear, over level ground; at least 1. It caps the rate as
    /// S nears Sc, where the form grows without bound; a numerical limit, not a measured one.
    double maxWearRate = 10.0;

    /// Memberwise equality.
    bool operator==(const CraterSlopeResponse& other) const {
        return asymmetric == other.asymmetric && seed == other.seed &&
               floorOffset == other.floorOffset && upslopeRim == other.upslopeRim &&
               fullSlopeDegrees == other.fullSlopeDegrees &&
               criticalGradient == other.criticalGradient && maxWearRate == other.maxWearRate;
    }
    bool operator!=(const CraterSlopeResponse& other) const {
        return !(*this == other);
    }
};

/// How many times as fast a crater wears on ground rising `east` and `north` meters per meter,
/// under `response`'s critical gradient; 1 without one.
double SlopeWearRate(const CraterSlopeResponse& response, double east, double north);

/// Checks slope response settings.
/// @throws std::invalid_argument for an invalid floor offset distribution or one outside
/// [0, 0.23), an `upslopeRim` outside [0, 1], or a `fullSlope` that is not positive and finite.
void ValidateCraterSlopeResponse(const CraterSlopeResponse& response);

/// One crater's asymmetry on its slope.
struct CraterSlopeShape {
    Vec2d shift{};    ///< Where the deepest point moves, east and north of the center (m).
    Vec2d upslope{};  ///< Upslope unit vector times the share of raised relief removed there.
};

/// The asymmetry `response` gives `crater` on ground rising `east` and `north` meters per meter,
/// its floor offset drawn from its ID; level when disabled or on level ground.
CraterSlopeShape SlopeShape(const Crater& crater, double east, double north,
                            const CraterSlopeResponse& response);

}  // namespace terraingen
