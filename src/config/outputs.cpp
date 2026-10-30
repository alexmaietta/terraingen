// SPDX-License-Identifier: BSD-3-Clause

#include "config/outputs.h"

#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "util/overloaded.h"
#include "util/text.h"

#include <cmath>
#include <type_traits>

namespace terraingen::config {

namespace {

/// Rows of a whole-body list's grid above which its spacing is too fine.
constexpr double kMaxWholeBodyRows = 1e9;

/// Whether an output is a feature list over the whole body: a list on a body without an extent or
/// a source's grid, with a pixel size.
bool ListsWholeBody(const OutputSpec& output, const std::optional<RasterGrid>& source,
                    const Surface& surface) {
    const OutputAreaSpec* area = OutputArea(output);
    return ListsFeatures(output) && surface.IsBody() && !source && area->pixelSize &&
           !SurfaceExtent(*area, surface);
}

/// Checks an extent at `path`, whose pairs its fields check.
/// @throws ConfigError at `path` or below it for an incomplete, mixed or empty box.
void CheckExtent(const ExtentSpec& extent, const std::string& path) {
    const bool corners = !extent.min.empty() || !extent.max.empty();
    const bool centered = !extent.center.empty() || !extent.size.empty();
    if (corners && centered) {
        throw ConfigError(path, "give center and size, or min and max, not both");
    }
    if (!corners && !centered) {
        throw ConfigError(path, "needs center and size, or min and max");
    }
    if (corners) {
        if (extent.min.empty() || extent.max.empty()) {
            throw ConfigError(Child(path, extent.min.empty() ? "min" : "max"),
                              "required with " + std::string(extent.min.empty() ? "max" : "min"));
        }
        if (!(extent.max[0] > extent.min[0] && extent.max[1] > extent.min[1])) {
            throw ConfigError(Child(path, "max"), "must lie above min along x and y");
        }
    } else if (extent.center.empty() || extent.size.empty()) {
        throw ConfigError(Child(path, extent.center.empty() ? "center" : "size"),
                          "required with " +
                              std::string(extent.center.empty() ? "size" : "center"));
    }
}

/// Checks the grid `cells` snaps to: at most `kMaxConfigGridSize` cells a side and, on a body,
/// within the latitudes and one turn of longitude.
/// @throws ConfigError at `sizePath` for a grid too large or small, or at `extentPath` off the
/// body.
void CheckAreaGrid(const AreaCells& cells, const std::string& sizePath,
                   const std::string& extentPath, bool body) {
    const double columns = SnappedCells(cells.box.maxX - cells.box.minX, cells.cell);
    const double rows = SnappedCells(cells.box.maxY - cells.box.minY, cells.cell);
    const auto limit = static_cast<double>(kMaxConfigGridSize);
    if (columns > limit || rows > limit) {
        throw ConfigError(sizePath, "gives " + FormatNumber(columns, 17) + "x" +
                                        FormatNumber(rows, 17) + " cells; each side must be " +
                                        CountText(kMinConfigGridSize, kMaxConfigGridSize));
    }
    const RasterGrid grid = RasterGrid::Covering(cells.box, cells.cell);
    CheckGridSize(grid.width, grid.height, sizePath);
    const Bounds2d bounds = grid.Bounds();
    const double slack = kGridSnapTolerance * cells.cell;
    if (body && !(bounds.minY >= -90.0 - slack && bounds.maxY <= 90.0 + slack &&
                  bounds.maxX - bounds.minX <= 360.0 + slack)) {
        throw ConfigError(extentPath,
                          "the grid must lie within latitudes -90 to 90 and one turn of longitude");
    }
}

/// Checks an output's area at `path`: its extent, pixel size and the grid they give.
/// @param listed Whether the output lists features, which may cover the whole body.
/// @throws ConfigError below `path` for an extent of the wrong unit or an invalid one, a missing
/// extent or pixel size without a bounded source, a grid of too many cells or off the body, or a
/// pixel size too fine to list the whole body.
void CheckOutputArea(const OutputAreaSpec& area, const std::string& path, bool listed,
                     const std::optional<RasterGrid>& source, const Surface& surface) {
    const bool body = surface.IsBody();
    if (body ? area.extentM : area.extentDeg) {
        throw ConfigError(Child(path, body ? "extent_m" : "extent_deg"),
                          body ? "on a body give extent_deg, in degrees of longitude and latitude"
                               : "only on a body; give extent_m, in meters, on a plane");
    }
    const std::string extentPath = Child(path, body ? "extent_deg" : "extent_m");
    const std::string pixelPath = Child(path, "pixel_size_m");
    const std::optional<ExtentSpec>& extent = SurfaceExtent(area, surface);
    if (extent) {
        CheckExtent(*extent, extentPath);
    }
    if (listed && body && !source && !extent && area.pixelSize) {
        if (180.0 * surface.UnitLength() / *area.pixelSize > kMaxWholeBodyRows) {
            throw ConfigError(pixelPath, "is too fine to list features over the body");
        }
        return;
    }
    if (!extent && !source) {
        throw ConfigError(extentPath,
                          listed && body
                              ? "required without a raster or chain source, or give pixel_size_m "
                                "alone to list the whole body"
                              : "required without a raster or chain source: the area to cover");
    }
    if (!area.pixelSize && !source) {
        throw ConfigError(pixelPath, "required without a raster or chain source");
    }
    if (extent || area.pixelSize) {
        CheckAreaGrid(*CoveredCells(area, source, surface), area.pixelSize ? pixelPath : extentPath,
                      extentPath, body);
    }
}

void CheckMeshFile(const std::string& file, const std::string& path) {
    if (!IsMeshPath(file) || IsIcqPath(file)) {
        throw ConfigError(path, "must end in .obj, .gltf, .glb or .stl");
    }
}

void CheckGeoTiffOutput(const GeoTiffOutput& output, const std::string& path) {
    if (output.compression == TiffCompression::PackBits) {
        throw ConfigError(Child(path, "compression"),
                          "packbits is read-only; use none, lzw or deflate");
    }
    if (output.tileSize % kTileSizeMultiple != 0) {
        throw ConfigError(Child(path, "tile_size_px"),
                          "must be a multiple of " + std::to_string(kTileSizeMultiple));
    }
}

/// What outputs are checked against.
struct OutputChecks {
    bool body = false;
    OutputNeeds needs{};
};

void CheckOutput(const HeightmapOutput& output, const std::string& path, const OutputChecks&) {
    CheckGeoTiffOutput(output, path);
}

void CheckOutput(const AlbedoOutput& output, const std::string& path, const OutputChecks& checks) {
    if (!checks.needs.albedo) {
        throw ConfigError(path, "the configuration has no albedo");
    }
    CheckGeoTiffOutput(output, path);
}

/// An output that writes the material map or its photometry.
template <typename Output>
constexpr bool MaterialMapOutput =
    std::is_same_v<Output, MaterialsOutput> || std::is_same_v<Output, PhotometryOutput>;

template <typename Output, std::enable_if_t<MaterialMapOutput<Output>, int> = 0>
void CheckOutput(const Output& output, const std::string& path, const OutputChecks& checks) {
    if (!checks.needs.materials) {
        throw ConfigError(path, "the configuration has no materials");
    }
    CheckGeoTiffOutput(output, path);
}

void CheckOutput(const ListOutput& /*output*/, const std::string& /*path*/, const OutputChecks&) {}

void CheckOutput(const BoulderModelsOutput& output, const std::string& path, const OutputChecks&) {
    if (!output.path.empty()) {
        CheckMeshFile(output.path, Child(path, "path"));
    }
}

void CheckOutput(const MeshOutput& output, const std::string& path, const OutputChecks& checks) {
    if (!checks.body) {
        throw ConfigError(path, "a mesh needs a whole body: a sphere, ellipsoid or shape model; "
                                "use terrain_mesh on a plane");
    }
    if (!output.path.empty() && !IsIcqPath(output.path)) {
        CheckMeshFile(output.path, Child(path, "path"));
    }
}

void CheckOutput(const TerrainMeshOutput& output, const std::string& path,
                 const OutputChecks& checks) {
    if (!output.path.empty()) {
        CheckMeshFile(output.path, Child(path, "path"));
    }
    for (std::size_t c = 0; c < output.cutouts.size(); ++c) {
        CheckExtent(output.cutouts[c], Element(Child(path, "cutouts"), c));
    }
    if (!output.siteDeg.empty() && !checks.body) {
        throw ConfigError(Child(path, "site_deg"),
                          "only on a body, where the mesh's frame is centered on the site");
    }
}

void CheckOutput(const ReportOutput& /*output*/, const std::string& /*path*/, const OutputChecks&) {
}

/// Checks that outputs sharing one result agree on it: body meshes of one kind (mesh or ICQ) on
/// their cells and spacing, and terrain meshes over one grid on their cutouts, skirt and site.
/// @throws ConfigError at the first output that disagrees with an earlier one.
void CheckSharedResults(const std::vector<OutputSpec>& outputs, const ConfigSummary& summary) {
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const std::string path = Element("outputs", i);
        for (std::size_t j = 0; j < i; ++j) {
            const auto* mesh = std::get_if<MeshOutput>(&outputs[i]);
            const auto* earlier = std::get_if<MeshOutput>(&outputs[j]);
            if (mesh && earlier && IsIcqPath(mesh->path) == IsIcqPath(earlier->path) &&
                (mesh->cellsPerFace != earlier->cellsPerFace ||
                 mesh->spacing != earlier->spacing)) {
                throw ConfigError(path, "body meshes share one result: give the same "
                                        "cells_per_face and spacing as " +
                                            Element("outputs", j));
            }
            const auto* terrain = std::get_if<TerrainMeshOutput>(&outputs[i]);
            const auto* before = std::get_if<TerrainMeshOutput>(&outputs[j]);
            if (terrain && before && summary.outputGrids[i] == summary.outputGrids[j] &&
                (terrain->cutouts != before->cutouts || terrain->skirt != before->skirt ||
                 terrain->siteDeg != before->siteDeg)) {
                throw ConfigError(path, "terrain meshes over one grid share one result: give the "
                                        "same cutouts, skirt_m and site_deg as " +
                                            Element("outputs", j));
            }
        }
    }
}

}  // namespace

std::optional<Bounds2d> ExtentBox(const ExtentSpec& extent) {
    if (extent.min.size() == 2 && extent.max.size() == 2) {
        return Bounds2d{extent.min[0], extent.min[1], extent.max[0], extent.max[1]};
    }
    if (extent.center.size() == 2 && extent.size.size() == 2) {
        const double halfX = 0.5 * extent.size[0];
        const double halfY = 0.5 * extent.size[1];
        return Bounds2d{extent.center[0] - halfX, extent.center[1] - halfY,
                        extent.center[0] + halfX, extent.center[1] + halfY};
    }
    return std::nullopt;
}

const OutputAreaSpec* OutputArea(const OutputSpec& output) {
    return std::visit(Overloaded{
                          [](const GeoTiffOutput& o) { return &o.area; },
                          [](const ListOutput& o) { return &o.area; },
                          [](const BoulderModelsOutput& o) { return &o.area; },
                          [](const TerrainMeshOutput& o) { return &o.area; },
                          [](const MeshOutput&) -> const OutputAreaSpec* { return nullptr; },
                          [](const ReportOutput&) -> const OutputAreaSpec* { return nullptr; },
                      },
                      output);
}

const std::optional<ExtentSpec>& SurfaceExtent(const OutputAreaSpec& area, const Surface& surface) {
    return surface.IsBody() ? area.extentDeg : area.extentM;
}

bool ListsFeatures(const OutputSpec& output) {
    return HoldsAny<CraterListOutput, BoulderListOutput, BoulderModelsOutput>(output);
}

RasterGrid WholeBodyGrid(const Surface& surface, double spacing) {
    const auto rows =
        static_cast<std::size_t>(std::max(1.0, std::ceil(180.0 * surface.UnitLength() / spacing)));
    return {2 * rows, rows, GeoTransform::NorthUp(-180.0, 90.0, 180.0 / static_cast<double>(rows))};
}

std::optional<AreaCells> CoveredCells(const OutputAreaSpec& area,
                                      const std::optional<RasterGrid>& source,
                                      const Surface& surface) {
    const std::optional<ExtentSpec>& extent = SurfaceExtent(area, surface);
    const std::optional<Bounds2d> box =
        extent ? ExtentBox(*extent)
               : (source ? std::optional<Bounds2d>(source->Bounds()) : std::nullopt);
    const double cell = area.pixelSize ? *area.pixelSize / surface.UnitLength()
                                       : (source ? source->PixelSize() : 0.0);
    if (!box || !(cell > 0.0)) {
        return std::nullopt;
    }
    return AreaCells{*box, cell};
}

std::optional<RasterGrid> OutputGrid(const OutputSpec& output,
                                     const std::optional<RasterGrid>& source,
                                     const Surface& surface) {
    const OutputAreaSpec* area = OutputArea(output);
    if (!area) {
        return std::nullopt;
    }
    if (ListsWholeBody(output, source, surface)) {
        return WholeBodyGrid(surface, *area->pixelSize);
    }
    if (!SurfaceExtent(*area, surface) && !area->pixelSize) {
        return source;
    }
    const std::optional<AreaCells> cells = CoveredCells(*area, source, surface);
    return cells ? std::optional<RasterGrid>(RasterGrid::Covering(cells->box, cells->cell))
                 : std::nullopt;
}

double OutputSpacing(const OutputSpec& output, const std::optional<RasterGrid>& source,
                     const Surface& surface) {
    return ListsWholeBody(output, source, surface) ? *OutputArea(output)->pixelSize : 0.0;
}

std::vector<OutputExtent> CheckOutputs(const std::vector<OutputSpec>& outputs,
                                       const OutputNeeds& needs, const Surface& surface,
                                       ConfigSummary& summary) {
    const OutputChecks checks{surface.IsBody(), needs};
    std::vector<OutputExtent> extents;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const OutputSpec& output = outputs[i];
        const std::string path = Element("outputs", i);
        std::visit([&](const auto& o) { CheckOutput(o, path, checks); }, output);
        std::optional<RasterGrid> grid;
        const bool listed = ListsFeatures(output);
        if (const OutputAreaSpec* area = OutputArea(output)) {
            CheckOutputArea(*area, path, listed, summary.source, surface);
            grid = OutputGrid(output, summary.source, surface);
        }
        summary.outputGrids.push_back(grid);
        if (grid) {
            const double requested = OutputSpacing(output, summary.source, surface);
            const double spacing = listed ? ListedSpacing(*grid, requested, surface)
                                          : std::fmax(requested, grid->Spacing(surface));
            extents.push_back({grid->SurfaceArea(surface), spacing, listed});
        }
    }
    CheckSharedResults(outputs, summary);
    return extents;
}

}  // namespace terraingen::config
