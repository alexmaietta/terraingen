/// @file
/// @brief The JSON fields of bodies and bases.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"

#include <string>
#include <string_view>

namespace terraingen::config {

/// The cells along a cube face edge a body mesh or shape model may have.
inline FieldRange CubeFaceCellsRange() {
    return Between(1.0, static_cast<double>(kMaxCubeFaceCells));
}

/// The fields of how a GeoTIFF's heights are read: its band, the cell size it is resampled to
/// when `resamples`, the interpolation, and the scale and offset of its samples.
/// @param resampling The description of the interpolation.
/// @param prefix Prepended to each key.
template <typename IO>
void GeoTiffReadFields(IO& io, GeoTiffSourceOptions& read, std::string_view resampling,
                       bool resamples, std::string_view prefix = {}) {
    const auto key = [&](std::string_view name) { return std::string(prefix) + std::string(name); };
    io(key("band"), read.band, "Band of the file to read, counting from 0.");
    if (resamples) {
        io(key("pixel_size"), read.pixelSize,
           "Cell size in metres to resample the raster to over the same extent, as when upscaling "
           "a DEM. Unset keeps the file's.",
           "m", Above(0.0));
    }
    io(key("resampling"), read.resampling, resampling);
    io(key("scale"), read.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata. Unset keeps the file's.");
    io(key("offset"), read.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.",
       "m");
}

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
    io("radius", spec.radius, "Radius of the body's datum sphere, in metres; required.", "m",
       Above(0.0));
}

template <> struct SpecName<EllipsoidSurfaceSpec> {
    static constexpr std::string_view kName = "ellipsoid";
    static constexpr std::string_view kDescription =
        "A whole body shaped as a triaxial ellipsoid, such as an elongated moon or asteroid: "
        "planetocentric longitude and latitude (degrees), heights along the direction from the "
        "centre, and features generated over the whole body.";
};
template <typename IO> void Fields(IO& io, EllipsoidSurfaceSpec& spec) {
    io("radii", spec.radii,
       "Semi-axes a, b and c of the datum ellipsoid in metres, along x (longitude 0), y "
       "(longitude 90) and z (the north pole); exactly three, each positive.",
       "m", Above(0.0));
}

template <> struct SpecName<ShapeModelSurfaceSpec> {
    static constexpr std::string_view kName = "shape_model";
    static constexpr std::string_view kDescription =
        "A whole star-shaped body such as an asteroid, from a shape model: planetocentric "
        "longitude and latitude (degrees), heights along the direction from the centre, and "
        "features generated over the whole body. The model is resampled along rays from its "
        "origin, keeping the outermost surface where a ray crosses it more than once.";
};
template <typename IO> void Fields(IO& io, ShapeModelSurfaceSpec& spec) {
    io("path", spec.path,
       InputPathHelp("Shape model in body-fixed coordinates around its origin: a Gaskell ICQ "
                     "model (.icq) or a closed mesh (.obj, .gltf, .glb or .stl)"));
    io("q", spec.q,
       "Intervals per cube face edge of the radial grid the model is resampled on, " +
           CountText(1, kMaxCubeFaceCells) +
           "; unset keeps an ICQ model's own Q and uses 256 for a mesh.",
       "", CubeFaceCellsRange());
    io("metres_per_unit", spec.metresPerUnit,
       "Metres per unit of the file's coordinates; unset is 1000 for an ICQ model, whose "
       "coordinates are kilometres, and 1 for a mesh.",
       "m", Above(0.0));
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
    GeoTiffReadFields(io, read, "Interpolation used when resampling to the cell size.", true);
    io("low_pass", read.lowPass,
       "Standard deviation in metres of a Gaussian low-pass applied to the file's heights before "
       "any resampling, keeping a DEM's low frequencies for synthetic detail to be added to; a "
       "dem_detail layer can add the removed detail back. 0 keeps every frequency.",
       "m", AtLeast(0.0));
}

template <> struct SpecName<ChainSourceSpec> {
    static constexpr std::string_view kName = "chain";
    static constexpr std::string_view kDescription =
        "Several bases tried in order, each filling where those before it have no data; its grid "
        "is the first one's.";
};
template <typename IO> void Fields(IO& io, ChainSourceSpec& source) {
    io("sources", source.sources,
       "Bases tried in order, each filling where those before it have no data; the chain's grid "
       "is the first one's.");
}

}  // namespace terraingen::config
