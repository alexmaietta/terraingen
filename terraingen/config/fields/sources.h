/// @file
/// @brief The JSON fields of surfaces and sources.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"

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
    io("radius", spec.radius, "Radius of the body's datum sphere, in metres; required.", "m");
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
    GeoTiffSourceOptions& read = source.read;
    io("band", read.band, "Band of the file to read, counting from 0.");
    io("pixel_size", read.pixelSize,
       "Cell size in metres to resample the raster to over the same extent, as when upscaling a "
       "DEM. Unset keeps the file's.",
       "m");
    io("resampling", read.resampling, "Interpolation used when resampling to the cell size.");
    io("scale", read.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata. Unset keeps the file's.");
    io("offset", read.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.",
       "m");
    io("low_pass", read.lowPass,
       "Standard deviation in metres of a Gaussian low-pass applied to the file's heights before "
       "any resampling, keeping a DEM's low frequencies for synthetic detail to be added to; a "
       "dem_detail layer can add the removed detail back. 0 keeps every frequency.",
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
