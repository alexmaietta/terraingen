// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/config/generate.h"

#include "config/terrain.h"
#include "terraingen/config/build.h"
#include "terraingen/config/json.h"
#include "terraingen/gpu/backend.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/mesh_build.h"
#include "terraingen/pipeline/body_mesh.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

/// Whether an output needs the heights over its grid: a raster or a terrain mesh.
bool NeedsHeights(const OutputSpec& output) {
    return config::HoldsAny<HeightmapOutput, AlbedoOutput, TerrainMeshOutput, MaterialsOutput,
                            PhotometryOutput>(output);
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

/// The distinct grids of `grids`, in order.
std::vector<RasterGrid> DistinctGrids(const std::vector<std::optional<RasterGrid>>& grids) {
    std::vector<RasterGrid> distinct;
    for (const std::optional<RasterGrid>& grid : grids) {
        if (grid && std::find(distinct.begin(), distinct.end(), *grid) == distinct.end()) {
            distinct.push_back(*grid);
        }
    }
    return distinct;
}

/// Generates what each output asks for over its grid (`GeneratedOutputs::outputGrids`), once per
/// grid and result, timing each step.
class OutputGenerator {
  public:
    /// @param outputs What each output covers (`config::CheckedTerrain::outputs`).
    OutputGenerator(const ConfigSpec& config, GeneratedOutputs& generated,
                    const std::filesystem::path& directory, unsigned threads,
                    std::vector<config::OutputGrid> outputs)
        : config_(config), generated_(generated), directory_(directory), threads_(threads),
          outputs_(std::move(outputs)), grids_(DistinctGrids(generated.outputGrids)) {
        for (std::size_t n = 0; n < config.layers.size(); ++n) {
            steps_.push_back(
                {"layer " + std::to_string(n + 1) + " " + LayerType(config.layers[n])});
        }
        for (const char* name : {"albedo", "materials", "features", "meshes"}) {
            steps_.push_back({name});
        }
    }

    /// Generates what output `i` asks for, unless an earlier output did.
    void Generate(std::size_t i) {
        const OutputSpec& output = config_.outputs[i];
        if (const auto* mesh = std::get_if<MeshOutput>(&output)) {
            AddBodyMesh(*mesh);
            return;
        }
        const std::optional<RasterGrid>& grid = generated_.outputGrids[i];
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
        AddFeatures(output, outputs_[i].featureSpacing, target);
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
    std::vector<LayerTiming> LayerTimings() const {
        std::vector<LayerTiming> timings;
        for (std::size_t n = 0; n < config_.layers.size(); ++n) {
            timings.push_back({LayerType(config_.layers[n]), steps_[n].milliseconds / 1000.0});
        }
        return timings;
    }

  private:
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

    /// How grids are evaluated: on the configured threads and backend.
    EvaluationOptions Options() const {
        return EvaluationOptions{}.Threads(threads_).On(generated_.backend);
    }

    void AddHeights(GeneratedGrid& target) {
        const Terrain& terrain = generated_.terrain;
        const auto index = static_cast<std::size_t>(
            std::find(grids_.begin(), grids_.end(), target.grid) - grids_.begin());
        LogInfo("pipeline", "Grid ", index + 1, " of ", grids_.size(), ": ", target.grid.width, "x",
                target.grid.height, " at ", FormatSpacing(target.grid.Spacing(terrain.Surface())));
        EvaluationOptions options = Options();
        options.onLayer = [this](std::size_t step, double milliseconds) {
            Timed(step, milliseconds);
        };
        target.heights = terrain.Evaluate(target.grid, options);
    }

    void AddAlbedo(GeneratedGrid& target) {
        const Stopwatch stopwatch;
        const Terrain& terrain = generated_.terrain;
        if (!albedo_) {
            albedo_ =
                config::BuildValidAlbedo(*config_.albedo, config_.layers, terrain, directory_);
        }
        target.albedo = EvaluateAlbedo(*albedo_, target.grid, terrain.Context(0.0, threads_));
        Timed(Step::Albedo, stopwatch.Milliseconds());
    }

    /// Adds the material map a materials or photometry output asks for, and the photometry.
    void AddMaterials(const OutputSpec& output, GeneratedGrid& target) {
        if (config::HoldsAny<MaterialsOutput, PhotometryOutput>(output) &&
            target.materials.empty()) {
            const Stopwatch stopwatch;
            target.materials = EvaluateMaterials(generated_.terrain, config_.materials->options,
                                                 target.grid, Options());
            Timed(Step::Materials, stopwatch.Milliseconds());
        }
        if (std::holds_alternative<PhotometryOutput>(output) && target.photometry.empty()) {
            const Stopwatch stopwatch;
            target.photometry = EvaluatePhotometry(
                target.materials, config::BuildPhotometry(config_.materials->photometry));
            Timed(Step::Materials, stopwatch.Milliseconds());
        }
    }

    /// Places the craters a crater list asks for, or the boulders a boulder list or models do, at
    /// `spacing` meters (0 for the grid's own).
    void AddFeatures(const OutputSpec& output, double spacing, GeneratedGrid& target) {
        const Terrain& terrain = generated_.terrain;
        if (std::holds_alternative<CraterListOutput>(output) && !target.craters) {
            const Stopwatch stopwatch;
            target.craters = terrain.Craters(target.grid, spacing);
            Timed(Step::Features, stopwatch.Milliseconds());
        }
        if (config::HoldsAny<BoulderListOutput, BoulderModelsOutput>(output) && !target.boulders) {
            const Stopwatch stopwatch;
            target.boulders = terrain.Boulders(target.grid, spacing);
            Timed(Step::Features, stopwatch.Milliseconds());
        }
    }

    void AddTerrainMesh(const TerrainMeshOutput& output, GeneratedGrid& target) {
        const Stopwatch stopwatch;
        BuildMeshOptions options;
        options.skirt = output.skirt;
        for (const ExtentSpec& cutout : output.cutouts) {
            options.cutouts.push_back(config::ExtentBox(cutout));
        }
        const Vec2d site = output.siteDeg ? Vec2d{(*output.siteDeg)[0], (*output.siteDeg)[1]}
                                          : target.grid.Bounds().Center();
        target.mesh = BuildMesh(target.heights, generated_.terrain.Surface(), site, options);
        Timed(Step::Meshes, stopwatch.Milliseconds());
    }

    void AddBodyMesh(const MeshOutput& output) {
        const Stopwatch stopwatch;
        const Terrain& terrain = generated_.terrain;
        const double spacing = output.spacing.value_or(0.0);
        if (IsIcqPath(output.path)) {
            if (!generated_.bodyIcq) {
                generated_.bodyIcq = BodyIcq(terrain, output.cellsPerFace, spacing, threads_);
                Timed(Step::Meshes, stopwatch.Milliseconds());
            }
        } else if (!generated_.bodyMesh) {
            generated_.bodyMesh = BodyMesh(terrain, output.cellsPerFace, spacing, threads_);
            Timed(Step::Meshes, stopwatch.Milliseconds());
        }
    }

    const ConfigSpec& config_;
    GeneratedOutputs& generated_;
    const std::filesystem::path& directory_;
    unsigned threads_;
    std::vector<config::OutputGrid> outputs_;
    std::vector<RasterGrid> grids_;
    std::vector<StepTime> steps_;
    std::optional<Albedo> albedo_;
};

/// A field-list `IO` that finds a layer's top-level "seed", or its inner layer's in a masked
/// layer.
class SeedFinder {
  public:
    template <typename T>
    void operator()(std::string_view key, T& value, std::string_view /*description*/ = {},
                    std::string_view /*unit*/ = {}, const FieldRange& /*range*/ = {}) {
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
        std::visit([this](auto& spec) { Fields(*this, spec); }, layer);
    }

    std::optional<std::uint32_t> seed;
};

/// The top-level "seed" of each layer that has one, in order, a masked layer's from inside.
std::vector<std::uint32_t> LayerSeeds(const ConfigSpec& config) {
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

/// The density of the terrain's first top-level crater layer, or null without one.
std::shared_ptr<const SizeFrequencyDistribution> CraterTarget(const Terrain& terrain) {
    for (const LayerPtr& layer : terrain.Layers()) {
        if (const auto* craters = dynamic_cast<const CraterLayer*>(layer.get())) {
            return craters->Models().density;
        }
    }
    return nullptr;
}

/// The report of what was generated from `config`, with each layer's summed `timings`.
TerrainReport BuildReport(const ConfigSpec& config, const GeneratedOutputs& generated,
                          std::vector<LayerTiming> timings, unsigned threads) {
    TerrainReport report;
    const std::shared_ptr<const SizeFrequencyDistribution> target = CraterTarget(generated.terrain);
    for (const GeneratedGrid& grid : generated.grids) {
        if (grid.heights.Empty()) {
            continue;
        }
        report.grids.push_back(ReportGrid(
            grid.heights, generated.terrain.Surface(), grid.craters ? &*grid.craters : nullptr,
            grid.boulders ? &*grid.boulders : nullptr, target.get(), threads));
    }
    report.layers = std::move(timings);
    report.backend = std::string(BackendName(generated.backend));
    report.threads = threads;
    report.seeds = LayerSeeds(config);
    report.seconds = generated.seconds;
    return report;
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

GeneratedOutputs GenerateFromConfig(const TerrainGenConfig& value,
                                    const std::filesystem::path& given, unsigned threads,
                                    std::optional<Backend> backend) {
    const ConfigSpec& config = value.Spec();
    const std::filesystem::path directory = ConfigDirectory(config, given);
    config::CheckedTerrain checked = config::BuildCheckedTerrain(config, directory);
    GeneratedOutputs generated{std::move(checked.terrain)};
    generated.outputGrids = std::move(checked.summary.outputGrids);
    generated.backend = ResolveBackend(backend.value_or(DefaultBackend()));
    const Stopwatch stopwatch;
    OutputGenerator outputGenerator(config, generated, directory, threads,
                                    std::move(checked.outputs));
    for (std::size_t i = 0; i < config.outputs.size(); ++i) {
        outputGenerator.Generate(i);
    }
    generated.geoKeys = generated.terrain.Keys();
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

}  // namespace terraingen
