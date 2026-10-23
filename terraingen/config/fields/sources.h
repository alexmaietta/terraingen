/// @file
/// @brief The JSON fields of bodies, sources and upsampling.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace terraingen::config {

/// The cells along a cube face edge a body mesh or shape model may have.
inline FieldRange CubeFaceCellsRange() {
    return Between(1.0, static_cast<double>(kMaxCubeFaceCells));
}

/// The fields of a box in a raster's CRS units, as a GeoTIFF's `crop`.
template <typename IO> void Fields(IO& io, Bounds2d& box) {
    io("min_x", box.minX, "Lower edge along x, in the raster's CRS units.");
    io("min_y", box.minY, "Lower edge along y, in the raster's CRS units.");
    io("max_x", box.maxX, "Upper edge along x, in the raster's CRS units; above min_x.");
    io("max_y", box.maxY, "Upper edge along y, in the raster's CRS units; above min_y.");
}

/// The fields of diamond-square upsampling, beside its "type".
template <typename IO> void Fields(IO& io, DiamondSquareOptions& options) {
    io("fractal_dimension", options.fractalDimension,
       "Fractal dimension, from 2 (smooth) to 3 (rough): each square or diamond step scales the "
       "displacement by 2^-((3 - F) / 2). Unset estimates it from the posts' variogram.",
       "", Between(2.0, 3.0));
    io("scale", options.scale,
       "Standard deviation in meters of the displacement at the posts' own last diamond step. "
       "Unset estimates it from the posts as the RMS of each post minus the mean of its four "
       "neighbors.",
       "m", AtLeast(0.0));
    io("interp", options.interp,
       "How new nodes are filled from the coarser ones and samples read between the finest "
       "nodes.");
    io("min_pixel_size_m", options.minPixelSize,
       "Finest node spacing in meters the posts are refined to; 0 refines down to the sample "
       "spacing.",
       "m", AtLeast(0.0));
    io("seed", options.seed, "Seed of the displacements.");
}

/// The description of a terrain noise stack of `fewest` to `kMaxNoiseLayers` layers.
inline std::string NoiseStackHelp(std::size_t fewest) {
    return "Noise layers, " + CountText(fewest, kMaxNoiseLayers) +
           ", combined in order: the first enabled layer sets the height and each later one is "
           "combined with it by its blend mode.";
}

/// The description of a source's upsampling.
inline constexpr std::string_view kUpsamplingHelp =
    "How the posts become heights between them and at sample spacings finer than them: a name "
    "(nearest, bilinear, cubic, catmull_rom, lanczos3, cubic_bspline, or a type registered in "
    "code), or an object with a \"type\" and its settings, such as {\"type\": \"diamond_square\", "
    "\"fractal_dimension\": 2.3}, which adds fractal detail below the posts' spacing.";

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
        "A heightmap read from a GeoTIFF in its own projected coordinates, its cells upsampled to "
        "finer spacings, to add relief to a real DEM.";
};
template <typename IO> void Fields(IO& io, RasterSourceSpec& source) {
    io("path", source.path, InputPathHelp("GeoTIFF to read the heights from"));
    GeoTiffSourceOptions& read = source.read;
    io("band", read.band, "Band of the file to read, counting from 0.");
    io("scale", read.scale,
       "Factor each sample is multiplied by, replacing the band's scale from the file's GDAL "
       "metadata. Unset keeps the file's.");
    io("offset", read.offset,
       "Added to each scaled sample, replacing the band's offset from the file's GDAL metadata. "
       "Unset keeps the file's.",
       "m");
    io("crop", read.crop,
       "Box in the raster's CRS units to read: only the cells meeting it are used, read from a "
       "window of the file, with the same heights there as reading the whole file. Unset reads "
       "the whole file.");
    io("effective_pixel_size_m", read.effectivePixelSize,
       "Cell size in meters the heights truly resolve, for a DEM that was upsampled or smoothed "
       "before it was saved; it replaces the cell size as the scale below which the source "
       "resolves no features. Unset uses the cell size.",
       "m", Above(0.0));
    io("upsampling", source.upsampling, kUpsamplingHelp);
}

template <> struct SpecName<ChainSourceSpec> {
    static constexpr std::string_view kName = "chain";
    static constexpr std::string_view kDescription =
        "Several sources tried in order, each filling where those before it have no data; its "
        "grid is the first one's.";
};
template <typename IO> void Fields(IO& io, ChainSourceSpec& source) {
    io("sources", source.sources,
       "Sources tried in order, each filling where those before it have no data, each upsampled "
       "as it says; the chain's grid is the first one's.");
}

template <> struct SpecName<SyntheticSourceSpec> {
    static constexpr std::string_view kName = "synthetic";
    static constexpr std::string_view kDescription =
        "Heights generated from a stack of fractal noise layers over an unbounded plane or the "
        "whole body; outputs give their own extents and pixel sizes.";
};
template <typename IO> void Fields(IO& io, SyntheticSourceSpec& source) {
    io("elevation_m", source.elevation, "Height in meters added everywhere.", "m");
    io("stack", source.stack, NoiseStackHelp(0) + " None gives a flat source.");
    io("pixel_size_m", source.pixelSize,
       "Spacing in meters of posts the noise is sampled at, as for a DEM of that cell size, "
       "which upsampling refines. Unset evaluates the noise at every point, at any spacing.",
       "m", Above(0.0));
    io("upsampling", source.upsampling,
       std::string(kUpsamplingHelp) + " Needs pixel_size_m; unset is cubic.");
}

}  // namespace terraingen::config
