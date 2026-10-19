#include "terraingen/config/generate.h"

#include "terraingen/config/build.h"
#include "terraingen/config/hierarchy.h"
#include "terraingen/config/json.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/pipeline/body_mesh.h"
#include "terraingen/pipeline/height_mesh.h"
#include "terraingen/sources/geotiff_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/files.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace terraingen {

namespace {

/// The projection of a terrain's raster base, or geographic keys on a body without one.
GeoKeys OutputGeoKeys(const Terrain& terrain) {
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

/// Whether an output writes the material map or its photometry.
bool MapsMaterials(const OutputSpec& output) {
    return config::HoldsAny<MaterialsOutput, PhotometryOutput>(output);
}

/// Whether an output writes the heights, albedo or materials over its grid.
bool WritesGrid(const OutputSpec& output) {
    return config::HoldsAny<HeightmapOutput, AlbedoOutput, TerrainMeshOutput, MaterialsOutput,
                            PhotometryOutput>(output);
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

/// The distinct grids the heightmap, albedo, terrain mesh and material outputs cover, in order.
std::vector<RasterGrid> GeneratedGrids(const std::vector<OutputSpec>& outputs,
                                       const Terrain& terrain) {
    const std::optional<RasterGrid> base = BaseGrid(terrain);
    std::vector<RasterGrid> grids;
    for (const OutputSpec& output : outputs) {
        if (WritesGrid(output)) {
            const RasterGrid grid = *config::OutputGrid(output, base, terrain.surface);
            if (std::find(grids.begin(), grids.end(), grid) == grids.end()) {
                grids.push_back(grid);
            }
        }
    }
    return grids;
}

/// Evaluates a generated terrain's heights, albedo and materials over the grids its outputs
/// need, timing each step.
class GridGenerator {
  public:
    GridGenerator(const TerrainConfig& config, const std::vector<OutputSpec>& outputs,
                  GeneratedTerrain& generated, const std::filesystem::path& directory,
                  unsigned threads)
        : config_(config), generated_(generated), directory_(directory), threads_(threads),
          grids_(GeneratedGrids(outputs, generated.terrain)) {
        for (std::size_t n = 0; n < config.layers.size(); ++n) {
            steps_.push_back(
                {"layer " + std::to_string(n + 1) + " " + LayerType(config.layers[n])});
        }
        steps_.push_back({"albedo"});
        steps_.push_back({"materials"});
    }

    /// Generates what `output` writes over its grid, unless an earlier output did.
    void Generate(const OutputSpec& output) {
        const Terrain& terrain = generated_.terrain;
        const RasterGrid grid = *config::OutputGrid(output, BaseGrid(terrain), terrain.surface);
        GeneratedGrid& target = Heights(grid);
        if (std::holds_alternative<AlbedoOutput>(output) && target.albedo.empty()) {
            AddAlbedo(grid, target);
        }
        if (MapsMaterials(output) && target.materials.empty()) {
            const Stopwatch stopwatch;
            target.materials = EvaluateMaterials(terrain, config_.materials->options, grid,
                                                 EvaluationOptions{0.0, threads_});
            Timed(steps_.size() - 1, stopwatch.Milliseconds());
        }
    }

    /// Logs the time of each step, of `total` milliseconds, when any grid was generated.
    void LogTimes(double total) const {
        if (!generated_.grids.empty()) {
            LogStepTimes(steps_, generated_.grids.size(), total);
        }
    }

  private:
    void Timed(std::size_t step, double milliseconds) {
        steps_[step].milliseconds += milliseconds;
        LogInfo("pipeline", "  ", steps_[step].name, " in ", FormatDuration(milliseconds));
    }

    /// The heights over `grid`, evaluated the first time.
    GeneratedGrid& Heights(const RasterGrid& grid) {
        if (GeneratedGrid* existing = generated_.Find(grid)) {
            return *existing;
        }
        const Terrain& terrain = generated_.terrain;
        LogInfo("pipeline", "Grid ", generated_.grids.size() + 1, " of ", grids_.size(), ": ",
                grid.width, "x", grid.height, " at ", FormatSpacing(grid.Spacing(terrain.surface)));
        EvaluationOptions options{0.0, threads_, generated_.backend};
        options.onLayer = [this](std::size_t step, double milliseconds) {
            Timed(step, milliseconds);
        };
        generated_.grids.push_back({Evaluate(terrain, grid, options), {}});
        return generated_.grids.back();
    }

    void AddAlbedo(const RasterGrid& grid, GeneratedGrid& target) {
        const Stopwatch stopwatch;
        const Terrain& terrain = generated_.terrain;
        if (!albedo_) {
            albedo_ = BuildAlbedo(config_, terrain, directory_);
        }
        const TerrainGround ground(terrain, terrain.layers.size());
        AlbedoContext context;
        context.spacing = 0.0;
        context.threads = threads_;
        context.ground = &ground;
        target.albedo = EvaluateAlbedo(*albedo_, grid, context);
        Timed(steps_.size() - 2, stopwatch.Milliseconds());
    }

    const TerrainConfig& config_;
    GeneratedTerrain& generated_;
    const std::filesystem::path& directory_;
    unsigned threads_;
    std::vector<RasterGrid> grids_;
    std::vector<StepTime> steps_;
    std::optional<Albedo> albedo_;
};

/// `value` rounded to a whole number when within rounding of one, as cell positions on a coarser
/// lattice are.
double Snapped(double value) {
    const double nearest = std::round(value);
    return std::fabs(value - nearest) < 1e-9 ? nearest : value;
}

/// `heights` interpolated bilinearly at world (`x`, `y`) between cell centres; NaN outside them or
/// next to a cell without data.
double Bilinear(const Raster<float>& heights, double x, double y) {
    const Vec2d pixel = heights.Transform().WorldToPixel(x, y);
    const double u = Snapped(pixel.x - 0.5);
    const double v = Snapped(pixel.y - 0.5);
    const double col = std::floor(u);
    const double row = std::floor(v);
    const double fu = u - col;
    const double fv = v - row;
    const auto at = [&](double c, double r, double weight) {
        if (weight == 0.0) {
            return 0.0;
        }
        if (c < 0.0 || r < 0.0 || c >= static_cast<double>(heights.Width()) ||
            r >= static_cast<double>(heights.Height())) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const float value = heights(static_cast<std::size_t>(c), static_cast<std::size_t>(r));
        return heights.IsNoData(value) ? std::numeric_limits<double>::quiet_NaN()
                                       : weight * static_cast<double>(value);
    };
    return at(col, row, (1.0 - fu) * (1.0 - fv)) + at(col + 1.0, row, fu * (1.0 - fv)) +
           at(col, row + 1.0, (1.0 - fu) * fv) + at(col + 1.0, row + 1.0, fu * fv);
}

/// Blends each hierarchy level's heights toward its parent's over `cells` cells along its edges,
/// with a smoothstep from the parent's on the edge, so the levels' meshes meet without gaps;
/// parents blend first.
void BlendHierarchyEdges(const std::vector<config::HierarchyGrid>& grids, std::size_t cells,
                         GeneratedTerrain& generated) {
    const Surface& surface = generated.terrain.surface;
    for (const config::HierarchyGrid& grid : grids) {
        if (!grid.parent) {
            continue;
        }
        GeneratedGrid* child = generated.Find(config::RegionGrid(grid.grid, surface));
        const GeneratedGrid* parent =
            generated.Find(config::RegionGrid(grids[*grid.parent].grid, surface));
        if (!child || !parent) {
            continue;
        }
        Raster<float>& heights = child->heights;
        const std::size_t width = heights.Width();
        const std::size_t height = heights.Height();
        for (std::size_t row = 0; row < height; ++row) {
            for (std::size_t col = 0; col < width; ++col) {
                const std::size_t edge =
                    std::min(std::min(col, row), std::min(width - 1 - col, height - 1 - row));
                if (edge >= cells || heights.IsNoData(heights(col, row))) {
                    continue;
                }
                const Vec2d centre = heights.CellCenter(col, row);
                const double outer = Bilinear(parent->heights, centre.x, centre.y);
                if (std::isnan(outer)) {
                    continue;
                }
                const double t = static_cast<double>(edge) / static_cast<double>(cells);
                const double weight = t * t * (3.0 - 2.0 * t);
                heights(col, row) = static_cast<float>(
                    outer + weight * (static_cast<double>(heights(col, row)) - outer));
            }
        }
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

/// How a GeoTIFF output is written.
GeoTiffWriteOptions TiffOptions(const GeoTiffOutput& output) {
    GeoTiffWriteOptions options;
    options.compression = output.compression;
    options.tileSize = output.tileSize;
    return options;
}

/// The material rasters generated over the output's grid.
/// @throws std::runtime_error if they were not generated.
const std::vector<Raster<float>>& RequireMaterials(const OutputContext& context,
                                                   const std::filesystem::path& path) {
    const std::vector<Raster<float>>& materials = context.Generated(path).materials;
    if (materials.empty()) {
        throw std::runtime_error("No materials to write to " + path.string());
    }
    return materials;
}

/// Each `Write` returns whether it wrote the file.
bool Write(const HeightmapOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    WriteGeoTiff(path, context.Generated(path).heights, context.generated.geoKeys,
                 TiffOptions(output));
    return true;
}

bool Write(const AlbedoOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const std::vector<Raster<float>>& albedo = context.Generated(path).albedo;
    if (albedo.empty()) {
        throw std::runtime_error("No albedo to write to " + path.string());
    }
    WriteGeoTiff(path, albedo, context.generated.geoKeys, TiffOptions(output));
    return true;
}

bool Write(const MaterialsOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    WriteGeoTiff(path, RequireMaterials(context, path), context.generated.geoKeys,
                 TiffOptions(output));
    return true;
}

bool Write(const PhotometryOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const MaterialPhotometry photometry =
        config::BuildPhotometry(context.config->materials->photometry);
    WriteGeoTiff(path, EvaluatePhotometry(RequireMaterials(context, path), photometry),
                 context.generated.geoKeys, TiffOptions(output));
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
    ShadedMesh shaded = WithCreases(mesh, kPi);
    WriteMesh(path, {std::move(shaded.mesh), std::move(shaded.normals)});
    LogInfo("pipeline", "Wrote a body mesh of ", mesh.triangles.size(), " triangles to ",
            path.string(), " in ", FormatDuration(stopwatch.Milliseconds()));
    return true;
}

bool Write(const TerrainMeshOutput& output, const std::filesystem::path& path,
           const OutputContext& context) {
    const Stopwatch stopwatch;
    HeightMeshOptions options;
    options.skirt = output.skirt;
    for (const RectangleRegionSpec& cutout : output.cutouts) {
        options.cutouts.push_back({cutout.minX, cutout.minY, cutout.maxX, cutout.maxY});
    }
    const Vec2d centre = context.Centre();
    const Vec2d site{output.siteX.value_or(centre.x), output.siteY.value_or(centre.y)};
    TriangleMesh mesh = HeightMesh(context.Generated(path).heights,
                                   context.generated.terrain.surface, site, options);
    const std::size_t triangles = mesh.triangles.size();
    std::vector<Vec3d> normals = VertexNormals(mesh);
    WriteMesh(path, {std::move(mesh), std::move(normals), {"terrain", {0.5, 0.5, 0.5}}});
    LogInfo("pipeline", "Wrote a terrain mesh of ", triangles, " triangles to ", path.string(),
            " in ", FormatDuration(stopwatch.Milliseconds()));
    return true;
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

GeneratedGrid* GeneratedTerrain::Find(const RasterGrid& grid) {
    return const_cast<GeneratedGrid*>(std::as_const(*this).Find(grid));
}

GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory, unsigned threads,
                                 std::optional<Backend> backend) {
    GeneratedTerrain generated{BuildTerrain(config, directory)};
    generated.backend = ResolveBackend(backend.value_or(DefaultBackend()));
    const Stopwatch stopwatch;
    const Surface& surface = generated.terrain.surface;
    const std::vector<OutputSpec> outputs =
        config::ConfigOutputs(config, BaseGrid(generated.terrain), surface);
    GridGenerator generator(config, outputs, generated, directory, threads);
    for (const OutputSpec& output : outputs) {
        if (WritesGrid(output)) {
            generator.Generate(output);
        }
    }
    if (config.hierarchy && config.hierarchy->blendCells > 0) {
        BlendHierarchyEdges(
            config::HierarchyGrids(*config.hierarchy, BaseGrid(generated.terrain), surface),
            config.hierarchy->blendCells, generated);
    }
    generated.geoKeys = OutputGeoKeys(generated.terrain);
    generated.seconds = stopwatch.Milliseconds() / 1000.0;
    generator.LogTimes(stopwatch.Milliseconds());
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
    for (const OutputSpec& spec : config::ConfigOutputs(config, base, terrain.terrain.surface)) {
        const std::string& relative =
            std::visit([](const auto& output) -> const std::string& { return output.path; }, spec);
        const std::filesystem::path path = config::Resolve(relative, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        const OutputContext context{terrain,
                                    config::OutputGrid(spec, base, terrain.terrain.surface),
                                    config::OutputSpacing(spec), &config, &placed};
        if (std::holds_alternative<MeshOutput>(spec) == context.grid.has_value()) {
            throw std::runtime_error("No grid to write " + path.string() + " over");
        }
        if (std::visit([&](const auto& output) { return Write(output, path, context); }, spec)) {
            written.push_back(path);
        }
    }
    if (config.hierarchy && !config.hierarchy->manifest.empty()) {
        const std::filesystem::path path = config::Resolve(config.hierarchy->manifest, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        WriteFile(path, config::HierarchyManifest(*config.hierarchy,
                                                  config::HierarchyGrids(*config.hierarchy, base,
                                                                         terrain.terrain.surface),
                                                  terrain.terrain.surface));
        written.push_back(path);
    }
    return written;
}

}  // namespace terraingen
