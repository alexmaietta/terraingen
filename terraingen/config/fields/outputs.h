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

template <typename IO> void Fields(IO& io, ExtentSpec& extent) {
    io("center", extent.center, "[x, y] of the box's center; give it with size.");
    io("size", extent.size, "[width, height] of the box, each positive; give it with center.", "",
       Above(0.0));
    io("min", extent.min, "[x, y] of the box's lower corner; give it with max.");
    io("max", extent.max, "[x, y] of the box's upper corner, above min; give it with min.");
}

/// The description of a path that is optional: `file` is what it names.
inline std::string OptionalOutputPathHelp(std::string_view file) {
    return OutputPathHelp(std::string(file) + "; unset to only return the result, writing nothing");
}

/// The fields of the area an output covers: its pixel size and its extent.
/// @param what What the pixel size is, such as "Cell size in meters of the file".
template <typename IO> void AreaFields(IO& io, OutputAreaSpec& area, std::string_view what) {
    io("pixel_size_m", area.pixelSize,
       std::string(what) +
           "; on a body, meters along a meridian, the same in degrees of longitude. Unset for the "
           "source's cell size; required without a raster or chain source.",
       "m", Above(0.0));
    io("extent_m", area.extentM,
       "Box the output covers on a plane, in meters: {\"center\": [x, y], \"size\": [w, h]} or "
       "{\"min\": [x, y], \"max\": [x, y]}. The grid starts at its min corner and is snapped "
       "outward to whole cells of pixel_size_m, sampled at cell centers. Unset for the source's "
       "extent; required without a raster or chain source.");
    io("extent_deg", area.extentDeg,
       "As extent_m on a body, in degrees of longitude (x) and latitude (y).");
}

/// The fields of heightmap and albedo outputs.
template <typename IO> void GeoTiffOutputFields(IO& io, GeoTiffOutput& output) {
    io("path", output.path, OptionalOutputPathHelp("GeoTIFF file to write"));
    AreaFields(io, output.area, "Cell size in meters of the file");
    io("compression", output.compression,
       "Compression of the file; compressed files use the floating-point predictor.");
    io("tile_size", output.tileSize,
       "Width and height of the file's tiles in pixels; a positive multiple of " +
           std::to_string(kTileSizeMultiple) + ".",
       "px", AtLeast(kTileSizeMultiple));
}

/// The fields of where features are listed: the area, whose pixel size is the sample spacing the
/// features are listed at, as a heightmap of that pixel size applies them.
template <typename IO> void FeatureAreaFields(IO& io, OutputAreaSpec& area) {
    AreaFields(io, area,
               "Sample spacing in meters the features are listed at, as a heightmap of that "
               "pixel size applies them, and the cell size of their grid; without an extent or "
               "a raster source, a list on a body covers the whole body at it");
}

/// The fields of crater and boulder list outputs.
template <typename IO> void ListOutputFields(IO& io, ListOutput& output) {
    io("path", output.path, OptionalOutputPathHelp("File to write"));
    io("format", output.format,
       "Format of the file; when not given, .json paths are written as JSON and others as CSV.");
    FeatureAreaFields(io, output.area);
}

template <> struct SpecName<HeightmapOutput> {
    static constexpr std::string_view kName = "heightmap";
    static constexpr std::string_view kDescription =
        "Writes the generated heights as a float32 GeoTIFF, with a raster source's projection.";
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
    io("path", output.path,
       OptionalOutputPathHelp("File to write, ending in .glb, .gltf, .obj or .stl"));
    io("crease_angle_deg", output.creaseAngle,
       "Angle in degrees, from " + CountText(0, static_cast<std::size_t>(kMaxCreaseAngle)) +
           ": vertices are split where faces meet at more than it, giving sharp edges.",
       "deg", Between(0.0, kMaxCreaseAngle));
    io("instanced", output.instanced,
       "Writes each shared shape once and draws it per boulder (glTF instances, OBJ objects); "
       "false writes every boulder as its own placed mesh, for tools without instancing.");
    FeatureAreaFields(io, output.area);
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
       OptionalOutputPathHelp("File to write, ending in .glb, .gltf, .obj, .stl or .icq"));
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
    io("path", output.path,
       OptionalOutputPathHelp("File to write, ending in .glb, .gltf, .obj or .stl"));
    AreaFields(io, output.area, "Cell size in meters of the mesh, the spacing of its vertices");
    io("cutouts", output.cutouts,
       "Boxes in surface coordinates (meters on a plane, degrees on a body) whose quads are left "
       "out, a quad when all four corners lie in one, such as where a finer mesh covers the "
       "ground: each {\"min\": [x, y], \"max\": [x, y]} or {\"center\": [x, y], \"size\": [w, "
       "h]}.");
    io("skirt_m", output.skirt,
       "Depth in meters of a skirt hung below every boundary edge, around cutouts too, hiding "
       "gaps between meshes; 0 for none.",
       "m", AtLeast(0.0));
    io("site_deg", output.siteDeg,
       "[longitude, latitude] in degrees of the site on a body the mesh's frame is centered on; "
       "unset for the grid's center. Only on a body.");
}

template <> struct SpecName<ReportOutput> {
    static constexpr std::string_view kName = "report";
    static constexpr std::string_view kDescription =
        "A report of what the configuration generates; accepted and checked, not yet produced.";
};
template <typename IO> void Fields(IO& io, ReportOutput& output) {
    io("path", output.path, OptionalOutputPathHelp("File to write the report to"));
}

}  // namespace terraingen::config
