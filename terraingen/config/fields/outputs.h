/// @file
/// @brief The JSON fields of the outputs.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"
#include "terraingen/config/fields/layers.h"
#include "terraingen/config/fields/sources.h"

#include <string>
#include <string_view>

namespace terraingen::config {

/// The cells a grid side may have.
inline FieldRange GridSizeRange() {
    return Between(static_cast<double>(kMinConfigGridSize),
                   static_cast<double>(kMaxConfigGridSize));
}

template <typename IO> void Fields(IO& io, GridRegionSpec& region) {
    io("width", region.width, "Columns of the grid.", "", GridSizeRange());
    io("height", region.height, "Rows of the grid.", "", GridSizeRange());
    io("pixel_size", region.pixelSize,
       "Cell size in metres; on a body, metres along a meridian, the same in degrees of "
       "longitude.",
       "m", Above(0.0));
    io("centre_x", region.centreX,
       "Centre of the grid along x in surface coordinates; the longitude in degrees on a body.");
    io("centre_y", region.centreY,
       "Centre of the grid along y in surface coordinates; the latitude in degrees on a body.");
}

template <typename IO> void Fields(IO& io, NestedLevelRef& nested) {
    io("region", nested.region, "Name of the region; required.");
    io("level", nested.level,
       "Level of the region, 0 being the base it lies in; unset for every level, with {level} in "
       "the path replaced by its number.");
}

/// The description of an output's nested level.
inline std::string NestedHelp(std::string_view what) {
    return "Level of a named region " + std::string(what) +
           ", in place of a region; {region} in the path is replaced by its name";
}

/// The description of an output's region: `what` it covers.
inline std::string RegionHelp(std::string_view what) {
    return "Grid of square cells " + std::string(what) + "; unset for the base's grid";
}

/// The fields of heightmap and albedo outputs.
template <typename IO> void GeoTiffOutputFields(IO& io, GeoTiffOutput& output) {
    io("path", output.path, OutputPathHelp("GeoTIFF file to write"));
    io("region", output.region, RegionHelp("the file covers") + ".");
    io("nested", output.nested, NestedHelp("the file covers") + ".");
    io("compression", output.compression,
       "Compression of the file; compressed files use the floating-point predictor.");
    io("tile_size", output.tileSize,
       "Width and height of the file's tiles in pixels; a positive multiple of " +
           std::to_string(kTileSizeMultiple) + ".",
       "px", AtLeast(kTileSizeMultiple));
}

/// The fields of where features are listed: a region and a sample spacing.
template <typename IO, typename Output> void FeatureExtentFields(IO& io, Output& output) {
    io("region", output.region,
       RegionHelp("the features are listed over") +
           ", or without a base the whole body on a body.");
    io("spacing", output.spacing,
       "Sample spacing in metres the features are listed at, as a heightmap of that pixel size "
       "applies them, finer or coarser than the region's pixels; unset for the region's pixel "
       "size. Boulder lists hold every boulder of the layers' diameter ranges, and mark models at "
       "it. A whole-body list needs it.",
       "m", Above(0.0));
    io("nested", output.nested, NestedHelp("the features are listed over") + ".");
}

/// The fields of crater and boulder list outputs.
template <typename IO> void ListOutputFields(IO& io, ListOutput& output) {
    io("path", output.path, OutputPathHelp("File to write"));
    io("format", output.format,
       "Format of the file; when not given, .json paths are written as JSON and others as CSV.");
    FeatureExtentFields(io, output);
}

template <> struct SpecName<HeightmapOutput> {
    static constexpr std::string_view kName = "heightmap";
    static constexpr std::string_view kDescription =
        "Writes the generated heights as a float32 GeoTIFF, with a raster base's projection.";
};
template <typename IO> void Fields(IO& io, HeightmapOutput& output) {
    GeoTiffOutputFields(io, output);
}

template <> struct SpecName<CraterListOutput> {
    static constexpr std::string_view kName = "crater_list";
    static constexpr std::string_view kDescription =
        "Writes the generated and manual craters as CSV or JSON.";
};
template <typename IO> void Fields(IO& io, CraterListOutput& output) {
    ListOutputFields(io, output);
}

template <> struct SpecName<BoulderListOutput> {
    static constexpr std::string_view kName = "boulder_list";
    static constexpr std::string_view kDescription =
        "Writes every boulder the layers placed, models and relief alike, as CSV or JSON.";
};
template <typename IO> void Fields(IO& io, BoulderListOutput& output) {
    ListOutputFields(io, output);
}

template <> struct SpecName<BoulderModelsOutput> {
    static constexpr std::string_view kName = "boulder_models";
    static constexpr std::string_view kDescription =
        "Writes the boulders placed as 3D models on the ground: as a glTF scene (.glb, or .gltf "
        "with a .bin) drawing each shape once per boulder, as OBJ (.obj with an .mtl) with one "
        "object per boulder, or as one merged STL mesh (.stl).";
};
template <typename IO> void Fields(IO& io, BoulderModelsOutput& output) {
    io("path", output.path, OutputPathHelp("File to write, ending in .glb, .gltf, .obj or .stl"));
    io("crease_angle_deg", output.creaseAngle,
       "Angle in degrees, from " + CountText(0, static_cast<std::size_t>(kMaxCreaseAngle)) +
           ": vertices are split where faces meet at more than it, giving sharp edges.",
       "deg", Between(0.0, kMaxCreaseAngle));
    io("instanced", output.instanced,
       "Writes each shared shape once and draws it per boulder (glTF instances, OBJ objects); "
       "false writes every boulder as its own placed mesh, for tools without instancing.");
    FeatureExtentFields(io, output);
}

template <> struct SpecName<AlbedoOutput> {
    static constexpr std::string_view kName = "albedo";
    static constexpr std::string_view kDescription =
        "Writes the albedo as a float32 GeoTIFF of one band or three (red, green, blue).";
};
template <typename IO> void Fields(IO& io, AlbedoOutput& output) {
    GeoTiffOutputFields(io, output);
}

template <> struct SpecName<MaterialsOutput> {
    static constexpr std::string_view kName = "materials";
    static constexpr std::string_view kDescription =
        "Writes the material map as a float32 GeoTIFF of five bands: the share of soil, sand, "
        "dust, bedrock and rock at each cell.";
};
template <typename IO> void Fields(IO& io, MaterialsOutput& output) {
    GeoTiffOutputFields(io, output);
}

template <> struct SpecName<PhotometryOutput> {
    static constexpr std::string_view kName = "photometry";
    static constexpr std::string_view kDescription =
        "Writes the Hapke parameters the materials mix to as a float32 GeoTIFF of four bands: "
        "single-scattering albedo, b, c and roughness in degrees.";
};
template <typename IO> void Fields(IO& io, PhotometryOutput& output) {
    GeoTiffOutputFields(io, output);
}

template <> struct SpecName<MeshOutput> {
    static constexpr std::string_view kName = "mesh";
    static constexpr std::string_view kDescription =
        "Writes the whole body, from six cube faces: a mesh as glTF (.glb or .gltf), OBJ (.obj) or "
        "STL (.stl), or a Gaskell ICQ shape model in kilometres (.icq), whose Q is "
        "cells_per_face.";
};
template <typename IO> void Fields(IO& io, MeshOutput& output) {
    io("path", output.path,
       OutputPathHelp("File to write, ending in .glb, .gltf, .obj, .stl or .icq"));
    io("cells_per_face", output.cellsPerFace,
       "Quads along each edge of the six cube faces, " + CountText(1, kMaxCubeFaceCells) +
           "; more give a finer mesh, with two triangles per quad.",
       "", CubeFaceCellsRange());
    io("spacing", output.spacing,
       "Sample spacing in metres the heights are evaluated at, which sets the finest features "
       "kept; unset uses a quad's side at a face's centre.",
       "m", Above(0.0));
}

template <> struct SpecName<TerrainMeshOutput> {
    static constexpr std::string_view kName = "terrain_mesh";
    static constexpr std::string_view kDescription =
        "Writes the generated heights as a triangle mesh, a vertex per cell with data, as glTF "
        "(.glb or .gltf), OBJ (.obj) or STL (.stl), in the frame boulder models are written in.";
};
template <typename IO> void Fields(IO& io, TerrainMeshOutput& output) {
    io("path", output.path, OutputPathHelp("File to write, ending in .glb, .gltf, .obj or .stl"));
    io("region", output.region, RegionHelp("the mesh covers") + ".");
    io("cutouts", output.cutouts,
       "Rectangles in surface coordinates whose quads are left out, a quad when all four corners "
       "lie in one, such as where a finer mesh covers the ground.");
    io("skirt", output.skirt,
       "Depth in metres of a skirt hung below every boundary edge, around cutouts too, hiding "
       "gaps between meshes; 0 for none.",
       "m", AtLeast(0.0));
    io("site_x", output.siteX,
       "x of the site on a body the mesh's frame is centred on, in surface coordinates; unset "
       "for the grid's centre.");
    io("site_y", output.siteY,
       "y of the site on a body the mesh's frame is centred on, in surface coordinates; unset "
       "for the grid's centre.");
    io("nested", output.nested,
       NestedHelp("the mesh covers") + "; the mesh is cut out under the region's finer levels.");
}

template <> struct SpecName<NestedMeshOutput> {
    static constexpr std::string_view kName = "nested_mesh";
    static constexpr std::string_view kDescription =
        "Writes the base and every level of regions sharing it as meshes, each cut out under the "
        "finer levels and framed at the base's centre: one merged file, or one per grid with "
        "{level} in the path.";
};
template <typename IO> void Fields(IO& io, NestedMeshOutput& output) {
    io("path", output.path,
       OutputPathHelp("File to write, ending in .glb, .gltf, .obj or .stl; with {level} (and "
                      "{region} for several regions) one file per grid, the base being level 0 "
                      "of the first region"));
    io("regions", output.regions, "Names of the regions, which share a base; at least one.");
    io("skirt", output.skirt,
       "Depth in metres of a skirt hung below every boundary edge, hiding gaps; 0 for none.", "m",
       AtLeast(0.0));
}

template <> struct SpecName<ManifestOutput> {
    static constexpr std::string_view kName = "manifest";
    static constexpr std::string_view kDescription =
        "Writes a JSON list of the regions' bases and levels: each grid's parent, extent, pixel "
        "size and the files the other outputs write over it.";
};
template <typename IO> void Fields(IO& io, ManifestOutput& output) {
    io("path", output.path, OutputPathHelp("JSON file to write"));
    io("regions", output.regions, "Names of the regions listed; empty for every region.");
}

template <typename IO> void Fields(IO& io, RegionLevelSpec& level) {
    io("pixel_size", level.pixelSize,
       "Cell size in metres: the level above's over a whole number of at least 2; required.", "m",
       Above(0.0));
    io("size", level.size,
       "Width of the level in metres, rounded to cells that end on the level above's cell "
       "centres; required.",
       "m", Above(0.0));
}

template <typename IO> void Fields(IO& io, RepeatedLevelsSpec& repeat) {
    io("count", repeat.count, "Levels nested in the base.", "", AtLeast(1.0));
    io("factor", repeat.factor, "Times finer each level is than the one above.", "", AtLeast(2.0));
    io("shrink", repeat.shrink,
       "Times narrower each level is than the one above; the factor keeps the same number of "
       "cells at every level.",
       "", AtLeast(1.0));
    io("size", repeat.size, "Width of level 1 in metres; required.", "m", Above(0.0));
}

template <typename IO> void Fields(IO& io, RegionSpec& region) {
    io("name", region.name, "Name outputs refer to the region by, unique; required.");
    io("centre_x", region.centreX,
       "Centre along x in surface coordinates, snapped to the base's nearest cell centre; the "
       "longitude in degrees on a body.");
    io("centre_y", region.centreY,
       "Centre along y in surface coordinates, snapped to the base's nearest cell centre; the "
       "latitude in degrees on a body.");
    io("base", region.base,
       "Grid of level 0, which the region lies in; unset for the base's grid. Regions with the "
       "same base may not overlap.");
    io("levels", region.levels, "The levels from 1, each finer than the one above.");
    io("repeat", region.repeat, "The levels as a count, in place of levels.");
    io("upscaling", region.upscaling,
       "Layers adding the detail of the finer levels, such as midpoint_displacement or "
       "spectral_continuation, applied to every level after the base and before the terrain's "
       "layers; they should add nothing at the base's pixel size.");
    io("blend_cells", region.blendCells,
       "Cells along each level's edges over which its heights blend from the level above's, "
       "which they equal on the edge, so meshes meet without gaps; 0 keeps the level's own.");
}

}  // namespace terraingen::config
