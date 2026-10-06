#pragma once

#include <cstdint>

namespace terraingen {

/// One rock: where it stands, how large it is and how it lies, in surface coordinates and metres. A
/// renderer or physics engine places a body or mesh with these; the rock layer adds the exposed
/// part to the heights.
struct Rock {
    double x = 0.0;
    double y = 0.0;
    double diameter = 0.0;  ///< Largest horizontal extent.
    double height = 0.0;    ///< Full height of the rock, buried part included.
    double burial = 0.0;    ///< Share of `height` below the surface, in [0, 1).
    double yaw = 0.0;       ///< Rotation about the vertical, radians counter-clockwise from east.
    double lean = 0.0;      ///< Tilt of the rock's vertical axis from the vertical, radians.
    std::uint64_t id = 0;   ///< Stable ID, keyed like crater IDs.

    /// Height of the rock's top above the surface it stands on.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    bool operator==(const Rock&) const = default;
};

}  // namespace terraingen
