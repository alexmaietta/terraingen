/// @file
/// @brief Bedform presets for Martian aeolian features.
#pragma once

#include "terraingen/layers/bedforms.h"

namespace terraingen {

/// @brief Martian large wind ripples: 2 m wavelength, 0.2 m height, a 30 degree lee slope and
/// sinuous crests; cover and field size are placeholders to set per site.
/// @return The bedform options.
BedformOptions MarsLargeRipples();

/// @brief Martian transverse aeolian ridges: symmetric, 25.8 m wavelength and 1.3 m height; cover
/// and field size are placeholders to set per site.
/// @return The bedform options.
BedformOptions MarsTransverseAeolianRidges();

}  // namespace terraingen
