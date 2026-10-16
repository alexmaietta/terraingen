/// @file
/// @brief `Boulder`, the record of one placed boulder.
#pragma once

#include <cstdint>
#include <optional>

namespace terraingen {

/// Which of its axes a boulder stands on end along, the one turned to the vertical.
enum class BoulderRest : std::uint8_t {
    Face,  ///< On its broadest face: its short axis up.
    Side,  ///< On a side: its intermediate axis up.
    End,   ///< On an end: its long axis up.
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

    /// Height of the top above the surface it stands on, `height` (1 - `burial`), in metres.
    double ExposedHeight() const {
        return height * (1.0 - burial);
    }

    bool operator==(const Boulder&) const = default;
};

}  // namespace terraingen
