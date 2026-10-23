/// @file
/// @brief `Boulder`, the record of one placed boulder, and `ManualBoulder`, one placed by hand.
#pragma once

#include "terraingen/util/geometry.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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
    /// Distance (m) toward `azimuth` from the boulder's center at which the facet meets the surface
    /// the boulder stands on.
    double reach = 0.0;

    /// The facet's height (m) above that surface at east and north `offset` meters from the
    /// boulder's center, negative beyond `reach`.
    double HeightAt(const Vec2d& offset) const {
        const double along = offset.x * std::cos(azimuth) + offset.y * std::sin(azimuth);
        return (reach - along) * std::tan(angle);
    }

    bool operator==(const BoulderFacet& other) const {
        return azimuth == other.azimuth && angle == other.angle && reach == other.reach;
    }
    bool operator!=(const BoulderFacet& other) const {
        return !(*this == other);
    }
};

/// One boulder: where it stands, how large it is and how it lies, in surface coordinates
/// and meters.
struct Boulder {
    double x = 0.0;         ///< Surface x of its center.
    double y = 0.0;         ///< Surface y of its center.
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
    /// Height of the ground beneath its center before any boulders, when the layer knows it.
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
    /// The seed of its shape; unset derives it from its ID, or its parent's for a fragment
    /// (`BoulderShapeSeed`).
    std::optional<std::uint64_t> shapeSeed{};

    /// Height of the top above the surface it stands on, `height` (1 - `burial`), in meters.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    /// `top`, a height (m) of the boulder above the surface at east and north `offset` meters from
    /// its center, lowered to its facets and not below 0.
    double CutByFacets(const Vec2d& offset, double top) const {
        for (std::uint8_t k = 0; k < facetCount; ++k) {
            top = std::fmin(top, facets[k].HeightAt(offset));
        }
        return std::fmax(top, 0.0);
    }

    bool operator==(const Boulder& other) const {
        return x == other.x && y == other.y && diameter == other.diameter &&
               height == other.height && burial == other.burial && yaw == other.yaw &&
               lean == other.lean && id == other.id && rest == other.rest && tilt == other.tilt &&
               tiltAzimuth == other.tiltAzimuth && ground == other.ground && model == other.model &&
               wear == other.wear && parent == other.parent && piece == other.piece &&
               facets == other.facets && facetCount == other.facetCount &&
               shapeSeed == other.shapeSeed;
    }
    bool operator!=(const Boulder& other) const {
        return !(*this == other);
    }
};

/// A boulder placed by hand: its center and size, and any of its pose and shape fixed; NaN or
/// unset fields are drawn as for generated boulders.
struct ManualBoulder {
    double x = 0.0;         ///< Surface x of its center.
    double y = 0.0;         ///< Surface y of its center.
    double diameter = 0.0;  ///< Length of its long axis (m).
    /// Stable ID; 0 derives one from position and size (`ManualCraterId`).
    std::uint64_t id = 0;
    /// Vertical extent (m), buried part included; NaN draws it from the pose options.
    double height = std::numeric_limits<double>::quiet_NaN();
    /// Buried share of the height, in [0, 1); NaN draws it.
    double burial = std::numeric_limits<double>::quiet_NaN();
    /// Rotation about the vertical, radians counter-clockwise from east; NaN draws it.
    double yaw = std::numeric_limits<double>::quiet_NaN();
    /// Tilt of its vertical axis from the vertical, radians; NaN draws it.
    double lean = std::numeric_limits<double>::quiet_NaN();
    /// Angle of its up axis from the vertical after lean, radians; NaN draws it.
    double tilt = std::numeric_limits<double>::quiet_NaN();
    /// Direction its up axis tips toward, radians from east; NaN draws it.
    double tiltAzimuth = std::numeric_limits<double>::quiet_NaN();
    /// How worn it is, from 0 (fresh) to 1; NaN draws it.
    double wear = std::numeric_limits<double>::quiet_NaN();
    std::optional<BoulderRest> rest{};  ///< Which axis it stands along; unset draws it.
    /// The seed of its shape (`GenerateBoulderShape`); unset derives it from its ID.
    std::optional<std::uint64_t> shapeSeed{};

    /// Whether any of its lean, tilt, tilt azimuth or rest is fixed.
    bool FixesOrientation() const {
        return !std::isnan(lean) || !std::isnan(tilt) || !std::isnan(tiltAzimuth) ||
               rest.has_value();
    }

    /// Whether any of its pose or shape is fixed.
    bool FixesAny() const {
        return FixesOrientation() || !std::isnan(height) || !std::isnan(burial) ||
               !std::isnan(yaw) || !std::isnan(wear) || shapeSeed.has_value();
    }

    /// Fieldwise equality, two NaN fields counting as equal.
    bool operator==(const ManualBoulder& other) const {
        const auto same = [](double a, double b) {
            return a == b || (std::isnan(a) && std::isnan(b));
        };
        return x == other.x && y == other.y && diameter == other.diameter && id == other.id &&
               same(height, other.height) && same(burial, other.burial) && same(yaw, other.yaw) &&
               same(lean, other.lean) && same(tilt, other.tilt) &&
               same(tiltAzimuth, other.tiltAzimuth) && same(wear, other.wear) &&
               rest == other.rest && shapeSeed == other.shapeSeed;
    }
    bool operator!=(const ManualBoulder& other) const {
        return !(*this == other);
    }
};

}  // namespace terraingen
