/// @file
/// @brief `Boulder`, the record of one placed boulder.
#pragma once

#include <cstdint>
#include <optional>

namespace terraingen {

/// @brief One boulder: where it stands, how large it is and how it lies, in surface coordinates
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

    /// @brief Height of the boulder's top above the surface it stands on.
    /// @return `height` times one minus `burial`, in metres.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    bool operator==(const Boulder&) const = default;
};

}  // namespace terraingen
