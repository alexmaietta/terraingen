#include "terraingen/config/generate.h"

#include "terraingen/config/build.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/pipeline/body_mesh.h"
#include "terraingen/pipeline/height_mesh.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/util/log.h"

#include <iomanip>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace terraingen {

namespace {

/// The projection of a terrain's raster source, or geographic keys on a sphere without one.
GeoKeys KeysOf(const Terrain& terrain) {
    GeoKeys keys = terrain.source->Keys();
    if (terrain.surface.IsBody() && keys.Empty()) {
        keys = GeographicSphereKeys(terrain.surface.Radius());
    }
    return keys;
}

/// What `WriteOutputs` writes from.
struct OutputContext {
    const GeneratedTerrain& generated;
    RasterGrid grid;
};

/// Each `Write` returns whether it wrote the file.
bool Write(const HeightmapOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    WriteGeoTiff(path, context.generated.heights, context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

bool Write(const AlbedoOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    if (context.generated.albedo.empty()) {
        throw std::runtime_error("No albedo to write to " + path.string());
    }
    WriteGeoTiff(path, context.generated.albedo, context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

bool Write(const CraterListOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const CraterFeatures placed = PlacedCraters(context.generated.terrain, context.grid);
    WriteCraterList(placed.craters, path, output.format, placed.attributes);
    LogInfo("pipeline", "Wrote ", placed.craters.Size(), " craters to ", path.string());
    return true;
}

bool Write(const BoulderListOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const std::vector<Boulder> boulders =
        PlacedBoulders(context.generated.terrain, context.grid).boulders;
    WriteBoulderList(boulders, path, output.format);
    LogInfo("pipeline", "Wrote ", boulders.size(), " boulders to ", path.string());
    return true;
}

bool Write(const BoulderModelsOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const std::vector<BoulderModel> models =
        PlacedBoulders(context.generated.terrain, context.grid).models;
    if (models.empty()) {
        LogWarning("pipeline", "No boulder models to write to ", path.string());
        return false;
    }
    const Bounds2d bounds = context.grid.Bounds();
    const Vec2d centre{0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
    WriteBoulderModels(models, path, output.creaseAngle, context.generated.terrain.surface, centre);
    LogInfo("pipeline", "Wrote ", models.size(), " boulder models to ", path.string());
    return true;
}

bool Write(const BodyMeshOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    if (IsIcqPath(path)) {
        WriteIcq(path, BodyIcq(context.generated.terrain, output.cellsPerFace,
                               output.spacing.value_or(0.0)));
        LogInfo("pipeline", "Wrote an ICQ shape model of Q = ", output.cellsPerFace, " to ",
                path.string());
        return true;
    }
    const TriangleMesh mesh =
        BodyMesh(context.generated.terrain, output.cellsPerFace, output.spacing.value_or(0.0));
    ShadedMesh shaded = WithCreases(mesh, std::numbers::pi);
    WriteMesh(path, {std::move(shaded.mesh), std::move(shaded.normals)});
    LogInfo("pipeline", "Wrote a body mesh of ", mesh.triangles.size(), " triangles to ",
            path.string());
    return true;
}

bool Write(const TerrainMeshOutput& /*output*/, const std::filesystem::path& path,
           const OutputContext& context) {
    const Bounds2d bounds = context.grid.Bounds();
    const Vec2d centre{0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
    TriangleMesh mesh =
        HeightMesh(context.generated.heights, context.generated.terrain.surface, centre);
    const std::size_t triangles = mesh.triangles.size();
    std::vector<Vec3d> normals = VertexNormals(mesh);
    WriteMesh(path, {std::move(mesh), std::move(normals), {"terrain", {0.5, 0.5, 0.5}}});
    LogInfo("pipeline", "Wrote a terrain mesh of ", triangles, " triangles to ", path.string());
    return true;
}

}  // namespace

GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory, unsigned threads) {
    GeneratedTerrain generated{BuildTerrain(config, directory)};
    const Terrain& terrain = generated.terrain;
    const Stopwatch stopwatch;
    generated.heights = Evaluate(terrain, terrain.source->Grid(), EvaluationOptions{0.0, threads});
    generated.geoKeys = KeysOf(terrain);
    if (config.albedo) {
        generated.albedo =
            EvaluateAlbedo(BuildAlbedo(config, terrain, directory),
                           RasterGrid::Of(generated.heights), {.spacing = 0.0, .threads = threads});
    }
    generated.seconds = stopwatch.Milliseconds() / 1000.0;
    LogDebug("pipeline", Summary(generated));
    return generated;
}

std::string Summary(const GeneratedTerrain& terrain) {
    std::ostringstream text;
    text << std::setprecision(3) << "Generated a " << terrain.heights.Width() << "x"
         << terrain.heights.Height() << " terrain in " << terrain.seconds << " s";
    return text.str();
}

std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory) {
    const OutputContext context{terrain, RasterGrid::Of(terrain.heights)};
    std::vector<std::filesystem::path> written;
    for (const OutputSpec& spec : config.outputs) {
        const std::string& relative =
            std::visit([](const auto& output) -> const std::string& { return output.path; }, spec);
        const std::filesystem::path path = config::Resolve(relative, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        if (std::visit([&](const auto& output) { return Write(output, path, context); }, spec)) {
            written.push_back(path);
        }
    }
    return written;
}

}  // namespace terraingen
