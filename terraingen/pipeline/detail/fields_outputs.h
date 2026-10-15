/// @file
/// @brief Internal: the JSON fields of the outputs.
#pragma once

#include "terraingen/pipeline/config.h"
#include "terraingen/pipeline/detail/fields_common.h"

#include <string_view>

namespace terraingen::config {

/// The fields of heightmap and albedo outputs.
template <typename IO> void GeoTiffOutputFields(IO& io, GeoTiffOutput& output) {
    io("path", output.path, OutputPathHelp("GeoTIFF file to write"));
    io("compression", output.compression,
       "Compression of the file; compressed files use the floating-point predictor.");
    io("tile_size", output.tileSize,
       "Width and height of the file's tiles in pixels; a positive multiple of 16.", "px");
}

/// The fields of crater and boulder list outputs.
template <typename IO> void ListOutputFields(IO& io, ListOutput& output) {
    io("path", output.path, OutputPathHelp("File to write"));
    io("format", output.format,
       "Format of the file; when not given, .json paths are written as JSON and others as CSV.");
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
        "Writes the boulders placed as 3D models on the ground, as a glTF scene (.glb) or one "
        "merged PLY mesh (.ply).";
};
template <typename IO> void Fields(IO& io, BoulderModelsOutput& output) {
    io("path", output.path, OutputPathHelp("File to write, ending in .glb or .ply"));
    io("crease_angle", output.creaseAngle,
       "Angle in degrees, from 0 to 180: in .glb files, vertices are split where faces meet at "
       "more than it, giving sharp edges; .ply files ignore it.",
       "deg");
}

template <> struct SpecName<AlbedoOutput> {
    static constexpr std::string_view kName = "albedo";
    static constexpr std::string_view kDescription =
        "Writes the albedo as a float32 GeoTIFF of one band or three (red, green, blue).";
};
template <typename IO> void Fields(IO& io, AlbedoOutput& output) {
    GeoTiffOutputFields(io, output);
}

template <> struct SpecName<BodyMeshOutput> {
    static constexpr std::string_view kName = "body_mesh";
    static constexpr std::string_view kDescription =
        "Writes a mesh of the whole body on a sphere, from six cube faces, as glTF (.glb) or PLY "
        "(.ply).";
};
template <typename IO> void Fields(IO& io, BodyMeshOutput& output) {
    io("path", output.path, OutputPathHelp("File to write, ending in .glb or .ply"));
    io("cells_per_face", output.cellsPerFace,
       "Quads along each edge of the six cube faces, 1 to 4096; more give a finer mesh, with two "
       "triangles per quad.");
    io("spacing", output.spacing,
       "Sample spacing in metres the heights are evaluated at, which sets the finest features "
       "kept; unset uses a quad's side at a face's centre.",
       "m");
}

}  // namespace terraingen::config
