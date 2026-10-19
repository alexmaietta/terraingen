#include "terraingen/config/generate.h"

#include "terraingen/config/build.h"
#include "terraingen/config/json.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/pipeline/body_mesh.h"
#include "terraingen/pipeline/height_mesh.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

/// The projection of a terrain's raster base, or geographic keys on a body without one.
GeoKeys KeysOf(const Terrain& terrain) {
    GeoKeys keys = terrain.source ? terrain.source->Keys() : GeoKeys{};
    if (terrain.surface.IsBody() && keys.Empty()) {
        keys = GeographicSphereKeys(terrain.surface.Radius());
    }
    return keys;
}

/// The grid of the terrain's base, or none without one.
std::optional<RasterGrid> BaseGrid(const Terrain& terrain) {
    return terrain.source ? std::optional<RasterGrid>(terrain.source->Grid()) : std::nullopt;
}

/// The sample spacing in metres an output lists features at; 0 for its grid's.
double SpacingOf(const OutputSpec& output) {
    return std::visit(
        [](const auto& o) {
            if constexpr (config::FeatureOutput<std::decay_t<decltype(o)>>) {
                return o.spacing.value_or(0.0);
            }
            return 0.0;
        },
        output);
}

/// The boulders placed over one grid at one spacing, shared by the outputs that list them.
struct PlacedBoulderSet {
    RasterGrid grid;
    double spacing = 0.0;
    BoulderFeatures features;
};

/// One step of generating a terrain and the time it took over every grid.
struct StepTime {
    std::string name;
    double milliseconds = 0.0;
};

/// A sample spacing in metres to three significant figures, or in kilometres from 1 km.
std::string FormatSpacing(double metres) {
    std::ostringstream text;
    text << std::setprecision(3);
    if (metres >= 1000.0) {
        text << metres / 1000.0 << " km";
    } else {
        text << metres << " m";
    }
    return text.str();
}

/// Logs the time of each step that ran and its share of `total` milliseconds.
void LogStepTimes(const std::vector<StepTime>& steps, std::size_t grids, double total) {
    std::size_t width = 0;
    for (const StepTime& step : steps) {
        width = std::max(width, step.name.size());
    }
    LogInfo("pipeline", "Time per step over ", grids, grids == 1 ? " grid" : " grids", ", of ",
            FormatDuration(total), ":");
    for (const StepTime& step : steps) {
        if (step.milliseconds <= 0.0) {
            continue;
        }
        std::ostringstream line;
        line << "  " << std::left << std::setw(static_cast<int>(width)) << step.name << std::right
             << std::setw(10) << FormatDuration(step.milliseconds) << std::setw(5)
             << std::llround(100.0 * step.milliseconds / std::fmax(total, 1e-9)) << "%";
        LogInfo("pipeline", line.str());
    }
}

/// What `WriteOutputs` writes one output from.
struct OutputContext {
    const GeneratedTerrain& generated;
    std::optional<RasterGrid> grid;         ///< The grid the output covers; none for a mesh.
    double spacing = 0.0;                   ///< The output's sample spacing; 0 for its grid's.
    const TerrainConfig* config = nullptr;  ///< The configuration the outputs come from.
    std::vector<PlacedBoulderSet>* placed = nullptr;  ///< Boulders placed for earlier outputs.

    /// The boulders placed over the output's grid, placed once for every output that lists them.
    const BoulderFeatures& Boulders() const {
        for (const PlacedBoulderSet& set : *placed) {
            if (set.grid == *grid && set.spacing == spacing) {
                return set.features;
            }
        }
        placed->push_back({*grid, spacing, PlacedBoulders(generated.terrain, *grid, spacing)});
        return placed->back().features;
    }

    /// The heights and albedo generated over the output's grid.
    /// @throws std::runtime_error if they were not generated.
    const GeneratedGrid& Generated(const std::filesystem::path& path) const {
        const GeneratedGrid* found = grid ? generated.Find(*grid) : nullptr;
        if (!found) {
            throw std::runtime_error("No heights generated for " + path.string());
        }
        return *found;
    }

    /// The centre of the output's grid, or the origin for a whole-body list.
    Vec2d Centre() const {
        const Bounds2d bounds = grid->Bounds();
        return {0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
    }
};

/// Each `Write` returns whether it wrote the file.
bool Write(const HeightmapOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    WriteGeoTiff(path, context.Generated(path).heights, context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

bool Write(const AlbedoOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const std::vector<Raster<float>>& albedo = context.Generated(path).albedo;
    if (albedo.empty()) {
        throw std::runtime_error("No albedo to write to " + path.string());
    }
    WriteGeoTiff(path, albedo, context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

/// The material rasters generated over the output's grid.
/// @throws std::runtime_error if they were not generated.
const std::vector<Raster<float>>& MaterialsOf(const OutputContext& context,
                                              const std::filesystem::path& path) {
    const std::vector<Raster<float>>& materials = context.Generated(path).materials;
    if (materials.empty()) {
        throw std::runtime_error("No materials to write to " + path.string());
    }
    return materials;
}

bool Write(const MaterialsOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    WriteGeoTiff(path, MaterialsOf(context, path), context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

bool Write(const PhotometryOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const std::vector<Raster<float>>& materials = MaterialsOf(context, path);
    const MaterialPhotometry photometry =
        config::BuildPhotometry(context.config->materials->photometry);
    WriteGeoTiff(path, EvaluatePhotometry(materials, photometry), context.generated.geoKeys,
                 {.compression = output.compression, .tileSize = output.tileSize});
    return true;
}

bool Write(const CraterListOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const Stopwatch stopwatch;
    const CraterFeatures placed =
        PlacedCraters(context.generated.terrain, *context.grid, context.spacing);
    WriteCraterList(placed.craters, path, output.format, placed.attributes);
    LogInfo("pipeline", "Wrote ", placed.craters.Size(), " craters to ", path.string(), " in ",
            FormatDuration(stopwatch.Milliseconds()));
    return true;
}

bool Write(const BoulderListOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const Stopwatch stopwatch;
    const std::vector<Boulder>& boulders = context.Boulders().boulders;
    WriteBoulderList(boulders, path, output.format);
    LogInfo("pipeline", "Wrote ", boulders.size(), " boulders to ", path.string(), " in ",
            FormatDuration(stopwatch.Milliseconds()));
    return true;
}

bool Write(const BoulderModelsOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const Stopwatch stopwatch;
    const std::vector<BoulderModel>& models = context.Boulders().models;
    if (models.empty()) {
        LogWarning("pipeline", "No boulder models to write to ", path.string());
        return false;
    }
    WriteBoulderModels(models, path, output.creaseAngle, context.generated.terrain.surface,
                       context.Centre(), output.instanced);
    LogInfo("pipeline", "Wrote ", models.size(), " boulder models to ", path.string(), " in ",
            FormatDuration(stopwatch.Milliseconds()));
    return true;
}

bool Write(const MeshOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const Stopwatch stopwatch;
    if (IsIcqPath(path)) {
        WriteIcq(path, BodyIcq(context.generated.terrain, output.cellsPerFace,
                               output.spacing.value_or(0.0)));
        LogInfo("pipeline", "Wrote an ICQ shape model of Q = ", output.cellsPerFace, " to ",
                path.string(), " in ", FormatDuration(stopwatch.Milliseconds()));
        return true;
    }
    const TriangleMesh mesh =
        BodyMesh(context.generated.terrain, output.cellsPerFace, output.spacing.value_or(0.0));
    ShadedMesh shaded = WithCreases(mesh, std::numbers::pi);
    WriteMesh(path, {std::move(shaded.mesh), std::move(shaded.normals)});
    LogInfo("pipeline", "Wrote a body mesh of ", mesh.triangles.size(), " triangles to ",
            path.string(), " in ", FormatDuration(stopwatch.Milliseconds()));
    return true;
}

bool Write(const TerrainMeshOutput& /*output*/, const std::filesystem::path& path,
           const OutputContext& context) {
    const Stopwatch stopwatch;
    TriangleMesh mesh = HeightMesh(context.Generated(path).heights,
                                   context.generated.terrain.surface, context.Centre());
    const std::size_t triangles = mesh.triangles.size();
    std::vector<Vec3d> normals = VertexNormals(mesh);
    WriteMesh(path, {std::move(mesh), std::move(normals), {"terrain", {0.5, 0.5, 0.5}}});
    LogInfo("pipeline", "Wrote a terrain mesh of ", triangles, " triangles to ", path.string(),
            " in ", FormatDuration(stopwatch.Milliseconds()));
    return true;
}

/// Whether an output writes the material map or its photometry.
bool MapsMaterials(const OutputSpec& output) {
    return std::holds_alternative<MaterialsOutput>(output) ||
           std::holds_alternative<PhotometryOutput>(output);
}

/// Whether an output writes the heights, albedo or materials over its grid.
bool WritesGrid(const OutputSpec& output) {
    return std::holds_alternative<HeightmapOutput>(output) ||
           std::holds_alternative<AlbedoOutput>(output) ||
           std::holds_alternative<TerrainMeshOutput>(output) || MapsMaterials(output);
}

}  // namespace

const GeneratedGrid* GeneratedTerrain::Find(const RasterGrid& grid) const {
    for (const GeneratedGrid& generated : grids) {
        if (RasterGrid::FromRaster(generated.heights) == grid) {
            return &generated;
        }
    }
    return nullptr;
}

GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory, unsigned threads,
                                 std::optional<Backend> backend) {
    GeneratedTerrain generated{BuildTerrain(config, directory)};
    generated.backend = ResolveBackend(backend.value_or(DefaultBackend()));
    const Terrain& terrain = generated.terrain;
    const Stopwatch stopwatch;
    const std::optional<RasterGrid> base = BaseGrid(terrain);
    std::vector<RasterGrid> grids;
    for (const OutputSpec& output : config.outputs) {
        if (WritesGrid(output)) {
            const RasterGrid grid = *config::OutputGrid(output, base, terrain.surface);
            if (std::find(grids.begin(), grids.end(), grid) == grids.end()) {
                grids.push_back(grid);
            }
        }
    }
    std::vector<StepTime> steps;
    for (std::size_t n = 0; n < config.layers.size(); ++n) {
        steps.push_back({"layer " + std::to_string(n + 1) + " " + LayerType(config.layers[n])});
    }
    const std::size_t albedoStep = steps.size();
    steps.push_back({"albedo"});
    steps.push_back({"materials"});
    const auto timed = [&](std::size_t step, double milliseconds) {
        steps[step].milliseconds += milliseconds;
        LogInfo("pipeline", "  ", steps[step].name, " in ", FormatDuration(milliseconds));
    };
    const auto generatedOver = [&](const RasterGrid& grid) -> GeneratedGrid& {
        for (GeneratedGrid& existing : generated.grids) {
            if (RasterGrid::FromRaster(existing.heights) == grid) {
                return existing;
            }
        }
        LogInfo("pipeline", "Grid ", generated.grids.size() + 1, " of ", grids.size(), ": ",
                grid.width, "x", grid.height, " at ", FormatSpacing(grid.Spacing(terrain.surface)));
        EvaluationOptions options{0.0, threads, generated.backend};
        options.onLayer = timed;
        generated.grids.push_back({Evaluate(terrain, grid, options), {}});
        return generated.grids.back();
    };
    std::optional<Albedo> albedo;
    for (const OutputSpec& output : config.outputs) {
        if (!WritesGrid(output)) {
            continue;
        }
        const RasterGrid grid = *config::OutputGrid(output, base, terrain.surface);
        GeneratedGrid& target = generatedOver(grid);
        if (std::holds_alternative<AlbedoOutput>(output) && target.albedo.empty()) {
            const Stopwatch albedoTime;
            if (!albedo) {
                albedo = BuildAlbedo(config, terrain, directory);
            }
            const TerrainGround ground(terrain, terrain.layers.size());
            target.albedo = EvaluateAlbedo(*albedo, grid,
                                           {.spacing = 0.0, .threads = threads, .ground = &ground});
            timed(albedoStep, albedoTime.Milliseconds());
        }
        if (MapsMaterials(output) && target.materials.empty()) {
            const Stopwatch materialsTime;
            target.materials = EvaluateMaterials(terrain, config.materials->options, grid,
                                                 EvaluationOptions{0.0, threads});
            timed(albedoStep + 1, materialsTime.Milliseconds());
        }
    }
    generated.geoKeys = KeysOf(terrain);
    generated.seconds = stopwatch.Milliseconds() / 1000.0;
    if (!generated.grids.empty()) {
        LogStepTimes(steps, generated.grids.size(), stopwatch.Milliseconds());
    }
    return generated;
}

std::string Summary(const GeneratedTerrain& terrain) {
    std::ostringstream text;
    text << std::setprecision(3);
    if (terrain.grids.empty()) {
        return "Built the terrain; no output grids to evaluate before writing";
    }
    if (terrain.grids.size() == 1) {
        text << "Generated a " << terrain.grids[0].heights.Width() << "x"
             << terrain.grids[0].heights.Height() << " terrain";
    } else {
        text << "Generated " << terrain.grids.size() << " grids (";
        for (std::size_t i = 0; i < terrain.grids.size(); ++i) {
            text << (i > 0 ? ", " : "") << terrain.grids[i].heights.Width() << "x"
                 << terrain.grids[i].heights.Height();
        }
        text << ")";
    }
    text << " in " << terrain.seconds << " s on " << DescribeBackend(terrain.backend);
    return text.str();
}

std::vector<std::filesystem::path> WriteOutputs(const TerrainConfig& config,
                                                const GeneratedTerrain& terrain,
                                                const std::filesystem::path& directory) {
    const std::optional<RasterGrid> base = BaseGrid(terrain.terrain);
    std::vector<std::filesystem::path> written;
    std::vector<PlacedBoulderSet> placed;
    for (const OutputSpec& spec : config.outputs) {
        const std::string& relative =
            std::visit([](const auto& output) -> const std::string& { return output.path; }, spec);
        const std::filesystem::path path = config::Resolve(relative, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        const OutputContext context{terrain,
                                    config::OutputGrid(spec, base, terrain.terrain.surface),
                                    SpacingOf(spec), &config, &placed};
        if (std::holds_alternative<MeshOutput>(spec) == context.grid.has_value()) {
            throw std::runtime_error("No grid to write " + path.string() + " over");
        }
        if (std::visit([&](const auto& output) { return Write(output, path, context); }, spec)) {
            written.push_back(path);
        }
    }
    return written;
}

}  // namespace terraingen
