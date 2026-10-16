/// @file
/// @brief `Boulder`, the record of one placed boulder.
#pragma once

#include <cstdint>
#include <optional>

namespace terraingen {

/// One boulder: where it stands, how large it is and how it lies, in surface coordinates
/// and metres.
struct Boulder {
    double x = 0.0;         ///< Surface x of its centre.
    double y = 0.0;         ///< Surface y of its centre.
    double diameter = 0.0;  ///< Largest horizontal extent.
    double height = 0.0;    ///< Full height of the boulder, buried part included.
    double burial = 0.0;    ///< Share of `height` below the surface, in [0, 1).
    double yaw = 0.0;       ///< Rotation about the vertical, radians counter-clockwise from east.
    double lean = 0.0;      ///< Tilt of the boulder's vertical axis from the vertical, radians.
    std::uint64_t id = 0;   ///< Stable ID, keyed like crater IDs.
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

    /// Height of the top above the surface it stands on, `height` (1 - `burial`), in metres.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    bool operator==(const Boulder&) const = default;
};

}  // namespace terraingen
