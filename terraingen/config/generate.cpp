#include "terraingen/config/generate.h"

#include "terraingen/config/build.h"
#include "terraingen/config/json.h"
#include "terraingen/config/regions.h"
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
    std::optional<std::size_t> region;
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

/// A grid and the terrain that evaluates it: the terrain itself, or a region's for its levels.
struct GridKey {
    RasterGrid grid;
    std::optional<std::size_t> region;
    std::optional<std::size_t> group;  ///< For a nested grid, its group.
    std::size_t index = 0;             ///< For a nested grid, its index in the group.
};

/// The keys of the grids `output` writes over: its own, or each grid of a nested mesh.
std::vector<GridKey> OutputKeys(const config::ResolvedOutput& output,
                                const config::RegionLayout& layout) {
    if (std::holds_alternative<NestedMeshOutput>(output.spec)) {
        std::vector<GridKey> keys;
        const config::RegionLayout::Group& group = layout.groups[*output.group];
        for (std::size_t index : output.grids) {
            const NestedGrid& grid = group.grids[index];
            const std::optional<std::size_t> region =
                grid.region ? std::optional<std::size_t>(group.regions[*grid.region])
                            : std::nullopt;
            keys.push_back({grid.grid, region, output.group, index});
        }
        return keys;
    }
    if (!WritesGrid(output.spec)) {
        return {};
    }
    if (output.group) {
        return {{*output.grid, output.region, output.group, output.grids.front()}};
    }
    return {{*output.grid, std::nullopt, std::nullopt, 0}};
}

/// Evaluates a generated terrain's heights, albedo and materials over the grids its outputs
/// need, timing each step.
class GridGenerator {
  public:
    GridGenerator(const TerrainConfig& config, const std::vector<config::ResolvedOutput>& outputs,
                  GeneratedTerrain& generated, const std::filesystem::path& directory,
                  unsigned threads)
        : config_(config), generated_(generated), directory_(directory), threads_(threads) {
        for (std::size_t n = 0; n < config.layers.size(); ++n) {
            steps_.push_back(
                {"layer " + std::to_string(n + 1) + " " + LayerType(config.layers[n])});
        }
        for (const RegionSpec& region : config.regions) {
            upscalingSteps_.push_back(steps_.size());
            for (std::size_t n = 0; n < region.upscaling.size(); ++n) {
                steps_.push_back({"region " + region.name + " upscaling " + std::to_string(n + 1) +
                                  " " + LayerType(region.upscaling[n])});
            }
        }
        steps_.push_back({"albedo"});
        steps_.push_back({"materials"});
        for (const config::ResolvedOutput& output : outputs) {
            for (const GridKey& key : OutputKeys(output, generated.layout)) {
                Count(key);
            }
        }
    }

    /// Generates what `output` writes over its grids, unless an earlier output did.
    void Generate(const config::ResolvedOutput& output) {
        for (const GridKey& key : OutputKeys(output, generated_.layout)) {
            const std::size_t at = Heights(key);
            const Terrain& terrain = TerrainOf(key);
            if (std::holds_alternative<AlbedoOutput>(output.spec) &&
                generated_.grids[at].albedo.empty()) {
                AddAlbedo(key.grid, terrain, generated_.grids[at]);
            }
            if (MapsMaterials(output.spec) && generated_.grids[at].materials.empty()) {
                const Stopwatch stopwatch;
                generated_.grids[at].materials =
                    EvaluateMaterials(terrain, config_.materials->options, key.grid,
                                      EvaluationOptions{0.0, threads_});
                Timed(steps_.size() - 1, stopwatch.Milliseconds());
            }
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

    /// The key of the grid `key`'s nested grid lies in.
    GridKey ParentKey(const GridKey& key) const {
        const config::RegionLayout::Group& group = generated_.layout.groups[*key.group];
        const std::size_t parent = *group.grids[key.index].parent;
        const NestedGrid& grid = group.grids[parent];
        const std::optional<std::size_t> region =
            grid.region ? std::optional<std::size_t>(group.regions[*grid.region]) : std::nullopt;
        return {grid.grid, region, key.group, parent};
    }

    /// Counts `key` and the grids above it among the grids to evaluate.
    void Count(const GridKey& key) {
        if (key.group && generated_.layout.groups[*key.group].grids[key.index].parent) {
            Count(ParentKey(key));
        }
        const bool seen = std::any_of(keys_.begin(), keys_.end(), [&](const GridKey& other) {
            return other.grid == key.grid && other.region == key.region;
        });
        if (!seen) {
            keys_.push_back(key);
        }
    }

    const Terrain& TerrainOf(const GridKey& key) const {
        return key.group ? generated_.nested[*key.group].GridTerrain(key.index)
                         : generated_.terrain;
    }

    /// The index of the heights over `key`, evaluated the first time after the grid above it.
    std::size_t Heights(const GridKey& key) {
        if (const GeneratedGrid* existing = generated_.Find(key.grid, key.region)) {
            return static_cast<std::size_t>(existing - generated_.grids.data());
        }
        std::optional<std::size_t> parent;
        if (key.group && generated_.layout.groups[*key.group].grids[key.index].parent) {
            parent = Heights(ParentKey(key));
        }
        const Surface& surface = generated_.terrain.surface;
        LogInfo("pipeline", "Grid ", generated_.grids.size() + 1, " of ", keys_.size(), ": ",
                key.grid.width, "x", key.grid.height, " at ",
                FormatSpacing(key.grid.Spacing(surface)),
                key.region ? " in region " + config_.regions[*key.region].name : std::string());
        EvaluationOptions options{0.0, threads_, generated_.backend};
        const std::size_t upscaling =
            key.region ? config_.regions[*key.region].upscaling.size() : 0;
        const std::size_t offset = key.region ? upscalingSteps_[*key.region] : 0;
        options.onLayer = [this, upscaling, offset](std::size_t step, double milliseconds) {
            Timed(step < upscaling ? offset + step : step - upscaling, milliseconds);
        };
        Raster<float> heights =
            key.group
                ? generated_.nested[*key.group].Evaluate(
                      key.index, parent ? &generated_.grids[*parent].heights : nullptr, options)
                : Evaluate(generated_.terrain, key.grid, options);
        GeneratedGrid generated;
        generated.heights = std::move(heights);
        generated.region = key.region;
        generated_.grids.push_back(std::move(generated));
        return generated_.grids.size() - 1;
    }

    void AddAlbedo(const RasterGrid& grid, const Terrain& terrain, GeneratedGrid& target) {
        const Stopwatch stopwatch;
        if (!albedo_) {
            albedo_ = BuildAlbedo(config_, generated_.terrain, directory_);
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
    std::vector<GridKey> keys_;
    std::vector<StepTime> steps_;
    std::vector<std::size_t> upscalingSteps_;
    std::optional<Albedo> albedo_;
};

/// What `WriteOutputs` writes one output from.
struct OutputContext {
    const GeneratedTerrain& generated;
    std::optional<RasterGrid> grid;         ///< The grid the output covers; none for a mesh.
    double spacing = 0.0;                   ///< The output's sample spacing; 0 for its grid's.
    const TerrainConfig* config = nullptr;  ///< The configuration the outputs come from.
    std::vector<PlacedBoulderSet>* placed = nullptr;   ///< Boulders placed for earlier outputs.
    const config::ResolvedOutput* resolved = nullptr;  ///< The output and what it covers.

    /// The terrain the output is evaluated with: a region's for its levels, else the terrain.
    const Terrain& Evaluated() const {
        return resolved->group
                   ? generated.nested[*resolved->group].GridTerrain(resolved->grids.front())
                   : generated.terrain;
    }

    /// The boulders placed over the output's grid, placed once for every output that lists them.
    const BoulderFeatures& Boulders() const {
        for (const PlacedBoulderSet& set : *placed) {
            if (set.grid == *grid && set.region == resolved->region && set.spacing == spacing) {
                return set.features;
            }
        }
        placed->push_back(
            {*grid, resolved->region, spacing, PlacedBoulders(Evaluated(), *grid, spacing)});
        return placed->back().features;
    }

    /// The heights and albedo generated over the output's grid.
    /// @throws std::runtime_error if they were not generated.
    const GeneratedGrid& Generated(const std::filesystem::path& path) const {
        const GeneratedGrid* found = grid ? generated.Find(*grid, resolved->region) : nullptr;
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
        PlacedCraters(context.Evaluated(), *context.grid, context.spacing);
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

/// Writes a nested mesh output: one merged mesh, or one mesh per grid, and returns the paths.
/// @param prepare Resolves a relative path and creates its folder.
/// @throws std::runtime_error if a grid's heights were not generated.
template <typename Prepare>
std::vector<std::filesystem::path>
WriteNestedMesh(const NestedMeshOutput& output, const config::ResolvedOutput& resolved,
                const GeneratedTerrain& generated, const Prepare& prepare) {
    const Stopwatch stopwatch;
    const NestedTerrain& nested = generated.nested[*resolved.group];
    std::vector<Raster<float>> heights(nested.Grids().size());
    for (std::size_t index : resolved.grids) {
        const NestedGrid& grid = nested.Grids()[index];
        const std::optional<std::size_t> region =
            grid.region ? std::optional<std::size_t>(
                              generated.layout.groups[*resolved.group].regions[*grid.region])
                        : std::nullopt;
        const GeneratedGrid* found = generated.Find(grid.grid, region);
        if (!found) {
            throw std::runtime_error("No heights generated for " + output.path);
        }
        heights[index] = found->heights;
    }
    const auto write = [&](const std::filesystem::path& path, TriangleMesh mesh) {
        const std::size_t triangles = mesh.triangles.size();
        std::vector<Vec3d> normals = VertexNormals(mesh);
        WriteMesh(path, {std::move(mesh), std::move(normals), {"terrain", {0.5, 0.5, 0.5}}});
        LogInfo("pipeline", "Wrote a nested mesh of ", triangles, " triangles to ", path.string(),
                " in ", FormatDuration(stopwatch.Milliseconds()));
    };
    std::vector<std::filesystem::path> written;
    if (!config::SplitsNestedMesh(output)) {
        written.push_back(prepare(output.path));
        write(written.back(), nested.Mesh(heights, resolved.grids, output.skirt));
        return written;
    }
    for (std::size_t index : resolved.grids) {
        written.push_back(
            prepare(config::NestedMeshFile(output, generated.layout, *resolved.group, index)));
        write(written.back(), nested.GridMesh(heights[index], index, resolved.grids, output.skirt));
    }
    return written;
}

}  // namespace

const GeneratedGrid* GeneratedTerrain::Find(const RasterGrid& grid,
                                            std::optional<std::size_t> region) const {
    for (const GeneratedGrid& generated : grids) {
        if (generated.region == region && RasterGrid::FromRaster(generated.heights) == grid) {
            return &generated;
        }
    }
    return nullptr;
}

GeneratedGrid* GeneratedTerrain::Find(const RasterGrid& grid, std::optional<std::size_t> region) {
    return const_cast<GeneratedGrid*>(std::as_const(*this).Find(grid, region));
}

GeneratedTerrain GenerateTerrain(const TerrainConfig& config,
                                 const std::filesystem::path& directory, unsigned threads,
                                 std::optional<Backend> backend) {
    GeneratedTerrain generated{BuildTerrain(config, directory)};
    generated.backend = ResolveBackend(backend.value_or(DefaultBackend()));
    const Stopwatch stopwatch;
    const Surface& surface = generated.terrain.surface;
    const std::optional<RasterGrid> base = BaseGrid(generated.terrain);
    generated.layout = config::LayOutRegions(config.regions, base, surface);
    generated.nested =
        config::BuildNestedTerrains(config, generated.terrain, generated.layout, directory);
    const std::vector<config::ResolvedOutput> outputs =
        config::ResolveOutputs(config, base, surface, generated.layout);
    GridGenerator generator(config, outputs, generated, directory, threads);
    for (const config::ResolvedOutput& output : outputs) {
        generator.Generate(output);
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
    const Surface& surface = terrain.terrain.surface;
    const std::vector<config::ResolvedOutput> outputs =
        config::ResolveOutputs(config, base, surface, terrain.layout);
    std::vector<std::filesystem::path> written;
    std::vector<PlacedBoulderSet> placed;
    const auto prepare = [&](const std::string& relative) {
        const std::filesystem::path path = config::Resolve(relative, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        return path;
    };
    for (const config::ResolvedOutput& output : outputs) {
        if (const auto* mesh = std::get_if<NestedMeshOutput>(&output.spec)) {
            for (const auto& path : WriteNestedMesh(*mesh, output, terrain, prepare)) {
                written.push_back(path);
            }
            continue;
        }
        if (const auto* manifest = std::get_if<ManifestOutput>(&output.spec)) {
            const std::filesystem::path path = prepare(manifest->path);
            WriteFile(path, config::RegionManifest(*manifest, terrain.layout, outputs, surface));
            written.push_back(path);
            continue;
        }
        const std::string& relative =
            std::visit([](const auto& o) -> const std::string& { return o.path; }, output.spec);
        const std::filesystem::path path = prepare(relative);
        const OutputContext context{terrain, output.grid, config::OutputSpacing(output.spec),
                                    &config, &placed,     &output};
        if (std::holds_alternative<MeshOutput>(output.spec) == context.grid.has_value()) {
            throw std::runtime_error("No grid to write " + path.string() + " over");
        }
        const bool wrote = std::visit(
            [&](const auto& o) {
                using Output = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<Output, NestedMeshOutput> ||
                              std::is_same_v<Output, ManifestOutput>) {
                    return false;
                } else {
                    return Write(o, path, context);
                }
            },
            output.spec);
        if (wrote) {
            written.push_back(path);
        }
    }
    return written;
}

}  // namespace terraingen
