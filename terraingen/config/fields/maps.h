/// @file
/// @brief The JSON fields of surface fields and density maps.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"

namespace terraingen::config {

template <typename IO> void Fields(IO& io, FieldSpec& field) {
    io("path", field.path,
       "GeoTIFF read in the surface's coordinates, relative to the configuration's folder: "
       "metres on a plane, longitude and latitude in degrees on a sphere. Empty uses value.");
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
    io("min_diameter", map.minDiameter,
       "Smallest diameter the map measures, in metres; positive for a count or areal fraction.",
       "m");
    io("max_diameter", map.maxDiameter,
       "Largest diameter the map measures, in metres, exclusive; unset for no limit.", "m");
    io("area_unit", map.areaUnit,
       "Square metres per area unit of a count: 1 for per m^2, 1e6 for per km^2.", "m^2",
       Above(0.0));
    io("total", map.total,
       "The map counts every feature of its kind, including the secondaries thrown from a crater "
       "layer's craters and the boulders a layer throws out around craters, which are taken out "
       "of it first; off when it counts only what the layer generates.");
}

}  // namespace terraingen::config
