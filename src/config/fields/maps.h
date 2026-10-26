// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The JSON fields of surface fields and density maps.
#pragma once

#include "config/fields/common.h"
#include "config/spec.h"

namespace terraingen::config {

template <typename IO> void Fields(IO& io, FieldSpec& field) {
    io("path", field.path,
       "GeoTIFF read in the surface's coordinates, relative to the configuration's folder: "
       "meters on a plane, longitude and latitude in degrees on a sphere. Empty uses value.");
    io("value", field.value, "The field everywhere when no path is given.");
    io("band", field.band, "Band of the GeoTIFF read, from 0.");
    io("method", field.method,
       "How the GeoTIFF is interpolated: nearest or bilinear, which stay within the cells read.");
    io("scale", field.scale,
       "Multiplies every cell, such as 0.001 for a map stored in thousandths; positive.", "",
       Above(0.0));
    io("fallback", field.fallback,
       "Value outside the GeoTIFF and where its cells are no-data or NaN; unset leaves none, and "
       "the layer then keeps its own setting there.");
    io("periodic_longitude", field.periodicLongitude,
       "The GeoTIFF's x is longitude repeating every 360 degrees, so a map from 0 to 360 serves "
       "-180 to 180.");
}

template <typename IO> void Fields(IO& io, DensityMapSpec& map) {
    io("field", map.field, "The map.");
    io("measure", map.measure, "What the map's values measure.");
    io("min_diameter_m", map.minDiameter,
       "Smallest diameter the map measures, in meters; positive for a count or areal fraction.",
       "m");
    io("max_diameter_m", map.maxDiameter,
       "Largest diameter the map measures, in meters, exclusive; unset for no limit.", "m");
    io("area_unit", map.areaUnit,
       "Square meters per area unit of a count: 1 for per m^2, 1e6 for per km^2.", "m^2",
       Above(0.0));
    io("total", map.total,
       "The map counts every feature of its kind, including the secondaries thrown from a crater "
       "layer's craters and the boulders a layer throws out around craters, which are taken out "
       "of it first; off when it counts only what the layer generates.");
}

template <typename IO> void Fields(IO& io, SizeSlopeMapSpec& map) {
    io("field", map.field, "The map.");
    io("measure", map.measure, "What the map's values measure.");
    io("reference_diameter_m", map.referenceDiameter,
       "Diameter D0 in meters where the local power law meets the layer's own count, as N(>=D) = "
       "N(>=D0) (D / D0)^-alpha; for a mean diameter map, the smallest diameter averaged.",
       "m", Above(0.0));
    io("min_exponent", map.minExponent,
       "Smallest exponent alpha the map's values are clamped to; with the largest it bounds the "
       "boulders drawn, so generous bounds draw more to thin.",
       "", Above(0.0));
    io("max_exponent", map.maxExponent,
       "Largest exponent alpha the map's values are clamped to; above the smallest.", "",
       Above(0.0));
}

}  // namespace terraingen::config
