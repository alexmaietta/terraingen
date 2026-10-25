// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/config/generate.h"

#include "config/fields/fields.h"
#include "terraingen/config/build.h"
#include "terraingen/config/json.h"
#include "terraingen/gpu/backend.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/io/mesh_io.h"
#include "terraingen/pipeline/body_mesh.h"
#include "terraingen/pipeline/mesh_build.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/log.h"
#include "util/overloaded.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
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

/// The projection of a terrain's raster source, or geographic keys on a body without one.
GeoKeys OutputGeoKeys(const TerrainGenerator& generator) {
    return TerrainGeoKeys(generator);
}

/// The grid of the terrain's source, or none without one or for one without bounds.
std::optional<RasterGrid> SourceGrid(const TerrainGenerator& generator) {
    return generator.source ? BoundedGrid(*generator.source) : std::nullopt;
}

/// Whether an output needs the heights over its grid: a raster or a terrain mesh.
bool NeedsHeights(const OutputSpec& output) {
    return config::HoldsAny<HeightmapOutput, AlbedoOutput, TerrainMeshOutput, MaterialsOutput,
                            PhotometryOutput>(output);
}

/// The center of a grid.
Vec2d GridCenter(const RasterGrid& grid) {
    const Bounds2d bounds = grid.Bounds();
    return {0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
}

/// One step of generating a terrain and the time it took over every grid.
struct StepTime {
    std::string name;
    double milliseconds = 0.0;
};

/// The steps after the layers, in order.
enum class Step { Albedo, Materials, Features, Meshes };

/// A sample spacing in meters to three significant figures, or in kilometers from 1 km.
std::string FormatSpacing(double meters) {
    std::ostringstream text;
    text << std::setprecision(3);
    if (meters >= 1000.0) {
        text << meters / 1000.0 << " km";
    } else {
        text << meters << " m";
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

/// The distinct grids the outputs cover, in order.
std::vector<RasterGrid> CoveredGrids(const TerrainGenConfig& config,
                                     const TerrainGenerator& generator) {
    const std::optional<RasterGrid> source = SourceGrid(generator);
    std::vector<RasterGrid> grids;
    for (const OutputSpec& output : config.outputs) {
        const std::optional<RasterGrid> grid =
            config::OutputGrid(output, source, generator.surface);
        if (grid && std::find(grids.begin(), grids.end(), *grid) == grids.end()) {
            grids.push_back(*grid);
        }
    }
    return grids;
}

/// Generates what each output asks for over its grid, once per grid and result, timing each step.
class OutputGenerator {
  public:
    OutputGenerator(const TerrainGenConfig& config, GeneratedOutputs& generated,
                    const std::filesystem::path& directory, unsigned threads)
        : config_(config), generated_(generated), directory_(directory), threads_(threads),
          source_(SourceGrid(generated.generator)),
          grids_(CoveredGrids(config, generated.generator)), layerTimer_(LayerTypes(config)) {
        for (std::size_t n = 0; n < config.layers.size(); ++n) {
            steps_.push_back(
                {"layer " + std::to_string(n + 1) + " " + LayerType(config.layers[n])});
        }
        for (const char* name : {"albedo", "materials", "features", "meshes"}) {
            steps_.push_back({name});
        }
    }

    /// Generates what `output` asks for, unless an earlier output did.
    void Generate(const OutputSpec& output) {
        if (const auto* mesh = std::get_if<MeshOutput>(&output)) {
            AddBodyMesh(*mesh);
            return;
        }
        const std::optional<RasterGrid> grid =
            config::OutputGrid(output, source_, generated_.generator.surface);
        if (!grid) {
            return;
        }
        GeneratedGrid& target = Target(*grid);
        if (NeedsHeights(output) && target.heights.Empty()) {
            AddHeights(target);
        }
        if (std::holds_alternative<AlbedoOutput>(output) && target.albedo.empty()) {
            AddAlbedo(target);
        }
        AddMaterials(output, target);
        AddFeatures(output, target);
        if (const auto* mesh = std::get_if<TerrainMeshOutput>(&output); mesh && !target.mesh) {
            AddTerrainMesh(*mesh, target);
        }
    }

    /// Logs the time of each step, of `total` milliseconds, when anything was generated.
    void LogTimes(double total) const {
        if (!generated_.grids.empty() || generated_.bodyMesh || generated_.bodyIcq) {
            LogStepTimes(steps_, generated_.grids.size(), total);
        }
    }

    /// Each layer's type and its time summed over every evaluation, in order.
    const std::vector<LayerTiming>& LayerTimings() const {
        return layerTimer_.Timings();
    }

  private:
    static std::vector<std::string> LayerTypes(const TerrainGenConfig& config) {
        std::vector<std::string> types;
        for (const LayerSpec& layer : config.layers) {
            types.push_back(LayerType(layer));
        }
        return types;
    }

    void Timed(std::size_t step, double milliseconds) {
        steps_[step].milliseconds += milliseconds;
        LogInfo("pipeline", "  ", steps_[step].name, " in ", FormatDuration(milliseconds));
    }

    void Timed(Step step, double milliseconds) {
        Timed(config_.layers.size() + static_cast<std::size_t>(step), milliseconds);
    }

    /// The generated grid over `grid`, added empty the first time.
    GeneratedGrid& Target(const RasterGrid& grid) {
        if (GeneratedGrid* existing = generated_.Find(grid)) {
            return *existing;
        }
        GeneratedGrid added;
        added.grid = grid;
        generated_.grids.push_back(std::move(added));
        return generated_.grids.back();
    }

    void AddHeights(GeneratedGrid& target) {
        const TerrainGenerator& generator = generated_.generator;
        const auto index = static_cast<std::size_t>(
            std::find(grids_.begin(), grids_.end(), target.grid) - grids_.begin());
        LogInfo("pipeline", "Grid ", index + 1, " of ", grids_.size(), ": ", target.grid.width, "x",
                target.grid.height, " at ", FormatSpacing(target.grid.Spacing(generator.surface)));
        EvaluationOptions options{0.0, threads_, generated_.backend};
        options.onLayer = [this](std::size_t step, double milliseconds) {
            Timed(step, milliseconds);
            layerTimer_.Add(step, milliseconds);
        };
        target.heights = Evaluate(generator, target.grid, options);
    }

    void AddAlbedo(GeneratedGrid& target) {
        const Stopwatch stopwatch;
        const TerrainGenerator& generator = generated_.generator;
        if (!albedo_) {
            albedo_ = config::BuildValidAlbedo(config_, generator, directory_);
        }
        const TerrainGround ground(generator, generator.layers.size());
        AlbedoContext context;
        context.spacing = 0.0;
        context.threads = threads_;
        context.ground = &ground;
        target.albedo = EvaluateAlbedo(*albedo_, target.grid, context);
        Timed(Step::Albedo, stopwatch.Milliseconds());
    }

    /// Adds the material map a materials or photometry output asks for, and the photometry.
    void AddMaterials(const OutputSpec& output, GeneratedGrid& target) {
        if (config::HoldsAny<MaterialsOutput, PhotometryOutput>(output) &&
            target.materials.empty()) {
            const Stopwatch stopwatch;
            target.materials = EvaluateMaterials(generated_.generator, config_.materials->options,
                                                 target.grid, EvaluationOptions{0.0, threads_});
            Timed(Step::Materials, stopwatch.Milliseconds());
        }
        if (std::holds_alternative<PhotometryOutput>(output) && target.photometry.empty()) {
            const Stopwatch stopwatch;
            target.photometry = EvaluatePhotometry(
                target.materials, config::BuildPhotometry(config_.materials->photometry));
            Timed(Step::Materials, stopwatch.Milliseconds());
        }
    }

    /// Places the craters a crater list asks for, or the boulders a boulder list or models do.
    void AddFeatures(const OutputSpec& output, GeneratedGrid& target) {
        const TerrainGenerator& generator = generated_.generator;
        const double spacing = config::OutputSpacing(output, source_, generator.surface);
        if (std::holds_alternative<CraterListOutput>(output) && !target.craters) {
            const Stopwatch stopwatch;
            target.craters = PlacedCraters(generator, target.grid, spacing);
            Timed(Step::Features, stopwatch.Milliseconds());
        }
        if (config::HoldsAny<BoulderListOutput, BoulderModelsOutput>(output) && !target.boulders) {
            const Stopwatch stopwatch;
            target.boulders = PlacedBoulders(generator, target.grid, spacing);
            Timed(Step::Features, stopwatch.Milliseconds());
        }
    }

    void AddTerrainMesh(const TerrainMeshOutput& output, GeneratedGrid& target) {
        const Stopwatch stopwatch;
        BuildMeshOptions options;
        options.skirt = output.skirt;
        for (const ExtentSpec& cutout : output.cutouts) {
            options.cutouts.push_back(*config::ExtentBox(cutout));
        }
        const Vec2d site = output.siteDeg.size() == 2 ? Vec2d{output.siteDeg[0], output.siteDeg[1]}
                                                      : GridCenter(target.grid);
        target.mesh = BuildMesh(target.heights, generated_.generator.surface, site, options);
        Timed(Step::Meshes, stopwatch.Milliseconds());
    }

    void AddBodyMesh(const MeshOutput& output) {
        const Stopwatch stopwatch;
        const TerrainGenerator& generator = generated_.generator;
        const double spacing = output.spacing.value_or(0.0);
        if (IsIcqPath(output.path)) {
            if (!generated_.bodyIcq) {
                generated_.bodyIcq = BodyIcq(generator, output.cellsPerFace, spacing, threads_);
                Timed(Step::Meshes, stopwatch.Milliseconds());
            }
        } else if (!generated_.bodyMesh) {
            generated_.bodyMesh = BodyMesh(generator, output.cellsPerFace, spacing, threads_);
            Timed(Step::Meshes, stopwatch.Milliseconds());
        }
    }

    const TerrainGenConfig& config_;
    GeneratedOutputs& generated_;
    const std::filesystem::path& directory_;
    unsigned threads_;
    std::optional<RasterGrid> source_;
    std::vector<RasterGrid> grids_;
    std::vector<StepTime> steps_;
    std::optional<Albedo> albedo_;
    LayerTimer layerTimer_;
};

/// A field-list `IO` that finds a layer's top-level "seed", or its inner layer's in a masked
/// layer.
class SeedFinder {
  public:
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view /*description*/ = {},
                    std::string_view /*unit*/ = {}, const config::FieldRange& /*range*/ = {}) {
        if constexpr (std::is_same_v<T, std::uint32_t>) {
            if (key == "seed") {
                seed = value;
            }
        } else if constexpr (std::is_same_v<T, Boxed<LayerSpec>>) {
            Find(*value);
        }
    }

    /// Visits the fields of `layer`.
    void Find(LayerSpec layer) {
        std::visit([this](auto& spec) { config::Fields(*this, spec); }, layer);
    }

    std::optional<std::uint32_t> seed;
};

/// The top-level "seed" of each layer that has one, in order, a masked layer's from inside.
std::vector<std::uint32_t> LayerSeeds(const TerrainGenConfig& config) {
    std::vector<std::uint32_t> seeds;
    for (const LayerSpec& layer : config.layers) {
        SeedFinder finder;
        finder.Find(layer);
        if (finder.seed) {
            seeds.push_back(*finder.seed);
        }
    }
    return seeds;
}

/// The density of the first top-level crater layer, or null without one.
std::shared_ptr<const SizeFrequencyDistribution> CraterTarget(const TerrainGenConfig& config) {
    const std::vector<std::size_t> craters = config::LayerIndices<CraterLayerSpec>(config.layers);
    if (craters.empty()) {
        return nullptr;
    }
    return BuildCraterModels(std::get<CraterLayerSpec>(config.layers[craters.front()])).density;
}

/// The report of what was generated from `config`, with each layer's summed `timings`.
TerrainReport BuildReport(const TerrainGenConfig& config, const GeneratedOutputs& generated,
                          std::vector<LayerTiming> timings, unsigned threads) {
    TerrainReport report;
    const std::shared_ptr<const SizeFrequencyDistribution> target = CraterTarget(config);
    for (const GeneratedGrid& grid : generated.grids) {
        if (grid.heights.Empty()) {
            continue;
        }
        report.grids.push_back(ReportGrid(
            grid.heights, generated.generator.surface, grid.craters ? &*grid.craters : nullptr,
            grid.boulders ? &*grid.boulders : nullptr, target.get(), threads));
    }
    report.layers = std::move(timings);
    report.backend = std::string(BackendName(generated.backend));
    report.threads = threads;
    report.seeds = LayerSeeds(config);
    report.seconds = generated.seconds;
    return report;
}

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
            throw std::runtime_error("No " + std::string(config::SpecName<Output>::kName) +
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
    WriteBoulderModels(models, path, output.creaseAngle, context.generated.generator.surface,
                       GridCenter(context.grid->grid), output.instanced);
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
    const TerrainReport& report = Require(context.generated.report, "report", path);
    std::ofstream file(path, std::ios::binary);
    file << ReportToJson(report) << '\n';
    if (!file) {
        throw std::runtime_error("Cannot write the report to " + path.string());
    }
    LogInfo("pipeline", "Wrote the report to ", path.string());
    return true;
}

/// The path an output names, empty for none.
const std::string& OutputPath(const OutputSpec& spec) {
    return std::visit([](const auto& output) -> const std::string& { return output.path; }, spec);
}

}  // namespace

const GeneratedGrid& GeneratedOutputs::Grid() const {
    if (grids.size() != 1) {
        throw std::runtime_error("The outputs cover " + std::to_string(grids.size()) +
                                 " grids, not one; find a grid by its RasterGrid instead");
    }
    return grids.front();
}

const GeneratedGrid* GeneratedOutputs::Find(const RasterGrid& grid) const {
    for (const GeneratedGrid& generated : grids) {
        if (generated.grid == grid) {
            return &generated;
        }
    }
    return nullptr;
}

GeneratedGrid* GeneratedOutputs::Find(const RasterGrid& grid) {
    return const_cast<GeneratedGrid*>(std::as_const(*this).Find(grid));
}

GeneratedOutputs GenerateFromConfig(const TerrainGenConfig& config,
                                    const std::filesystem::path& given, unsigned threads,
                                    std::optional<Backend> backend) {
    const std::filesystem::path directory = ConfigDirectory(config, given);
    GeneratedOutputs generated{BuildGenerator(config, directory)};
    generated.backend = ResolveBackend(backend.value_or(DefaultBackend()));
    const Stopwatch stopwatch;
    OutputGenerator outputGenerator(config, generated, directory, threads);
    for (const OutputSpec& output : config.outputs) {
        outputGenerator.Generate(output);
    }
    generated.geoKeys = OutputGeoKeys(generated.generator);
    generated.seconds = stopwatch.Milliseconds() / 1000.0;
    outputGenerator.LogTimes(stopwatch.Milliseconds());
    if (config::HasOutput<ReportOutput>(config.outputs)) {
        generated.report = BuildReport(config, generated, outputGenerator.LayerTimings(), threads);
    }
    return generated;
}

std::string Summary(const GeneratedOutputs& generated) {
    std::vector<const Raster<float>*> heights;
    for (const GeneratedGrid& grid : generated.grids) {
        if (!grid.heights.Empty()) {
            heights.push_back(&grid.heights);
        }
    }
    std::ostringstream text;
    text << std::setprecision(3);
    if (heights.empty()) {
        text << "Generated the outputs, with no heights to evaluate,";
    } else if (heights.size() == 1) {
        text << "Generated a " << heights[0]->Width() << "x" << heights[0]->Height() << " terrain";
    } else {
        text << "Generated " << heights.size() << " grids (";
        for (std::size_t i = 0; i < heights.size(); ++i) {
            text << (i > 0 ? ", " : "") << heights[i]->Width() << "x" << heights[i]->Height();
        }
        text << ")";
    }
    text << " in " << generated.seconds << " s on " << DescribeBackend(generated.backend);
    return text.str();
}

std::vector<std::filesystem::path> WriteOutputs(const TerrainGenConfig& config,
                                                const GeneratedOutputs& generated,
                                                const std::filesystem::path& directory) {
    const std::optional<RasterGrid> source = SourceGrid(generated.generator);
    std::vector<std::filesystem::path> written;
    for (const OutputSpec& spec : config.outputs) {
        const std::string& relative = OutputPath(spec);
        if (relative.empty()) {
            continue;
        }
        const std::filesystem::path path = config::Resolve(relative, directory);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        const GeneratedGrid* found = nullptr;
        if (config::OutputArea(spec)) {
            const std::optional<RasterGrid> grid =
                config::OutputGrid(spec, source, generated.generator.surface);
            found = grid ? generated.Find(*grid) : nullptr;
            if (!found) {
                throw std::runtime_error("Nothing generated over the grid of " + path.string());
            }
        }
        const OutputContext context{generated, found};
        if (std::visit([&](const auto& output) { return Write(output, path, context); }, spec)) {
            written.push_back(path);
        }
    }
    return written;
}

}  // namespace terraingen
