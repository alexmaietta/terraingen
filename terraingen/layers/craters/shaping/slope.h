/// @file
/// @brief Craters formed on slopes: floors offset downslope and raised rims lowered upslope.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/geometry.h"

#include <cstdint>
#include <optional>

namespace terraingen {

/// How craters formed on a slope become asymmetric along it and wear faster.
///
/// Lunar craters on slopes have their deepest point offset downslope, an upslope margin that is
/// often only a break in slope with little raised rim, and ejecta thrown mostly downslope; the
/// asymmetry shows from slopes of a few degrees and does not grow on steeper ones, and they are
/// on average no longer downslope than across (Plescia et al. 2019,
/// https://www.hou.usra.edu/meetings/lpsc2019/pdf/2984.pdf).
///
/// The slope is that of the ground beneath the layer, its source and earlier layers, across each
/// crater's datum footprint; the craters of the layer itself are left out, so each crater's shape
/// depends on no other and the relief its datum is fitted to is the relief applied. A crater on the
/// wall of a larger one responds to it when the larger craters are in an earlier layer. Needs the
/// `Plane` datum.
struct CraterSlopeResponse {
    bool enabled = false;    ///< Off: craters keep their shape on any slope.
    std::uint32_t seed = 0;  ///< Varies the floor offsets.
    /// How far the deepest point lies downslope of the centre, in diameters, drawn per crater;
    /// below 0.23. The default spans the interquartile range of the upslope wall width over the
    /// downslope diameter, 0.55 to 0.69 with a median of 0.61, digitized from Plescia et al.
    /// (2019, Fig. 3).
    ValueDistribution floorOffset{ValueDistribution::Kind::Uniform, 0.045, 0.194};
    /// Share of the raised rim and ejecta kept straight upslope, in [0, 1]; the rest returns as
    /// the squared cosine of the angle from upslope falls, all of it across the slope and
    /// downslope.
    double upslopeRim = 0.0;
    /// Slope (radians) at which the asymmetry is complete, rising from none on level ground along
    /// a smoothstep; 5 degrees, the "few degrees" of Plescia et al. (2019).
    double fullSlope = 0.08726646259971647;
    /// The critical gradient Sc (rise over run) toward which downslope transport grows without
    /// bound: a crater on a regional gradient S wears 1 / (1 - (S / Sc)^2) times as fast, the
    /// nonlinear hillslope transport of Roering et al. (1999,
    /// https://doi.org/10.1029/1998WR900090) that Fassett and Thomson (2014,
    /// https://doi.org/10.1002/2014JE004698) suggest for steep lunar slopes. Unset wears craters
    /// alike on any slope; it applies whether or not `enabled` is set. No lunar value is
    /// published, so it has no default.
    std::optional<double> criticalGradient{};
    /// The fastest a slope makes craters wear, over level ground; at least 1. It caps the rate as
    /// S nears Sc, where the form grows without bound; a numerical limit, not a measured one.
    double maxWearRate = 10.0;

    bool operator==(const CraterSlopeResponse&) const = default;
};

/// How many times as fast a crater wears on ground rising `east` and `north` metres per metre,
/// under `response`'s critical gradient; 1 without one.
double SlopeWearRate(const CraterSlopeResponse& response, double east, double north);

/// Checks slope response settings.
/// @throws std::invalid_argument for an invalid floor offset distribution or one outside
/// [0, 0.23), an `upslopeRim` outside [0, 1], or a `fullSlope` that is not positive and finite.
void ValidateCraterSlopeResponse(const CraterSlopeResponse& response);

/// One crater's asymmetry on its slope.
struct CraterSlopeShape {
    Vec2d shift{};    ///< Where the deepest point moves, east and north of the centre (m).
    Vec2d upslope{};  ///< Upslope unit vector times the share of raised relief removed there.

    /// Whether the crater keeps its level-ground shape.
    bool Level() const {
        return shift.x == 0.0 && shift.y == 0.0 && upslope.x == 0.0 && upslope.y == 0.0;
    }
};

/// The asymmetry `response` gives `crater` on ground rising `east` and `north` metres per metre,
/// its floor offset drawn from its ID; level when disabled or on level ground.
CraterSlopeShape SlopeShape(const Crater& crater, double east, double north,
                            const CraterSlopeResponse& response);

}  // namespace terraingen
