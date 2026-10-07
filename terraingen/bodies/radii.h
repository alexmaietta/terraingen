#pragma once

#include "terraingen/util/surface.h"

#include <optional>
#include <string_view>

namespace terraingen {

/// Bodies with a radius preset.
enum class Body { Moon, Mars, Mercury };

/// The volumetric mean radius of `body` in metres, from NASA's planetary fact sheets: the Moon
/// 1737.4 km, Mars 3389.5 km and Mercury 2439.7 km (equatorial and polar radii 1738.1 and 1736.0,
/// 3396.2 and 3376.2, and 2440.5 and 2438.3 km).
///
/// Reference: D. R. Williams, NASA Space Science Data Coordinated Archive, Planetary Fact Sheets
/// (Moon, Mars and Mercury), last updated 11 January 2024.
/// https://nssdc.gsfc.nasa.gov/planetary/factsheet/
double MeanRadius(Body body);

/// The sphere of `body`'s mean radius.
Surface BodySphere(Body body);

/// The body named `name` ("moon", "mars" or "mercury"), if any.
std::optional<Body> BodyNamed(std::string_view name);

}  // namespace terraingen
