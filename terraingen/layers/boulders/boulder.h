#pragma once

#include <cstdint>
#include <optional>

namespace terraingen {

/// One boulder: where it stands, how large it is and how it lies, in surface coordinates and
/// metres. A renderer or physics engine places a body or mesh with these; the boulder layer adds
/// the exposed part to the heights.
struct Boulder {
    double x = 0.0;
    double y = 0.0;
    double diameter = 0.0;  ///< Largest horizontal extent.
    double height = 0.0;    ///< Full height of the boulder, buried part included.
    double burial = 0.0;    ///< Share of `height` below the surface, in [0, 1).
    double yaw = 0.0;       ///< Rotation about the vertical, radians counter-clockwise from east.
    double lean = 0.0;      ///< Tilt of the boulder's vertical axis from the vertical, radians.
    std::uint64_t id = 0;   ///< Stable ID, keyed like crater IDs.
    /// Height of the ground beneath the boulder's centre before any boulders, when the layer knows
    /// it.
    std::optional<double> ground{};
    /// Placed as a 3D model of its own rather than drawn into the heights
    /// (`BoulderModelThreshold`).
    bool model = false;

    /// Height of the boulder's top above the surface it stands on.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    bool operator==(const Boulder&) const = default;
};

}  // namespace terraingen
