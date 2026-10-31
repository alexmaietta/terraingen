// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/config/generate.h"

#include "config/terrain.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/log.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

/// What `WriteOutputs` writes one output from.
struct OutputContext {
    const GeneratedOutputs& generated;
    /// What was generated over the output's grid; null for an output without one.
    const GeneratedGrid* grid;
};

/// `result`, which an output at `path` writes.
/// @throws std::runtime_error if it was not generated.
template <typename T>
const T& Require(const std::optional<T>& result, const char* what,
                 const std::filesystem::path& path) {
    if (!result) {
        throw std::runtime_error(std::string("No ") + what + " generated for " + path.string());
    }
    return *result;
}

/// @name Bands
/// The bands a GeoTIFF output other than a heightmap writes.
/// @{
const std::vector<Raster<float>>& Bands(const AlbedoOutput&, const GeneratedGrid& grid) {
    return grid.albedo;
}
const std::vector<Raster<float>>& Bands(const MaterialsOutput&, const GeneratedGrid& grid) {
    return grid.materials;
}
const std::vector<Raster<float>>& Bands(const PhotometryOutput&, const GeneratedGrid& grid) {
    return grid.photometry;
}
/// @}

/// Each `Write` writes one output to `path` and returns whether it wrote the file.
/// @throws std::runtime_error if what it writes was not generated or cannot be written.
template <typename Output, std::enable_if_t<std::is_base_of_v<GeoTiffOutput, Output>, int> = 0>
bool Write(const Output& output, const std::filesystem::path& path, const OutputContext& context) {
    const GeoTiffWriteOptions options{output.compression, output.tileSize};
    const GeneratedGrid& grid = *context.grid;
    if constexpr (std::is_same_v<Output, HeightmapOutput>) {
        if (grid.heights.Empty()) {
            throw std::runtime_error("No heights generated for " + path.string());
        }
        WriteGeoTiff(path, grid.heights, context.generated.geoKeys, options);
    } else {
        const std::vector<Raster<float>>& bands = Bands(output, grid);
        if (bands.empty()) {
            throw std::runtime_error("No " + std::string(SpecName<Output>::kName) +
                                     " generated for " + path.string());
        }
        WriteGeoTiff(path, bands, context.generated.geoKeys, options);
    }
    return true;
}

bool Write(const CraterListOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const CraterFeatures& placed = Require(context.grid->craters, "craters", path);
    WriteCraterList(placed.craters, path, output.format, placed.attributes);
    LogInfo("pipeline", "Wrote ", placed.craters.Size(), " craters to ", path.string());
    return true;
}

bool Write(const BoulderListOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const BoulderList& boulders = Require(context.grid->boulders, "boulders", path).boulders;
    WriteBoulderList(boulders, path, output.format);
    LogInfo("pipeline", "Wrote ", boulders.Size(), " boulders to ", path.string());
    return true;
}

bool Write(const BoulderModelsOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const std::vector<BoulderModel>& models =
        Require(context.grid->boulders, "boulders", path).models;
    if (models.empty()) {
        LogWarning("pipeline", "No boulder models to write to ", path.string());
        return false;
    }
    WriteBoulderModels(models, path, output.creaseAngle, context.generated.terrain.Surface(),
                       context.grid->grid.Bounds().Center(), output.instanced);
    LogInfo("pipeline", "Wrote ", models.size(), " boulder models to ", path.string());
    return true;
}

bool Write(const TerrainMeshOutput& /*output*/, const std::filesystem::path& path,
           const OutputContext& context) {
    TriangleMesh mesh = Require(context.grid->mesh, "terrain mesh", path);
    const std::size_t triangles = mesh.triangles.size();
    std::vector<Vec3d> normals = VertexNormals(mesh);
    WriteMesh(path, {std::move(mesh), std::move(normals), {"terrain", {0.5, 0.5, 0.5}}});
    LogInfo("pipeline", "Wrote a terrain mesh of ", triangles, " triangles to ", path.string());
    return true;
}

bool Write(const MeshOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const GeneratedOutputs& generated = context.generated;
    if (IsIcqPath(output.path)) {
        WriteIcq(path, Require(generated.bodyIcq, "ICQ shape model", path));
        LogInfo("pipeline", "Wrote an ICQ shape model of Q = ", output.cellsPerFace, " to ",
                path.string());
        return true;
    }
    const TriangleMesh& mesh = Require(generated.bodyMesh, "body mesh", path);
    ShadedMesh shaded = WithCreases(mesh, kPi);
    WriteMesh(path, {std::move(shaded.mesh), std::move(shaded.normals)});
    LogInfo("pipeline", "Wrote a body mesh of ", mesh.triangles.size(), " triangles to ",
            path.string());
    return true;
}

bool Write(const ReportOutput& /*output*/, const std::filesystem::path& path,
           const OutputContext& context) {
    WriteReport(path, Require(context.generated.report, "report", path));
    LogInfo("pipeline", "Wrote the report to ", path.string());
    return true;
}

/// The path an output names, empty for none.
const std::string& OutputPath(const OutputSpec& spec) {
    return std::visit([](const auto& output) -> const std::string& { return output.path; }, spec);
}

}  // namespace

std::vector<std::filesystem::path> WriteOutputs(const TerrainGenConfig& value,
                                                const GeneratedOutputs& generated,
                                                const std::filesystem::path& directory) {
    const ConfigSpec& config = value.Spec();
    if (generated.outputGrids.size() != config.outputs.size()) {
        throw std::runtime_error("The outputs were generated from another configuration");
    }
    std::vector<std::filesystem::path> written;
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        const OutputSpec& spec = config.outputs[i];
        const std::string& relative = OutputPath(spec);
        if (relative.empty()) {
            continue;
        }
        const std::filesystem::path path = config::Resolve(relative, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        const std::optional<RasterGrid>& grid = generated.outputGrids[i];
        const GeneratedGrid* found = grid ? generated.Find(*grid) : nullptr;
        if (config::OutputArea(spec) && !found) {
            throw std::runtime_error("Nothing generated over the grid of " + path.string());
        }
        const OutputContext context{generated, found};
        if (std::visit([&](const auto& output) { return Write(output, path, context); }, spec)) {
            written.push_back(path);
        }
    }
    return written;
}

}  // namespace terraingen
