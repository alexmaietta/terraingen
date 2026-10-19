/// @file
/// @brief The JSON fields of the outputs.
#pragma once

#include "terraingen/config/config.h"
#include "terraingen/config/fields/common.h"
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

/// The description of an output's region: `what` it covers.
inline std::string RegionHelp(std::string_view what) {
    return "Grid of square cells " + std::string(what) + "; unset for the base's grid";
}

/// The fields of heightmap and albedo outputs.
template <typename IO> void GeoTiffOutputFields(IO& io, GeoTiffOutput& output) {
    io("path", output.path, OutputPathHelp("GeoTIFF file to write"));
    io("region", output.region, RegionHelp("the file covers") + ".");
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
       "applies them; unset for the region's pixel size. A whole-body list needs it.",
       "m", Above(0.0));
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
    io("crease_angle", output.creaseAngle,
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
}

}  // namespace terraingen::config
