// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Generating the terrain a configuration describes and writing its outputs.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/config/config.h"
#include "terraingen/mesh/io/icq.h"
#include "terraingen/mesh/mesh.h"
#include "terraingen/pipeline/feature_lists.h"
#include "terraingen/pipeline/generator.h"
#include "terraingen/pipeline/report.h"
#include "terraingen/raster/geotiff/geotiff.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/surface.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace terraingen {

/// What a configuration's outputs ask for over one grid, each computed once and shared by every
/// output over the grid. What no output asks for stays empty.
struct GeneratedGrid {
    RasterGrid grid{};        ///< The grid the outputs cover (`config::OutputGrid`).
    Raster<float> heights{};  ///< Heights in meters, for raster and terrain mesh outputs.
    std::vector<Raster<float>> albedo{};  ///< The albedo bands, for albedo outputs.
    /// The share of each material class, one raster per `MaterialClass`, for materials and
    /// photometry outputs.
    std::vector<Raster<float>> materials{};
    /// The Hapke parameters the materials mix to (`EvaluatePhotometry`), for photometry outputs.
    std::vector<Raster<float>> photometry{};
    /// The craters placed over the grid (`PlacedCraters`), for crater lists.
    std::optional<CraterFeatures> craters{};
    /// The boulders placed over the grid (`PlacedBoulders`), for boulder lists and models.
    std::optional<BoulderFeatures> boulders{};
    /// The heights as a mesh (`BuildMesh`), for terrain mesh outputs.
    std::optional<TriangleMesh> mesh{};
};

/// What a configuration's outputs ask for: its generator, what each grid they cover holds and the
/// whole body's mesh.
struct GeneratedOutputs {
    /// The generator built from the configuration, to evaluate or list features from further.
    TerrainGenerator generator;
    /// One per distinct grid an output covers, in output order.
    std::vector<GeneratedGrid> grids{};
    std::optional<TriangleMesh> bodyMesh{};  ///< The whole body (`BodyMesh`), for mesh outputs.
    /// The whole body as an ICQ shape model (`BodyIcq`), for mesh outputs to .icq paths.
    std::optional<IcqModel> bodyIcq{};
    /// The projection of a raster source, or geographic keys on a body without one.
    GeoKeys geoKeys{};
    double seconds = 0.0;            ///< Wall-clock time the outputs took to generate.
    Backend backend = Backend::Cpu;  ///< Where the heights were evaluated: `Cpu` or a GPU backend.
    /// The report, for a configuration with a report output; craters and boulders are reported
    /// only over grids a list or boulder models output placed them on.
    std::optional<TerrainReport> report{};

    /// The only grid.
    /// @throws std::runtime_error when the outputs cover no grid or several.
    const GeneratedGrid& Grid() const;
    /// The generated grid over `grid`, or null.
    const GeneratedGrid* Find(const RasterGrid& grid) const;
    /// The generated grid over `grid`, or null.
    GeneratedGrid* Find(const RasterGrid& grid);
};

/// Generates everything a configuration's outputs ask for, each computed once: `BuildGenerator`,
/// then over each distinct grid the outputs cover its heights (`Evaluate`), albedo, materials and
/// photometry, terrain mesh, and the craters and boulders placed there, the whole body's mesh and,
/// with a report output, the report (`GeneratedOutputs::report`). Nothing is written
/// (`WriteOutputs`).
/// @param directory Folder relative input paths resolve against; empty for the folder the
/// configuration was read from (`TerrainGenConfig::directory`).
/// @param threads Threads to use, 0 for every hardware thread; the result does not depend on it.
/// @param backend Where the heights are evaluated; empty for `DefaultBackend()`.
/// @throws ConfigError if the configuration is invalid.
/// @throws GeoTiffError if a raster cannot be read.
/// @throws std::runtime_error when `backend` is `Vulkan` and Vulkan is unavailable.
GeneratedOutputs GenerateFromConfig(const TerrainGenConfig& config,
                                    const std::filesystem::path& directory = {},
                                    unsigned threads = 0,
                                    std::optional<Backend> backend = std::nullopt);

/// One line describing a generated terrain, such as "Generated a WxH terrain in S s".
std::string Summary(const GeneratedOutputs& generated);

/// Writes each output of a configuration that has a path from what `GenerateFromConfig`
/// returned, computing nothing, and returns the paths written: rasters with `generated.geoKeys`,
/// meshes, crater and boulder lists and boulder models, each from the grid it covers
/// (`config::OutputGrid`), and reports as JSON (`ReportToJson`). Outputs without a path are not
/// written.
/// @param generated What `GenerateFromConfig` generated from `config`.
/// @param directory Folder relative output paths resolve against.
/// @throws std::runtime_error if an output cannot be written, or `generated` lacks what it needs.
std::vector<std::filesystem::path> WriteOutputs(const TerrainGenConfig& config,
                                                const GeneratedOutputs& generated,
                                                const std::filesystem::path& directory);

}  // namespace terraingen
