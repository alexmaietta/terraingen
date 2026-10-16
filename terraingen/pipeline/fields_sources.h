/// @file
/// @brief The JSON fields of surfaces and sources.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/fields_common.h"

#include <string_view>

namespace terraingen::config {

template <> struct SpecName<PlaneSurfaceSpec> {
    static constexpr std::string_view kName = "plane";
    static constexpr std::string_view kDescription =
        "A flat projected frame in metres, for local sites.";
};
template <typename IO> void Fields(IO& /*io*/, PlaneSurfaceSpec& /*spec*/) {}

template <> struct SpecName<SphereSurfaceSpec> {
    static constexpr std::string_view kName = "sphere";
    static constexpr std::string_view kDescription =
        "A whole body such as a planet or moon: positions in longitude and latitude (degrees) and "
        "features generated over the whole body.";
};
template <typename IO> void Fields(IO& io, SphereSurfaceSpec& spec) {
    io("radius", spec.radius,
       "Radius of the body's datum sphere, in metres; give it or a body, not both.", "m");
    io("body", spec.body,
       "Body whose mean radius to use instead of a radius; unset requires the radius.");
}

template <> struct SpecName<FlatSourceSpec> {
    static constexpr std::string_view kName = "flat";
    static constexpr std::string_view kDescription =
        "A level datum over a grid of square cells, to build a synthetic terrain on from scratch.";
};
template <typename IO> void Fields(IO& io, FlatSourceSpec& source) {
    io("width", source.width, "Columns of the grid.");
    io("height", source.height, "Rows of the grid.");
    io("pixel_size", source.pixelSize,
       "Cell size in metres; on a sphere, metres along a meridian, the same in degrees of "
       "longitude.",
       "m");
    io("elevation", source.elevation, "Height of the level datum, in metres.", "m");
    io("centre_x", source.centreX,
       "Centre of the grid along x in surface coordinates; the longitude in degrees on a sphere.");
    io("centre_y", source.centreY,
       "Centre of the grid along y in surface coordinates; the latitude in degrees on a sphere.");
}

template <> struct SpecName<RasterSourceSpec> {
    static constexpr std::string_view kName = "raster";
    static constexpr std::string_view kDescription =
        "A heightmap read from a GeoTIFF in its own projected coordinates, optionally resampled, "
        "to add relief to a real DEM.";
};
template <typename IO> void Fields(IO& io, RasterSourceSpec& source) {
    io("path", source.path, InputPathHelp("GeoTIFF to read the heights from"));
    io("band", source.band, "Band of the file to read, counting from 0.");
    io("pixel_size", source.pixelSize,
       "Cell size in metres to resample the raster to over the same extent, as when upscaling a "
       "DEM. Unset keeps the file's.",
       "m");
    io("resampling", source.resampling, "Interpolation used when resampling to the cell size.");
    io("scale", source.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata. Unset keeps the file's.");
    io("offset", source.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.",
       "m");
}

template <> struct SpecName<ChainSourceSpec> {
    static constexpr std::string_view kName = "chain";
    static constexpr std::string_view kDescription =
        "Several sources tried in order, each filling where those before it have no data; the "
        "terrain covers the first source's grid.";
};
template <typename IO> void Fields(IO& io, ChainSourceSpec& source) {
    io("sources", source.sources,
       "Sources tried in order, each filling where those before it have no data; the terrain "
       "covers the first source's grid.");
}

}  // namespace terraingen::config
