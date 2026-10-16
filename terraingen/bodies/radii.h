/// @file
/// @brief Mean radii of the bodies with presets, and their spheres.
#pragma once

#include "terraingen/util/surface.h"

#include <optional>
#include <string_view>

namespace terraingen {

/// @brief Bodies with a radius preset.
enum class Body { Moon, Mars, Mercury };

/// @brief The volumetric mean radius of `body`: the Moon 1737.4 km, Mars 3389.5 km and Mercury
/// 2439.7 km.
/// @param body The body.
/// @return The radius in metres.
double MeanRadius(Body body);

/// @brief The sphere of `body`'s mean radius.
/// @param body The body.
/// @return A spherical surface of radius `MeanRadius(body)`.
Surface BodySphere(Body body);

/// @brief The body named `name`.
/// @param name "moon", "mars" or "mercury".
/// @return The body, or nullopt for any other name.
std::optional<Body> BodyNamed(std::string_view name);

}  // namespace terraingen
