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

/// Checks an extent at `path`, whose pairs its fields check, and returns its box.
/// @throws ConfigError at `path` or below it for an incomplete, mixed or empty box.
Bounds2d CheckExtent(const ExtentSpec& extent, const std::string& path) {
    const bool corners = extent.min || extent.max;
    const bool centered = extent.center || extent.size;
    if (corners && centered) {
        throw ConfigError(path, "give center and size, or min and max, not both");
    }
    if (!corners && !centered) {
        throw ConfigError(path, "needs center and size, or min and max");
    }
    const auto require = [&](const auto& first, const char* firstKey, const auto& second,
                             const char* secondKey) {
        if (!first || !second) {
            throw ConfigError(FieldPath(path, first ? secondKey : firstKey),
                              std::string("required with ") + (first ? firstKey : secondKey));
        }
    };
    if (corners) {
        require(extent.min, "min", extent.max, "max");
        if (!((*extent.max)[0] > (*extent.min)[0] && (*extent.max)[1] > (*extent.min)[1])) {
            throw ConfigError(FieldPath(path, "max"), "must lie above min along x and y");
        }
    } else {
        require(extent.center, "center", extent.size, "size");
    }
    return ExtentBox(extent);
}

/// Whether an output lists features: a crater or boulder list, or boulder models.
bool ListsFeatures(const OutputSpec& output) {
    return HoldsAny<CraterListOutput, BoulderListOutput, BoulderModelsOutput>(output);
}

/// The whole of a body in longitude and latitude, in square cells of at most `spacing` meters
/// along a meridian.
RasterGrid WholeBodyGrid(const Surface& surface, double spacing) {
    const auto rows =
        static_cast<std::size_t>(std::max(1.0, std::ceil(180.0 * surface.UnitLength() / spacing)));
    return {2 * rows, rows, GeoTransform::NorthUp(-180.0, 90.0, 180.0 / static_cast<double>(rows))};
}

/// The grid covering `box` in square cells of side `cell` in the surface's units, checked: at most
/// `kMaxConfigGridSize` cells a side and, on a body, within the latitudes and one turn of
/// longitude.
/// @throws ConfigError at `sizePath` for a grid too large or small, or at `extentPath` off the
/// body.
RasterGrid CheckedGrid(const Bounds2d& box, double cell, const std::string& sizePath,
                       const std::string& extentPath, bool body) {
    const double columns = SnappedCells(box.maxX - box.minX, cell);
    const double rows = SnappedCells(box.maxY - box.minY, cell);
    const auto limit = static_cast<double>(kMaxConfigGridSize);
    if (columns > limit || rows > limit) {
        throw ConfigError(sizePath, "gives " + FormatNumber(columns, 17) + "x" +
                                        FormatNumber(rows, 17) + " cells; each side must be " +
                                        CountText(kMinConfigGridSize, kMaxConfigGridSize));
    }
    const RasterGrid grid = RasterGrid::Covering(box, cell);
    CheckGridSize(grid.width, grid.height, sizePath);
    const Bounds2d bounds = grid.Bounds();
    const double slack = kGridSnapTolerance * cell;
    if (body && !(bounds.minY >= -90.0 - slack && bounds.maxY <= 90.0 + slack &&
                  bounds.maxX - bounds.minX <= 360.0 + slack)) {
        throw ConfigError(extentPath,
                          "the grid must lie within latitudes -90 to 90 and one turn of longitude");
    }
    return grid;
}

/// The extent an area at `path` gives in the surface's unit: `extentDeg` on a body, `extentM` on
/// a plane.
/// @throws ConfigError at the extent given in the other unit.
const std::optional<ExtentSpec>& SurfaceExtent(const OutputAreaSpec& area, const std::string& path,
                                               bool body) {
    if (body ? area.extentM : area.extentDeg) {
        throw ConfigError(FieldPath(path, body ? "extent_m" : "extent_deg"),
                          body ? "on a body give extent_deg, in degrees of longitude and latitude"
                               : "only on a body; give extent_m, in meters, on a plane");
    }
    return body ? area.extentDeg : area.extentM;
}

/// Checks an output's area at `path` and returns the grid it covers.
/// @param listed Whether the output lists features, which may cover the whole body.
/// @throws ConfigError below `path` for an extent of the wrong unit or an invalid one, a missing
/// extent or pixel size without a bounded source, a grid of too many cells or off the body, or a
/// pixel size too fine to list the whole body.
OutputGrid CheckOutputArea(const OutputAreaSpec& area, const std::string& path, bool listed,
                           const std::optional<RasterGrid>& source, const Surface& surface) {
    const bool body = surface.IsBody();
    const std::optional<ExtentSpec>& extent = SurfaceExtent(area, path, body);
    const std::string extentPath = FieldPath(path, body ? "extent_deg" : "extent_m");
    const std::string pixelPath = FieldPath(path, "pixel_size_m");
    OutputGrid output;
    output.listed = listed;
    if (listed && body && !source && !extent && area.pixelSize) {
        if (180.0 * surface.UnitLength() / *area.pixelSize > kMaxWholeBodyRows) {
            throw ConfigError(pixelPath, "is too fine to list features over the body");
        }
        output.grid = WholeBodyGrid(surface, *area.pixelSize);
        output.featureSpacing = *area.pixelSize;
        return output;
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
    if (!extent && !area.pixelSize) {
        output.grid = source;
        return output;
    }
    const Bounds2d box = extent ? CheckExtent(*extent, extentPath) : source->Bounds();
    const double cell =
        area.pixelSize ? *area.pixelSize / surface.UnitLength() : source->PixelSize();
    output.grid = CheckedGrid(box, cell, area.pixelSize ? pixelPath : extentPath, extentPath, body);
    return output;
}

void CheckMeshFile(const std::string& file, const std::string& path) {
    if (!IsMeshPath(file) || IsIcqPath(file)) {
        throw ConfigError(path, "must end in .obj, .gltf, .glb or .stl");
    }
}

void CheckGeoTiffOutput(const GeoTiffOutput& output, const std::string& path) {
    if (output.compression == TiffCompression::PackBits) {
        throw ConfigError(FieldPath(path, "compression"),
                          "packbits is read-only; use none, lzw or deflate");
    }
    if (output.tileSize % kTileSizeMultiple != 0) {
        throw ConfigError(FieldPath(path, "tile_size_px"),
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
        CheckMeshFile(output.path, FieldPath(path, "path"));
    }
}

void CheckOutput(const MeshOutput& output, const std::string& path, const OutputChecks& checks) {
    if (!checks.body) {
        throw ConfigError(path, "a mesh needs a whole body: a sphere, ellipsoid or shape model; "
                                "use terrain_mesh on a plane");
    }
    if (!output.path.empty() && !IsIcqPath(output.path)) {
        CheckMeshFile(output.path, FieldPath(path, "path"));
    }
}

void CheckOutput(const TerrainMeshOutput& output, const std::string& path,
                 const OutputChecks& checks) {
    if (!output.path.empty()) {
        CheckMeshFile(output.path, FieldPath(path, "path"));
    }
    for (std::size_t c = 0; c < output.cutouts.size(); ++c) {
        CheckExtent(output.cutouts[c], ItemPath(FieldPath(path, "cutouts"), c));
    }
    if (output.siteDeg && !checks.body) {
        throw ConfigError(FieldPath(path, "site_deg"),
                          "only on a body, where the mesh's frame is centered on the site");
    }
}

void CheckOutput(const ReportOutput& /*output*/, const std::string& /*path*/, const OutputChecks&) {
}

/// Checks that each output of kind `Output` that shares one result with an earlier one, as
/// `shares(output, i, earlier, j)` says, gives what `agree(output, earlier)` compares the same.
/// @param what The keys that must agree, for the message.
/// @throws ConfigError at the first output that disagrees with an earlier one.
template <typename Output, typename Shares, typename Agree>
void CheckShared(const std::vector<OutputSpec>& outputs, Shares shares, Agree agree,
                 const std::string& what) {
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto* output = std::get_if<Output>(&outputs[i]);
        for (std::size_t j = 0; output && j < i; ++j) {
            const auto* earlier = std::get_if<Output>(&outputs[j]);
            if (earlier && shares(*output, i, *earlier, j) && !agree(*output, *earlier)) {
                throw ConfigError(ItemPath("outputs", i), what + " as " + ItemPath("outputs", j));
            }
        }
    }
}

/// Checks that outputs sharing one result agree on it: body meshes of one kind (mesh or ICQ) on
/// their cells and spacing, and terrain meshes over one grid on their cutouts, skirt and site.
/// @throws ConfigError at the first output that disagrees with an earlier one.
void CheckSharedResults(const std::vector<OutputSpec>& outputs,
                        const std::vector<OutputGrid>& grids) {
    CheckShared<MeshOutput>(
        outputs,
        [](const MeshOutput& mesh, std::size_t, const MeshOutput& earlier, std::size_t) {
            return IsIcqPath(mesh.path) == IsIcqPath(earlier.path);
        },
        [](const MeshOutput& mesh, const MeshOutput& earlier) {
            return mesh.cellsPerFace == earlier.cellsPerFace && mesh.spacing == earlier.spacing;
        },
        "body meshes share one result: give the same cells_per_face and spacing");
    CheckShared<TerrainMeshOutput>(
        outputs,
        [&](const TerrainMeshOutput&, std::size_t i, const TerrainMeshOutput&, std::size_t j) {
            return grids[i].grid == grids[j].grid;
        },
        [](const TerrainMeshOutput& mesh, const TerrainMeshOutput& earlier) {
            return mesh.cutouts == earlier.cutouts && mesh.skirt == earlier.skirt &&
                   mesh.siteDeg == earlier.siteDeg;
        },
        "terrain meshes over one grid share one result: give the same cutouts, skirt_m and "
        "site_deg");
}

}  // namespace

Bounds2d ExtentBox(const ExtentSpec& extent) {
    if (extent.min && extent.max) {
        return {(*extent.min)[0], (*extent.min)[1], (*extent.max)[0], (*extent.max)[1]};
    }
    const std::array<double, 2> center = extent.center.value_or(std::array<double, 2>{});
    const std::array<double, 2> size = extent.size.value_or(std::array<double, 2>{});
    return {center[0] - 0.5 * size[0], center[1] - 0.5 * size[1], center[0] + 0.5 * size[0],
            center[1] + 0.5 * size[1]};
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

std::vector<OutputGrid> CheckOutputs(const std::vector<OutputSpec>& outputs,
                                     const OutputNeeds& needs, const Surface& surface,
                                     const std::optional<RasterGrid>& source) {
    const OutputChecks checks{surface.IsBody(), needs};
    std::vector<OutputGrid> grids;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const OutputSpec& output = outputs[i];
        const std::string path = ItemPath("outputs", i);
        std::visit([&](const auto& o) { CheckOutput(o, path, checks); }, output);
        OutputGrid checked;
        if (const OutputAreaSpec* area = OutputArea(output)) {
            checked = CheckOutputArea(*area, path, ListsFeatures(output), source, surface);
            checked.area = checked.grid->SurfaceArea(surface);
            checked.spacing = checked.listed
                                  ? ListedSpacing(*checked.grid, checked.featureSpacing, surface)
                                  : checked.grid->Spacing(surface);
        }
        grids.push_back(std::move(checked));
    }
    CheckSharedResults(outputs, grids);
    return grids;
}

}  // namespace terraingen::config
