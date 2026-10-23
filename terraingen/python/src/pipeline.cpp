// SPDX-License-Identifier: BSD-3-Clause

#include "arrays.h"
#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "tables.h"

#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/json.h"
#include "terraingen/config/validate.h"
#include "terraingen/layers/evaluation_cache.h"
#include "terraingen/pipeline/body_mesh.h"
#include "terraingen/pipeline/generator.h"
#include "terraingen/pipeline/mesh_build.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

using OptionalPath = std::optional<std::filesystem::path>;

std::filesystem::path Directory(const OptionalPath& directory) {
    return directory.value_or(std::filesystem::path{});
}

/// `HeightsAt` over the rows of an (n, 2) array, as a float32 array.
nb::object HeightsAtArray(const PyTerrainGenerator& generator, const CpuMatrix<double>& points,
                          double spacing, unsigned threads, EvaluationCache* cache) {
    std::vector<Vec2d> positions;
    for (const auto& row : Rows<2>(points)) {
        positions.push_back({row[0], row[1]});
    }
    std::vector<float> heights;
    {
        nb::gil_scoped_release release;
        heights = HeightsAt(generator.Get(), positions, spacing, threads, cache);
    }
    return Column(std::move(heights));
}

/// @brief `text` parsed by Python's json module.
nb::object FromJson(const std::string& text) {
    return nb::module_::import_("json").attr("loads")(text);
}

/// @brief `grid`, or else the grid of `generator`'s source.
/// @throws std::invalid_argument when there is neither.
RasterGrid GridOr(const TerrainGenerator& generator, const std::optional<RasterGrid>& grid) {
    if (grid) {
        return *grid;
    }
    const std::optional<RasterGrid> bounded =
        generator.source ? BoundedGrid(*generator.source) : std::nullopt;
    if (!bounded) {
        throw std::invalid_argument(
            "A terrain without a source, or with a source without bounds, needs a grid");
    }
    return *bounded;
}

void BindGeneratorClass(nb::module_& m) {
    nb::class_<PyTerrainGenerator>(m, "TerrainGenerator",
                                   "A terrain generator: heights from `source` (the datum, 0 m, "
                                   "when None), plus each of `layers` in order.")
        .def(
            "__init__",
            [](PyTerrainGenerator* self, std::shared_ptr<Source> source,
               std::vector<std::shared_ptr<Layer>> layers, const Surface& surface) {
                new (self) PyTerrainGenerator{std::move(source), std::move(layers), surface};
            },
            "source"_a = nb::none(), "layers"_a = std::vector<std::shared_ptr<Layer>>{},
            "surface"_a = Surface{}, "`surface` must be the one the layers were built for.")
        .def_rw("surface", &PyTerrainGenerator::surface)
        .def_rw("source", &PyTerrainGenerator::source)
        .def_rw("layers", &PyTerrainGenerator::layers);
}

void BindFeatures(nb::module_& m) {
    nb::class_<CraterFeatures>(m, "CraterFeatures", "Craters and their attributes, in step.")
        .def_ro("craters", &CraterFeatures::craters, "The craters, as a CraterList.")
        .def_prop_ro(
            "table", [](const CraterFeatures& f) { return CraterTable(f.craters, f.attributes); },
            "The craters as NumPy columns 'x', 'y', 'diameter', 'age_gyr', 'id' and 'manual' (1 "
            "for manual craters), in the surface's coordinates, and their attributes "
            "'floor_depth' and 'rim_height' (m), 'depth_ratio', 'freshness' and 'complex'.")
        .def(
            "write_list",
            [](const CraterFeatures& f, const std::filesystem::path& path) {
                WriteCraterList(f.craters, path, InferListFormat(path.string()), f.attributes);
            },
            "path"_a, "Writes the craters as CSV, or JSON for a .json path.");
    nb::class_<BoulderFeatures>(m, "BoulderFeatures",
                                "Boulders, and those of them placed as their own 3D models.")
        .def_ro("boulders", &BoulderFeatures::boulders, "The boulders, a BoulderList.")
        .def_ro("models", &BoulderFeatures::models,
                "The boulder models, with their shapes and placements.")
        .def_prop_ro(
            "table", [](const BoulderFeatures& f) { return BoulderTable(f.boulders); },
            "The boulders as NumPy columns, as `BoulderLayer.boulders` gives them: 'x', 'y', "
            "'ground' (NaN when unknown), 'diameter', 'height', 'burial', 'yaw_deg', 'lean_deg', "
            "'id', 'model' (1 for boulder models), 'wear', 'parent', 'piece', 'rest' (a "
            "BoulderRest), 'tilt_deg' and 'tilt_azimuth_deg'.")
        .def(
            "write_list",
            [](const BoulderFeatures& f, const std::filesystem::path& path) {
                WriteBoulderList(f.boulders, path, InferListFormat(path.string()));
            },
            "path"_a, "Writes the boulders as CSV, or JSON for a .json path.")
        .def(
            "write_models",
            [](const BoulderFeatures& f, const std::filesystem::path& path, double creaseAngleDeg,
               bool instanced) {
                WriteBoulderModels(f.models, path, creaseAngleDeg, {}, {}, instanced);
            },
            "path"_a, "crease_angle_deg"_a = 60.0, "instanced"_a = true,
            "Writes the boulder models as a glTF scene (.glb or .gltf), OBJ (.obj) or STL (.stl): "
            "each shared shape once, drawn per boulder, or with `instanced` false every boulder as "
            "its own placed mesh.");
    m.def(
        "placed_craters",
        [](const PyTerrainGenerator& generator, std::optional<RasterGrid> grid, double spacing) {
            const TerrainGenerator built = generator.Get();
            return PlacedCraters(built, GridOr(built, grid), spacing);
        },
        "generator"_a, "grid"_a = nb::none(), "spacing"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The craters the generator's crater and secondary crater layers apply over `grid` (by "
        "default its source's) at `spacing` meters (0 for the grid's), in layer order.");
    m.def(
        "placed_boulders",
        [](const PyTerrainGenerator& generator, std::optional<RasterGrid> grid, double spacing) {
            const TerrainGenerator built = generator.Get();
            return PlacedBoulders(built, GridOr(built, grid), spacing);
        },
        "generator"_a, "grid"_a = nb::none(), "spacing"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The boulders the generator's boulder layers place over `grid` (by default its source's) "
        "at "
        "`spacing` meters (0 for the grid's), and the boulder models among them.");
}

void BindEvaluation(nb::module_& m) {
    m.def(
        "evaluate",
        [](const PyTerrainGenerator& generator, double spacing, unsigned threads,
           std::optional<RasterGrid> grid, std::optional<Backend> backend) {
            const TerrainGenerator built = generator.Get();
            return Evaluate(built, GridOr(built, grid),
                            EvaluationOptions{spacing, threads, backend});
        },
        "generator"_a, "spacing"_a = 0.0, "threads"_a = 0, "grid"_a = nb::none(),
        "backend"_a = nb::none(), nb::call_guard<nb::gil_scoped_release>(),
        "Evaluates `generator` over `grid` (by default its source's) at a sample spacing of "
        "`spacing` meters (0 for the grid's), on up to `threads` threads (0 for every hardware "
        "thread; the result does not depend on it), on `backend` (None for default_backend()). "
        "On Vulkan the heights match the CPU to within float rounding.");
    nb::class_<EvaluationCache>(
        m, "EvaluationCache",
        "Values point queries keep between calls, such as fitted crater datums. Without one, "
        "`height_at` and `heights_at` share a cache kept per generator; pass one to manage it. A "
        "cache belongs to one generator; start a new one after changing its source or layers.")
        .def(nb::init<std::size_t>(), "capacity"_a = std::size_t{1} << 20U,
             "A cache holding at most `capacity` values.");
    m.def(
        "height_at",
        [](const PyTerrainGenerator& generator, double x, double y, double spacing,
           EvaluationCache* cache) { return HeightAt(generator.Get(), x, y, spacing, cache); },
        "generator"_a, "x"_a, "y"_a, "spacing"_a, "cache"_a = nb::none(),
        nb::call_guard<nb::gil_scoped_release>(),
        "The generator's height at (x, y) sampled every `spacing` meters; equal to the cell "
        "centered there of `evaluate`. `cache`, an EvaluationCache, replaces the cache kept for "
        "the generator.");
    m.def("heights_at", &HeightsAtArray, "generator"_a, "points"_a, "spacing"_a, "threads"_a = 0,
          "cache"_a = nb::none(),
          "The generator's heights at `points`, an array of shape (n, 2) in surface coordinates, "
          "sampled every `spacing` meters, as a float32 array; each equals `height_at` there. "
          "Batches of points are much faster than one call per point. `cache`, an "
          "EvaluationCache, replaces the cache kept for the generator.");
    m.def(
        "fill_heights",
        [](const PyTerrainGenerator& generator, const RasterGrid& grid, CpuOutMatrix<float> heights,
           double spacing, unsigned threads, std::optional<Backend> backend) {
            if (heights.shape(0) != grid.height || heights.shape(1) != grid.width) {
                throw std::invalid_argument("Expected a float32 array of shape (" +
                                            std::to_string(grid.height) + ", " +
                                            std::to_string(grid.width) + ")");
            }
            const TerrainGenerator built = generator.Get();
            nb::gil_scoped_release release;
            FillHeights(built, grid, Span<float>(heights.data(), heights.size()),
                        EvaluationOptions{spacing, threads, backend});
        },
        "generator"_a, "grid"_a, nb::arg("heights").noconvert(), "spacing"_a = 0.0, "threads"_a = 0,
        "backend"_a = nb::none(),
        "Evaluates `generator` over `grid` into `heights`, a writable C-contiguous float32 array "
        "of "
        "shape (height, width), to the same bits as `evaluate` with the same options.");
}

/// Mesh options from the Python arguments.
BuildMeshOptions MeshOptions(std::vector<Bounds2d> cutouts, double skirt) {
    BuildMeshOptions options;
    options.cutouts = std::move(cutouts);
    options.skirt = skirt;
    return options;
}

/// The values of an array of shape (n, 3), as one span.
/// @throws std::invalid_argument for another number of columns.
template <typename T> Span<T> Triples(const CpuOutMatrix<T>& array, const char* name) {
    if (array.shape(1) != 3) {
        throw std::invalid_argument(std::string("Expected ") + name + " of shape (n, 3)");
    }
    return Span<T>(array.data(), array.size());
}

void BindMeshBuild(nb::module_& m) {
    nb::class_<MeshSize>(m, "MeshSize", "The number of vertices and triangles of a mesh.")
        .def_ro("vertices", &MeshSize::vertices)
        .def_ro("triangles", &MeshSize::triangles)
        .def("__eq__", [](const MeshSize& a, const MeshSize& b) { return a == b; })
        .def("__repr__", [](const MeshSize& s) {
            return "MeshSize(vertices=" + std::to_string(s.vertices) +
                   ", triangles=" + std::to_string(s.triangles) + ")";
        });
    m.def(
        "build_mesh",
        [](const Raster<float>& heights, const Surface& surface, const Vec2d& site,
           std::vector<Bounds2d> cutouts, double skirt) {
            return BuildMesh(heights, surface, site, MeshOptions(std::move(cutouts), skirt));
        },
        "heights"_a, "surface"_a = Surface{}, "site"_a = Vec2d{0.0, 0.0},
        "cutouts"_a = std::vector<Bounds2d>{}, "skirt"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The heights as a mesh, a vertex per cell with data, in the frame boulder models are "
        "written in: world coordinates on a plane, east-north-up meters of `site` on a sphere. "
        "Squares whose four cell centers lie in one of the `cutouts` boxes (min x, min y, max x, "
        "max y) are left out, and a skirt `skirt` meters deep hangs below every boundary edge. "
        "The mesh's `positions` and `triangles` are NumPy views of it, without a copy.");
    m.def(
        "get_mesh_size",
        [](const Raster<float>& heights, std::vector<Bounds2d> cutouts, double skirt) {
            return GetMeshSize(heights, MeshOptions(std::move(cutouts), skirt));
        },
        "heights"_a, "cutouts"_a = std::vector<Bounds2d>{}, "skirt"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The MeshSize `build_mesh` makes of the heights with these cutouts and skirt, to size the "
        "arrays `fill_mesh` fills.");
    m.def(
        "fill_mesh",
        [](const Raster<float>& heights, CpuOutMatrix<double> positions,
           CpuOutMatrix<std::int32_t> triangles, const Surface& surface, const Vec2d& site,
           std::vector<Bounds2d> cutouts, double skirt) {
            const Span<double> points = Triples(positions, "positions");
            const Span<std::int32_t> indices = Triples(triangles, "triangles");
            const BuildMeshOptions options = MeshOptions(std::move(cutouts), skirt);
            nb::gil_scoped_release release;
            FillMesh(heights, points, indices, surface, site, options);
        },
        "heights"_a, nb::arg("positions").noconvert(), nb::arg("triangles").noconvert(),
        "surface"_a = Surface{}, "site"_a = Vec2d{0.0, 0.0}, "cutouts"_a = std::vector<Bounds2d>{},
        "skirt"_a = 0.0,
        "Writes the mesh `build_mesh` makes, bit for bit, into writable C-contiguous arrays: "
        "`positions` float64 of shape (vertices, 3) and `triangles` int32 of shape (triangles, "
        "3), sized by `get_mesh_size`.");
}

void BindBodyMesh(nb::module_& m) {
    m.def(
        "body_mesh",
        [](const PyTerrainGenerator& generator, std::size_t cellsPerFace, double spacing,
           unsigned threads) { return BodyMesh(generator.Get(), cellsPerFace, spacing, threads); },
        "generator"_a, "cells_per_face"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "A mesh of the whole body a terrain on a sphere or ellipsoid lies on: six cube faces of "
        "`cells_per_face` squared quads, each vertex at the radius plus the terrain's height "
        "there.");
    m.def(
        "body_icq",
        [](const PyTerrainGenerator& generator, std::size_t q, double spacing, unsigned threads) {
            return BodyIcq(generator.Get(), q, spacing, threads);
        },
        "generator"_a, "q"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The whole body a terrain on a sphere or ellipsoid lies on as an ICQ shape model with "
        "`q` intervals per face edge, each label at the radius plus the terrain's height there.");
}

/// @brief Registers ConfigError, a ValueError with the JSON `path` of the offending value, as
/// GeoTiffError is registered, then a translator that sets the path.
void BindConfigError(nb::module_& m) {
    static const nb::exception<ConfigError> configError(m, "ConfigError", PyExc_ValueError);
    configError.attr("__doc__") = "An invalid configuration; `path` is the JSON path of the "
                                  "offending value, empty for the whole document.";
    nb::register_exception_translator([](const std::exception_ptr& error, void*) {
        try {
            std::rethrow_exception(error);
        } catch (const ConfigError& config) {
            nb::object instance = configError(config.what());
            instance.attr("path") = config.Path();
            PyErr_SetObject(configError.ptr(), instance.ptr());
        }
    });
}

void BindConfigIo(nb::module_& m, nb::class_<TerrainGenConfig>& config) {
    config.def(nb::init<>(), "The default configuration.")
        .def_static(
            "from_dict",
            [](nb::handle data) {
                const nb::object json = nb::module_::import_("json");
                return ParseConfig(nb::cast<std::string>(json.attr("dumps")(data)));
            },
            "data"_a, "Parses a configuration from a dict of JSON values.")
        .def(
            "to_dict", [](const TerrainGenConfig& c) { return FromJson(ConfigToJson(c, -1)); },
            "The configuration as a dict of JSON values, every value written out.");
    m.def("parse_config", &ParseConfig, "json"_a,
          "Parses a configuration from JSON text. Raises ConfigError if it does not fit the "
          "format.");
    m.def("load_config", &LoadConfig, "path"_a,
          "Reads the configuration in the JSON file at `path`.");
    m.def("config_to_json", &ConfigToJson, "config"_a, "indent"_a = 2,
          "The configuration as JSON with every value written out.");
}

void BindConfigQueries(nb::class_<TerrainGenConfig>& config) {
    config
        .def(
            "validate",
            [](const TerrainGenConfig& c, const OptionalPath& directory) {
                ValidateConfig(c, Directory(directory));
            },
            "directory"_a = nb::none(),
            "Checks everything parsing cannot (grid limits, raster files, model parameters, "
            "expected crater counts); relative input paths resolve against `directory`. Raises "
            "ConfigError naming the first invalid value.")
        .def("__eq__", [](const TerrainGenConfig& a, const TerrainGenConfig& b) { return a == b; })
        .def(
            "body",
            [](const TerrainGenConfig& c, const OptionalPath& directory) {
                return BuildSurface(c.body, Directory(directory));
            },
            "directory"_a = nb::none(),
            "The Surface the configuration's terrain lies on; a shape model's path resolves "
            "against `directory`.")
        .def(
            "output_grids",
            [](const TerrainGenConfig& c, const OptionalPath& directory) {
                return ValidateConfig(c, Directory(directory)).outputGrids;
            },
            "directory"_a = nb::none(),
            "The grid each output covers, in order (None for a mesh), after validating the "
            "configuration.")
        .def("__repr__", [](const TerrainGenConfig& c) {
            return "TerrainGenConfig(" + std::to_string(c.layers.size()) + " layers, " +
                   std::to_string(c.outputs.size()) + " outputs)";
        });
}

void BindConfig(nb::module_& m) {
    BindConfigError(m);
    nb::class_<TerrainGenConfig> config(
        m, "TerrainGenConfig",
        "A terrain configuration: a body, optionally a source, layers, "
        "outputs and optionally an albedo, as the JSON configs of the "
        "terraingen command describe.");
    BindConfigIo(m, config);
    BindConfigQueries(config);
    m.def(
        "config_catalog", [] { return FromJson(ConfigCatalog(-1)); },
        "Every configuration type with its defaults, as a dict: the lists 'body', 'source', "
        "'layers' and 'outputs', the 'albedo', and the 'models' (densities, ages, "
        "degradation, profiles, boulder sizes, region shapes and albedo sources and layers).");
    m.def(
        "config_schema", [] { return FromJson(ConfigSchema(-1)); },
        "The structure of a configuration, for editors, as a dict: 'root' (the configuration "
        "object) and 'definitions' (each struct and choice of types by id, referred to as "
        "{'kind': 'ref', 'ref': id}). Every field has a 'name', 'description', 'unit', 'schema' "
        "and 'default'; schema kinds are number, integer, boolean, string, enum (with 'choices' "
        "and their 'descriptions'), optional, array, object and choice (with 'alternatives').");
}

/// @brief `grid`, or else the first grid in `generated`.
/// @throws std::invalid_argument when there is neither.
RasterGrid GeneratedGridOr(const GeneratedOutputs& generated,
                           const std::optional<RasterGrid>& grid) {
    if (grid) {
        return *grid;
    }
    if (generated.grids.empty()) {
        throw std::invalid_argument("The terrain generated no grid; give one");
    }
    return generated.grids.front().grid;
}

/// @brief Each of `items`, a member of the object `owner`, as a list of views kept alive by it.
template <typename T> nb::list Views(nb::handle owner, std::vector<T>& items) {
    nb::list views;
    for (T& item : items) {
        views.append(nb::cast(&item, nb::rv_policy::reference_internal, owner));
    }
    return views;
}

/// @brief `value`, a member of the object `owner`, as a view kept alive by it, or None unset.
template <typename T> nb::object View(nb::handle owner, std::optional<T>& value) {
    return value ? nb::cast(&*value, nb::rv_policy::reference_internal, owner) : nb::none();
}

void BindGeneratedGrid(nb::module_& m) {
    nb::class_<GeneratedGrid>(m, "GeneratedGrid",
                              "What the outputs over one grid asked for, each computed once; "
                              "what none asked for is empty or None. Every value is a view "
                              "into the result, without a copy.")
        .def_ro("grid", &GeneratedGrid::grid, "The RasterGrid the outputs cover.")
        .def_prop_ro(
            "heights", [](GeneratedGrid& g) -> Raster<float>& { return g.heights; },
            nb::rv_policy::reference_internal,
            "Heights in meters over the grid, for raster and terrain mesh outputs; an empty "
            "Raster otherwise.")
        .def_prop_ro(
            "albedo",
            [](nb::handle self) { return Views(self, nb::cast<GeneratedGrid&>(self).albedo); },
            "Albedo bands, for albedo outputs.")
        .def_prop_ro(
            "materials",
            [](nb::handle self) { return Views(self, nb::cast<GeneratedGrid&>(self).materials); },
            "The share of each MaterialClass, one raster per class, for materials and "
            "photometry outputs.")
        .def_prop_ro(
            "photometry",
            [](nb::handle self) { return Views(self, nb::cast<GeneratedGrid&>(self).photometry); },
            "The Hapke single-scattering albedo, b, c and roughness in degrees the materials mix "
            "to, for photometry outputs.")
        .def_prop_ro(
            "craters",
            [](nb::handle self) { return View(self, nb::cast<GeneratedGrid&>(self).craters); },
            "The CraterFeatures placed over the grid, for crater lists; None otherwise.")
        .def_prop_ro(
            "boulders",
            [](nb::handle self) { return View(self, nb::cast<GeneratedGrid&>(self).boulders); },
            "The BoulderFeatures placed over the grid, for boulder lists and models; None "
            "otherwise.")
        .def_prop_ro(
            "mesh", [](nb::handle self) { return View(self, nb::cast<GeneratedGrid&>(self).mesh); },
            "The heights as a TriangleMesh, for terrain mesh outputs; None otherwise.");
}

void BindGeneratedOutputs(nb::module_& m) {
    BindGeneratedGrid(m);
    nb::class_<GeneratedOutputs> generated(m, "GeneratedOutputs",
                                           "What a configuration's outputs ask for, each "
                                           "computed once: a GeneratedGrid per grid they cover "
                                           "and the whole body's mesh.");
    generated
        .def_prop_ro(
            "grids",
            [](nb::handle self) { return Views(self, nb::cast<GeneratedOutputs&>(self).grids); },
            "The GeneratedGrid of each distinct grid an output covers, in output order.")
        .def(
            "grid",
            [](nb::handle self) {
                const GeneratedGrid& only = nb::cast<const GeneratedOutputs&>(self).Grid();
                return nb::cast(&only, nb::rv_policy::reference_internal, self);
            },
            "The only GeneratedGrid. Raises RuntimeError when the outputs cover none or several.")
        .def(
            "find",
            [](nb::handle self, const RasterGrid& grid) -> nb::object {
                GeneratedGrid* found = nb::cast<GeneratedOutputs&>(self).Find(grid);
                return found ? nb::cast(found, nb::rv_policy::reference_internal, self)
                             : nb::none();
            },
            "grid"_a, "The GeneratedGrid over `grid`, or None.")
        .def_prop_ro(
            "body_mesh",
            [](nb::handle self) { return View(self, nb::cast<GeneratedOutputs&>(self).bodyMesh); },
            "The whole body as a TriangleMesh, for mesh outputs; None otherwise.")
        .def_prop_ro(
            "body_icq",
            [](nb::handle self) { return View(self, nb::cast<GeneratedOutputs&>(self).bodyIcq); },
            "The whole body as an IcqModel, for mesh outputs to .icq paths; None otherwise.")
        .def_prop_ro(
            "report",
            [](nb::handle self) { return View(self, nb::cast<GeneratedOutputs&>(self).report); },
            "The TerrainReport, for a configuration with a report output; None otherwise. Its "
            "grids list craters and boulders only where a list output asked for them.")
        .def_prop_ro(
            "generator", [](const GeneratedOutputs& g) { return FromGenerator(g.generator); },
            "A copy of the TerrainGenerator built from the configuration.");
    generated
        .def(
            "placed_craters",
            [](const GeneratedOutputs& g, std::optional<RasterGrid> grid, double spacing) {
                return PlacedCraters(g.generator, GeneratedGridOr(g, grid), spacing);
            },
            "grid"_a = nb::none(), "spacing"_a = 0.0,
            "The craters the terrain's layers apply over `grid` (by default its first generated "
            "grid) at `spacing` meters (0 for the grid's).")
        .def(
            "placed_boulders",
            [](const GeneratedOutputs& g, std::optional<RasterGrid> grid, double spacing) {
                return PlacedBoulders(g.generator, GeneratedGridOr(g, grid), spacing);
            },
            "grid"_a = nb::none(), "spacing"_a = 0.0,
            "The boulders the terrain's layers place over `grid` (by default its first generated "
            "grid) at `spacing` meters (0 for the grid's), and the models among them.")
        .def_ro("seconds", &GeneratedOutputs::seconds,
                "Wall-clock time the heights and albedo took to evaluate, in seconds.")
        .def_ro("backend", &GeneratedOutputs::backend,
                "Where the heights were evaluated: CPU or a GPU backend.")
        .def("summary", &Summary, "One line describing the terrain: its size and time.");
}

void BindRun(nb::module_& m) {
    m.def(
        "generate_from_config",
        [](const TerrainGenConfig& config, const OptionalPath& directory, unsigned threads,
           std::optional<Backend> backend) {
            return GenerateFromConfig(config, Directory(directory), threads, backend);
        },
        "config"_a, "directory"_a = nb::none(), "threads"_a = 0, "backend"_a = nb::none(),
        nb::call_guard<nb::gil_scoped_release>(),
        "Generates everything `config`'s outputs ask for, each computed once, and returns it as "
        "GeneratedOutputs without writing anything (write_outputs writes it), on up to "
        "`threads` threads (0 for every hardware thread; the result does not depend on it), "
        "evaluating the heights on `backend` (None for default_backend()). Relative input "
        "paths resolve against `directory`. Raises ConfigError for an invalid configuration.");
    m.def(
        "build_generator",
        [](const TerrainGenConfig& config, const OptionalPath& directory,
           std::shared_ptr<const Source> source) {
            return FromGenerator(
                source ? BuildGenerator(config, std::move(source), Directory(directory))
                       : BuildGenerator(config, Directory(directory)));
        },
        "config"_a, "directory"_a = nb::none(), "source"_a = nb::none(),
        "The TerrainGenerator a configuration describes, as library objects to evaluate, tile, "
        "query at points or change; relative input paths resolve against `directory`. With a "
        "`source`, the generator is built on it in place of a source of the configuration's "
        "own, which it must not give.");
    m.def(
        "build_albedo",
        [](const TerrainGenConfig& config, const PyTerrainGenerator& generator,
           const OptionalPath& directory) {
            return FromAlbedo(BuildAlbedo(config, generator.Get(), Directory(directory)));
        },
        "config"_a, "generator"_a, "directory"_a = nb::none(),
        "The Albedo a configuration describes for `generator`, built from the same configuration "
        "by `build_generator`.");
    m.def("write_outputs", &WriteOutputs, "config"_a, "generated"_a, "directory"_a,
          nb::call_guard<nb::gil_scoped_release>(),
          "Writes each of `config`'s outputs that has a path from `generated`, what "
          "generate_from_config returned, computing nothing; resolves relative paths against "
          "`directory` and returns the paths written, as pathlib.Path.");
}

}  // namespace

void BindPipeline(nb::module_& m) {
    BindGeneratorClass(m);
    BindFeatures(m);
    BindEvaluation(m);
    BindMeshBuild(m);
    BindBodyMesh(m);
    BindConfig(m);
    BindGeneratedOutputs(m);
    BindRun(m);
}

}  // namespace terraingen::python
