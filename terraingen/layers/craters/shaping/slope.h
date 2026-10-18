/// @file
/// @brief Craters formed on slopes: floors offset downslope and raised rims lowered upslope.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/geometry.h"

#include <cstdint>

namespace terraingen {

/// How craters formed on a slope, read from their fitted datum plane, become asymmetric along it.
///
/// Lunar craters on slopes have their deepest point offset downslope, an upslope margin that is
/// often only a break in slope with little raised rim, and ejecta thrown mostly downslope; the
/// asymmetry shows from slopes of a few degrees and does not grow on steeper ones, and they are
/// on average no longer downslope than across (Plescia et al. 2019,
/// https://www.hou.usra.edu/meetings/lpsc2019/pdf/2984.pdf). Needs the `Plane` datum.
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

    bool operator==(const CraterSlopeResponse&) const = default;
};

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

/// Where the round profile is read for a point at `offset` metres from the centre: moved back by
/// `shape`'s shift in full within 0.3 radii, fading along a smoothstep to none at the rim.
/// @param radius Crater radius in metres.
Vec2d SlopeShiftedOffset(const CraterSlopeShape& shape, const Vec2d& offset, double radius);

/// The share of a crater's raised relief, rim and ejecta, kept at `offset` from its centre: 1 less
/// the removed share times the squared cosine of the angle from upslope, smooth across the slope.
double SlopeRaisedShare(const CraterSlopeShape& shape, const Vec2d& offset);

}  // namespace terraingen
