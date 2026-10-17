/// @file
/// @brief `Boulder`, the record of one placed boulder.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace terraingen {

/// Which of its axes a boulder stands on end along, the one turned to the vertical.
enum class BoulderRest : std::uint8_t {
    Face,  ///< On its broadest face: its short axis up.
    Side,  ///< On a side: its intermediate axis up.
    End,   ///< On an end: its long axis up.
};

/// A plane that sand-laden wind has cut across a boulder's windward side: a ventifact facet.
/// Rock above the plane is gone.
struct BoulderFacet {
    /// Direction the facet faces (upwind), radians counter-clockwise from east.
    double azimuth = 0.0;
    double angle = 0.0;  ///< Slope of the facet from horizontal, radians, in (0, 90) deg.
    /// Distance (m) toward `azimuth` from the boulder's centre at which the facet meets the surface
    /// the boulder stands on.
    double reach = 0.0;

    /// The facet's height (m) above that surface at east and north `offset` metres from the
    /// boulder's centre, negative beyond `reach`.
    double HeightAt(const Vec2d& offset) const {
        const double along = offset.x * std::cos(azimuth) + offset.y * std::sin(azimuth);
        return (reach - along) * std::tan(angle);
    }

    bool operator==(const BoulderFacet&) const = default;
};

/// One boulder: where it stands, how large it is and how it lies, in surface coordinates
/// and metres.
struct Boulder {
    double x = 0.0;         ///< Surface x of its centre.
    double y = 0.0;         ///< Surface y of its centre.
    double diameter = 0.0;  ///< Length of its long axis: its largest horizontal extent on its face.
    /// Length of its short axis, buried part included: its full height resting on its face.
    double height = 0.0;
    double burial = 0.0;   ///< Share of its vertical extent below the surface, in [0, 1).
    double yaw = 0.0;      ///< Rotation about the vertical, radians counter-clockwise from east.
    double lean = 0.0;     ///< Tilt of the boulder's vertical axis from the vertical, radians.
    std::uint64_t id = 0;  ///< Stable ID, keyed like crater IDs.
    BoulderRest rest = BoulderRest::Face;  ///< Which axis it stands along.
    double tilt = 0.0;         ///< Angle of its up axis from the vertical, after lean, radians.
    double tiltAzimuth = 0.0;  ///< Direction its up axis tips toward, radians from east.
    /// Height of the ground beneath its centre before any boulders, when the layer knows it.
    std::optional<double> ground{};
    /// Placed as a 3D model of its own rather than drawn into the heights
    /// (`BoulderModelThreshold`).
    bool model = false;
    /// How worn it is, from 0 (fresh) to 1: its crater's wear for a crater block, uniform between
    /// craters.
    double wear = 0.0;
    /// For a fragment, the ID of the boulder it broke from; 0 for a whole boulder.
    std::uint64_t parent = 0;
    std::uint32_t piece = 0;  ///< For a fragment, which piece of its parent it is.
    /// Its ventifact facets, the first `facetCount` of them (`BoulderVentifacts`).
    std::array<BoulderFacet, 2> facets{};
    std::uint8_t facetCount = 0;  ///< How many facets it has, 0 to 2.

    /// Height of the top above the surface it stands on, `height` (1 - `burial`), in metres.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    /// `top`, a height (m) of the boulder above the surface at east and north `offset` metres from
    /// its centre, lowered to its facets and not below 0.
    double CutByFacets(const Vec2d& offset, double top) const {
        for (std::uint8_t k = 0; k < facetCount; ++k) {
            top = std::fmin(top, facets[k].HeightAt(offset));
        }
        return std::fmax(top, 0.0);
    }

    bool operator==(const Boulder&) const = default;
};

}  // namespace terraingen
