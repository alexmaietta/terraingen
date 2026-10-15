/// @file
/// @brief Building, generating and writing the terrain a configuration describes.
#include "terraingen/pipeline/run.h"

#include "terraingen/meshes/mesh_io.h"
#include "terraingen/pipeline/detail/build.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/util/log.h"

#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace terraingen {

namespace {

/// The raster source whose grid `source` covers, if any: itself, or a chain's first source.
const GeoTiffSource* GridRaster(const Source& source) {
    if (const auto* chain = dynamic_cast<const SourceChain*>(&source)) {
        return GridRaster(*chain->Sources().front());
    }
    return dynamic_cast<const GeoTiffSource*>(&source);
}

/// The projection of a terrain's raster source, or geographic keys on a sphere without one.
GeoKeys KeysOf(const Terrain& terrain) {
    GeoKeys keys;
    if (const GeoTiffSource* raster = GridRaster(*terrain.source)) {
        keys = raster->Keys();
    }
    if (terrain.surface.IsSphere() && keys.Empty()) {
        keys = GeographicSphereKeys(terrain.surface.Radius());
    }
    return keys;
}

/// What `WriteOutputs` writes from, with the terrain built once for body meshes.
struct OutputContext {
    const TerrainConfig& config;
    const GeneratedTerrain& generated;
    const std::filesystem::path& directory;
    Surface surface;
    std::optional<Terrain> terrain{};
};

/// Each `Write` returns whether it wrote the file.
bool Write(const HeightmapOutput& output, const std::filesystem::path& path,
           OutputContext& context) {
    WriteGeoTiff(path, context.generated.heights, context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

bool Write(const AlbedoOutput& output, const std::filesystem::path& path, OutputContext& context) {
    if (context.generated.albedo.empty()) {
        throw std::runtime_error("No albedo to write to " + path.string());
    }
    WriteGeoTiff(path, context.generated.albedo, context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

bool Write(const CraterListOutput& output, const std::filesystem::path& path,
           OutputContext& context) {
    const TerrainFeatures& features = context.generated.features;
    WriteCraterList(features.craters, path, output.format, features.craterAttributes);
    LogInfo("pipeline", "Wrote ", features.craters.Size(), " craters to ", path.string());
    return true;
}

bool Write(const BoulderListOutput& output, const std::filesystem::path& path,
           OutputContext& context) {
    const std::vector<Boulder>& boulders = context.generated.features.boulders;
    WriteBoulderList(boulders, path, output.format);
    LogInfo("pipeline", "Wrote ", boulders.size(), " boulders to ", path.string());
    return true;
}

bool Write(const BoulderModelsOutput& output, const std::filesystem::path& path,
           OutputContext& context) {
    const std::vector<BoulderModel>& models = context.generated.features.boulderModels;
    if (models.empty()) {
        LogWarning("pipeline", "No boulder models to write to ", path.string());
        return false;
    }
    const Bounds2d bounds = RasterGrid::Of(context.generated.heights).Bounds();
    const Vec2d centre{0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
    WriteBoulderModels(models, path, output.creaseAngle, context.surface, centre);
    LogInfo("pipeline", "Wrote ", models.size(), " boulder models to ", path.string());
    return true;
}

bool Write(const BodyMeshOutput& output, const std::filesystem::path& path,
           OutputContext& context) {
    if (!context.terrain) {
        context.terrain = BuildTerrain(context.config, context.directory);
    }
    const TriangleMesh mesh =
        BodyMesh(*context.terrain, output.cellsPerFace, output.spacing.value_or(0.0));
    if (path.extension() == ".ply") {
        WritePly(path, mesh);
    } else {
        ShadedMesh shaded = WithCreases(mesh, std::numbers::pi);
        WriteGlb(path, shaded.mesh, shaded.normals);
    }
    LogInfo("pipeline", "Wrote a body mesh of ", mesh.triangles.size(), " triangles to ",
            path.string());
    return true;
}

}  // namespace

Terrain BuildTerrain(const TerrainConfig& config, const std::filesystem::path& directory) {
    ValidateConfig(config, directory);
    return BuildValidTerrain(config, directory);
}

GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory, unsigned threads) {
    const Terrain terrain = BuildTerrain(config, directory);
    const Stopwatch stopwatch;
    EvaluatedTerrain evaluated =
        Evaluate(terrain, terrain.source->Grid(), EvaluationOptions{0.0, threads});
    GeneratedTerrain generated{std::move(evaluated.heights), std::move(evaluated.features),
                               KeysOf(terrain)};
    if (config.albedo) {
        generated.albedo =
            EvaluateAlbedo(BuildValidAlbedo(*config.albedo, terrain, directory),
                           RasterGrid::Of(generated.heights), {.spacing = 0.0, .threads = threads});
    }
    generated.seconds = stopwatch.Milliseconds() / 1000.0;
    LogDebug("pipeline", Summary(generated));
    return generated;
}

std::string Summary(const GeneratedTerrain& terrain) {
    std::ostringstream text;
    text << "Generated a " << terrain.heights.Width() << "x" << terrain.heights.Height()
         << " terrain with " << terrain.features.craters.Size() << " craters and "
         << terrain.features.boulders.size() << " boulders in " << terrain.seconds << " s";
    return text.str();
}

std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory) {
    OutputContext context{config, terrain, directory, BuildSurface(config.surface)};
    std::vector<std::filesystem::path> written;
    for (const OutputSpec& spec : config.outputs) {
        const std::string& relative =
            std::visit([](const auto& output) -> const std::string& { return output.path; }, spec);
        const std::filesystem::path path = Resolve(relative, directory);
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
