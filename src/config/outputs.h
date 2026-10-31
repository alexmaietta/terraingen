// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief A configuration's outputs: their specs, fields, areas and grids, and checks.
#pragma once

#include "config/body.h"
#include "config/layer_context.h"

#include "terraingen/config/validate.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/io/io_fields.h"
#include "terraingen/pipeline/materials_fields.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster_fields.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace terraingen {

/// A box in surface coordinates, given by its `center` and `size` or by its `min` and `max`
/// corners, each an [x, y] pair. Surface coordinates are meters on a plane, and longitude and
/// latitude in degrees on a body.
struct ExtentSpec {
    std::optional<std::array<double, 2>> center{};  ///< [x, y] of the box's center.
    std::optional<std::array<double, 2>> size{};    ///< [width, height], both positive.
    std::optional<std::array<double, 2>> min{};     ///< [x, y] of the lower corner.
    /// [x, y] of the upper corner, above `min` along both axes.
    std::optional<std::array<double, 2>> max{};

    bool operator==(const ExtentSpec& other) const {
        return center == other.center && size == other.size && min == other.min && max == other.max;
    }
    bool operator!=(const ExtentSpec& other) const {
        return !(*this == other);
    }
};

/// The area an output covers and its resolution, snapped to a grid when its output is checked.
struct OutputAreaSpec {
    /// Cell size in meters; on a body, meters along a meridian, the same in degrees of longitude.
    std::optional<double> pixelSize{};
    std::optional<ExtentSpec> extentM{};    ///< The extent on a plane, in meters.
    std::optional<ExtentSpec> extentDeg{};  ///< The extent on a body, in degrees.
};

/// A list file to write; the format follows the extension (.json or else CSV) unless given.
///
/// The list covers `area`, or the source's grid without an extent, or else the whole body on a
/// body; features are listed as a heightmap of `area.pixelSize` applies them.
struct ListOutput {
    std::string path;  ///< The file to write; empty to only return the list.
    ListFormat format = ListFormat::Csv;
    OutputAreaSpec area{};
};

/// A float32 GeoTIFF file to write over `area`, with the source's projection.
struct GeoTiffOutput {
    std::string path;  ///< The file to write; empty to only return the rasters.
    TiffCompression compression = TiffCompression::Lzw;  ///< None, Lzw or Deflate.
    std::uint32_t tileSize = 256;                        ///< Pixels per tile side.
    OutputAreaSpec area{};
};

/// Output "crater_list": writes the generated and manual craters.
struct CraterListOutput : ListOutput {};

/// Output "heightmap": writes the generated heights.
struct HeightmapOutput : GeoTiffOutput {};

/// Output "boulder_list": writes every boulder the layers placed, models and relief alike.
struct BoulderListOutput : ListOutput {};

/// Output "boulder_models": writes the boulders placed as 3D models (`BoulderModelThreshold`) on
/// the ground beneath them, as `WriteBoulderModels` does, over the area a list would cover.
struct BoulderModelsOutput {
    std::string path;           ///< The file to write; empty to only return the models.
    double creaseAngle = 60.0;  ///< Degrees between faces above which vertices are split.
    OutputAreaSpec area{};      ///< As `ListOutput::area`.
    bool instanced = true;      ///< Shared shapes written once and drawn per boulder.
};

/// Output "albedo": writes the albedo with one band, or three (red, green, blue).
struct AlbedoOutput : GeoTiffOutput {};

/// Output "mesh": the whole body, as a mesh (`BodyMesh`) with smooth normals in any format
/// `WriteMesh` writes, or as an ICQ shape model (`BodyIcq`) for an .icq path.
struct MeshOutput {
    std::string path;                 ///< The file to write; empty to only return the mesh.
    std::size_t cellsPerFace = 128;   ///< Quads along each edge of the six cube faces.
    std::optional<double> spacing{};  ///< Sample spacing in meters; unset for a quad's side.
};

/// Output "terrain_mesh": the heights over `area` as a mesh (`BuildMesh`) with smooth normals, in
/// any format `WriteMesh` writes and in the frame boulder models are written in.
struct TerrainMeshOutput {
    std::string path;  ///< The file to write; empty to only return the mesh.
    OutputAreaSpec area{};
    /// Boxes in surface coordinates whose quads are left out, such as where a finer mesh covers
    /// the ground; a quad is left out when all four corners lie in one.
    std::vector<ExtentSpec> cutouts{};
    /// Depth in meters of the skirt hung below every boundary edge, inner ones included, to hide
    /// gaps between meshes; 0 for none.
    double skirt = 0.0;
    /// [longitude, latitude] in degrees of the frame's site on a body (`BuildMesh`); unset for the
    /// grid's center.
    std::optional<std::array<double, 2>> siteDeg{};
};

/// Output "materials": the share of each material class, one band per `MaterialClass` in its
/// order.
struct MaterialsOutput : GeoTiffOutput {};

/// Output "photometry": the Hapke albedo, b, c and roughness the materials mix to, in four bands.
struct PhotometryOutput : GeoTiffOutput {};

/// Output "report": a report of what the configuration generates (`GeneratedOutputs::report`),
/// written as JSON (`ReportToJson`).
struct ReportOutput {
    std::string path;  ///< The file to write; empty to only return the report.
};

/// An output, named in JSON by its "type".
using OutputSpec = std::variant<HeightmapOutput, CraterListOutput, BoulderListOutput,
                                BoulderModelsOutput, AlbedoOutput, MeshOutput, TerrainMeshOutput,
                                MaterialsOutput, PhotometryOutput, ReportOutput>;

template <typename IO> void Fields(IO& io, ExtentSpec& extent) {
    io("center", extent.center, "[x, y] of the box's center; give it with size.");
    io("size", extent.size, "[width, height] of the box; give it with center.", "", Above(0.0));
    io("min", extent.min, "[x, y] of the box's lower corner; give it with max.");
    io("max", extent.max, "[x, y] of the box's upper corner, above min; give it with min.");
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
       "Box the output covers on a plane in meters, snapped outward from its min corner to whole "
       "cells; unset for the source's extent, required without a raster or chain source.");
    io("extent_deg", area.extentDeg,
       "As extent_m on a body, in degrees of longitude (x) and latitude (y).");
}

/// The fields of heightmap and albedo outputs.
template <typename IO> void GeoTiffOutputFields(IO& io, GeoTiffOutput& output) {
    io("path", output.path, config::OutputPathHelp("GeoTIFF file to write"));
    AreaFields(io, output.area, "Cell size in meters of the file");
    io("compression", output.compression,
       "Compression of the file; compressed files use the floating-point predictor.");
    io("tile_size_px", output.tileSize,
       "Width and height of the file's tiles in pixels; a multiple of " +
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
    io("path", output.path, config::OutputPathHelp("File to write"));
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
       config::OutputPathHelp("File to write, ending in .glb, .gltf, .obj or .stl"));
    io("crease_angle_deg", output.creaseAngle,
       "Angle in degrees: vertices are split where faces meet at more than it, giving sharp "
       "edges.",
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
        "STL (.stl), or a Gaskell ICQ shape model in kilometers (.icq), whose Q is "
        "cells_per_face.";
};
template <typename IO> void Fields(IO& io, MeshOutput& output) {
    io("path", output.path,
       config::OutputPathHelp("File to write, ending in .glb, .gltf, .obj, .stl or .icq"));
    io("cells_per_face", output.cellsPerFace,
       "Quads along each edge of the six cube faces; more give a finer mesh, with two triangles "
       "per quad.",
       "", CubeFaceCellsRange());
    io("spacing_m", output.spacing,
       "Sample spacing in meters the heights are evaluated at, which sets the finest features "
       "kept; unset uses a quad's side at a face's center.",
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
       config::OutputPathHelp("File to write, ending in .glb, .gltf, .obj or .stl"));
    AreaFields(io, output.area, "Cell size in meters of the mesh, the spacing of its vertices");
    io("cutouts", output.cutouts,
       "Boxes in surface coordinates whose quads, all four corners inside one, are left out, such "
       "as where a finer mesh covers the ground.");
    io("skirt_m", output.skirt,
       "Depth in meters of a skirt hung below every boundary edge, around cutouts too, hiding "
       "gaps between meshes; 0 for none.",
       "m", AtLeast(0.0));
    io("site_deg", output.siteDeg,
       "[longitude, latitude] in degrees of the site on a body the mesh's frame is centered on; "
       "unset for the grid's center. Only on a body.",
       "deg");
}

template <> struct SpecName<ReportOutput> {
    static constexpr std::string_view kName = "report";
    static constexpr std::string_view kDescription =
        "A JSON report of what the configuration generates: height, slope, crater bin and "
        "boulder statistics per evaluated grid, per-layer timings, backend, threads, seeds and "
        "version.";
};
template <typename IO> void Fields(IO& io, ReportOutput& output) {
    io("path", output.path, config::OutputPathHelp("File to write the report to"));
}

namespace config {

/// The box a checked extent describes: its `min` and `max` corners, or else `center` - `size` / 2
/// to `center` + `size` / 2.
Bounds2d ExtentBox(const ExtentSpec& extent);

/// The area an output covers, or null for one without an area, a body mesh or a report.
const OutputAreaSpec* OutputArea(const OutputSpec& output);

/// Whether any of `outputs` is of the kinds `Kinds`.
template <typename... Kinds> bool HasOutput(const std::vector<OutputSpec>& outputs) {
    return std::any_of(outputs.begin(), outputs.end(),
                       [](const OutputSpec& output) { return HoldsAny<Kinds...>(output); });
}

/// What a configuration offers its outputs.
struct OutputNeeds {
    bool albedo = false;     ///< Whether the configuration has an albedo.
    bool materials = false;  ///< Whether the configuration has a material map.
};

/// Checks each output and the grid its area snaps to, and returns one record per output: its
/// extent snapped outward to whole cells of its pixel size (`RasterGrid::Covering`), or of
/// `source`'s, or `source` itself without either, and for a feature list on a body without an
/// extent or source the whole body at its pixel size.
/// @param source The source's grid; none without a raster or chain source.
/// @throws ConfigError under "outputs" for an invalid output.
std::vector<OutputGrid> CheckOutputs(const std::vector<OutputSpec>& outputs,
                                     const OutputNeeds& needs, const Surface& surface,
                                     const std::optional<RasterGrid>& source);

}  // namespace config

}  // namespace terraingen
