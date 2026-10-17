#include "bindings.h"
#include "common.h"
#include "objects.h"
#include "tables.h"

#include "terraingen/config/build.h"
#include "terraingen/config/generate.h"
#include "terraingen/config/json.h"
#include "terraingen/config/validate.h"
#include "terraingen/pipeline/body_mesh.h"
#include "terraingen/pipeline/features.h"
#include "terraingen/pipeline/height_mesh.h"
#include "terraingen/pipeline/terrain.h"
#include "terraingen/pipeline/tiles.h"

#include <nanobind/operators.h>

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

using PointArray = nb::ndarray<const double, nb::ndim<2>, nb::device::cpu, nb::c_contig>;

/// `EvaluatePoints` over the rows of an (n, 2) array, as a float32 array.
nb::object EvaluatePointArray(const PyTerrain& terrain, const PointArray& points, double spacing,
                              unsigned threads) {
    if (points.shape(1) != 2) {
        throw std::invalid_argument("Points must be an array of shape (n, 2)");
    }
    std::vector<Vec2d> positions(points.shape(0));
    for (std::size_t i = 0; i < positions.size(); ++i) {
        positions[i] = {points.data()[2 * i], points.data()[2 * i + 1]};
    }
    std::vector<float> heights;
    {
        nb::gil_scoped_release release;
        heights = EvaluatePoints(terrain.Get(), positions, spacing, threads);
    }
    return Column(std::move(heights));
}

/// @brief `text` parsed by Python's json module.
nb::object FromJson(const std::string& text) {
    return nb::module_::import_("json").attr("loads")(text);
}

/// @brief `grid`, or else the grid of `terrain`'s source.
/// @throws std::invalid_argument when there is neither.
RasterGrid GridOr(const Terrain& terrain, const std::optional<RasterGrid>& grid) {
    if (grid) {
        return *grid;
    }
    if (!terrain.source) {
        throw std::invalid_argument("A terrain without a source needs a grid");
    }
    return terrain.source->Grid();
}

/// @brief The list format a file's extension asks for: JSON for .json, CSV otherwise.
ListFormat FormatOf(const std::filesystem::path& path) {
    return path.extension() == ".json" ? ListFormat::Json : ListFormat::Csv;
}

void BindTerrainClass(nb::module_& m) {
    nb::class_<PyTerrain>(m, "Terrain",
                          "A terrain: heights from `source` (the datum, 0 m, when None), plus "
                          "each of `layers` in order.")
        .def(
            "__init__",
            [](PyTerrain* self, std::shared_ptr<Source> source,
               std::vector<std::shared_ptr<Layer>> layers, const Surface& surface) {
                new (self) PyTerrain{std::move(source), std::move(layers), surface};
            },
            "source"_a = nb::none(), "layers"_a = std::vector<std::shared_ptr<Layer>>{},
            "surface"_a = Surface{}, "`surface` must be the one the layers were built for.")
        .def_rw("surface", &PyTerrain::surface)
        .def_rw("source", &PyTerrain::source)
        .def_rw("layers", &PyTerrain::layers);
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
            "write",
            [](const CraterFeatures& f, const std::filesystem::path& path) {
                WriteCraterList(f.craters, path, FormatOf(path), f.attributes);
            },
            "path"_a, "Writes the craters as CSV, or JSON for a .json path.");
    nb::class_<BoulderFeatures>(m, "BoulderFeatures",
                                "Boulders, and those of them placed as their own 3D models.")
        .def_ro("boulders", &BoulderFeatures::boulders, "The boulders, as Boulder objects.")
        .def_ro("models", &BoulderFeatures::models,
                "The boulder models, with their shapes and placements.")
        .def_prop_ro(
            "table", [](const BoulderFeatures& f) { return BoulderTable(f.boulders); },
            "The boulders as NumPy columns 'x', 'y', 'ground' (NaN when unknown), 'diameter', "
            "'height', 'burial', 'yaw', 'lean', 'id' and 'model' (1 for boulder models).")
        .def(
            "write_list",
            [](const BoulderFeatures& f, const std::filesystem::path& path) {
                WriteBoulderList(f.boulders, path, FormatOf(path));
            },
            "path"_a, "Writes the boulders as CSV, or JSON for a .json path.")
        .def(
            "write_models",
            [](const BoulderFeatures& f, const std::filesystem::path& path, double creaseAngle,
               bool instanced) {
                WriteBoulderModels(f.models, path, creaseAngle, {}, {}, instanced);
            },
            "path"_a, "crease_angle"_a = 60.0, "instanced"_a = true,
            "Writes the boulder models as a glTF scene (.glb or .gltf), OBJ (.obj) or STL (.stl): "
            "each shared shape once, drawn per boulder, or with `instanced` false every boulder as "
            "its own placed mesh.");
    m.def(
        "placed_craters",
        [](const PyTerrain& terrain, std::optional<RasterGrid> grid, double spacing) {
            const Terrain built = terrain.Get();
            return PlacedCraters(built, GridOr(built, grid), spacing);
        },
        "terrain"_a, "grid"_a = nb::none(), "spacing"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The craters the terrain's crater and secondary crater layers apply over `grid` (by "
        "default its source's) at `spacing` metres (0 for the grid's), in layer order.");
    m.def(
        "placed_boulders",
        [](const PyTerrain& terrain, std::optional<RasterGrid> grid, double spacing) {
            const Terrain built = terrain.Get();
            return PlacedBoulders(built, GridOr(built, grid), spacing);
        },
        "terrain"_a, "grid"_a = nb::none(), "spacing"_a = 0.0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The boulders the terrain's boulder layers place over `grid` (by default its source's) at "
        "`spacing` metres (0 for the grid's), and the boulder models among them.");
}

void BindEvaluation(nb::module_& m) {
    m.def(
        "evaluate",
        [](const PyTerrain& terrain, double spacing, unsigned threads,
           std::optional<RasterGrid> grid, std::optional<Backend> backend) {
            const Terrain built = terrain.Get();
            return Evaluate(built, GridOr(built, grid),
                            EvaluationOptions{spacing, threads, backend});
        },
        "terrain"_a, "spacing"_a = 0.0, "threads"_a = 0, "grid"_a = nb::none(),
        "backend"_a = nb::none(), nb::call_guard<nb::gil_scoped_release>(),
        "Evaluates `terrain` over `grid` (by default its source's) at a sample spacing of "
        "`spacing` metres (0 for the grid's), on up to `threads` threads (0 for every hardware "
        "thread; the result does not depend on it), on `backend` (None for default_backend()). "
        "On Vulkan the heights match the CPU to within float rounding.");
    m.def(
        "evaluate_at",
        [](const PyTerrain& terrain, double x, double y, double spacing) {
            return EvaluateAt(terrain.Get(), x, y, spacing);
        },
        "terrain"_a, "x"_a, "y"_a, "spacing"_a, nb::call_guard<nb::gil_scoped_release>(),
        "The terrain's height at (x, y) sampled every `spacing` metres; equal to the cell centred "
        "there of `evaluate`.");
    m.def("evaluate_points", &EvaluatePointArray, "terrain"_a, "points"_a, "spacing"_a,
          "threads"_a = 0,
          "The terrain's heights at `points`, an array of shape (n, 2) in surface coordinates, "
          "sampled every `spacing` metres, as a float32 array; each equals `evaluate_at` there. "
          "Batches of points are much faster than one call per point.");
    m.def("height_mesh", &HeightMesh, "heights"_a, "surface"_a = Surface{},
          "site"_a = Vec2d{0.0, 0.0},
          "The heights as a mesh, a vertex per cell with data, in the frame boulder models are "
          "written in: world coordinates on a plane, east-north-up metres of `site` on a sphere.");
    m.def(
        "body_mesh",
        [](const PyTerrain& terrain, std::size_t cellsPerFace, double spacing, unsigned threads) {
            return BodyMesh(terrain.Get(), cellsPerFace, spacing, threads);
        },
        "terrain"_a, "cells_per_face"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "A mesh of the whole body a terrain on a sphere or ellipsoid lies on: six cube faces of "
        "`cells_per_face` squared quads, each vertex at the radius plus the terrain's height "
        "there.");
    m.def(
        "body_icq",
        [](const PyTerrain& terrain, std::size_t q, double spacing, unsigned threads) {
            return BodyIcq(terrain.Get(), q, spacing, threads);
        },
        "terrain"_a, "q"_a, "spacing"_a = 0.0, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "The whole body a terrain on a sphere or ellipsoid lies on as an ICQ shape model with "
        "`q` intervals per face edge, each label at the radius plus the terrain's height there.");
}

void BindTileKey(nb::module_& m) {
    nb::class_<TileKey>(m, "TileKey", "Tile (x, y) of a tile scheme's level `level`.")
        .def(
            "__init__",
            [](TileKey* self, int level, std::int64_t x, std::int64_t y) {
                new (self) TileKey{level, x, y};
            },
            "level"_a, "x"_a, "y"_a)
        .def_rw("level", &TileKey::level)
        .def_rw("x", &TileKey::x)
        .def_rw("y", &TileKey::y)
        .def(nb::self == nb::self)
        .def("__repr__",
             [](const TileKey& k) {
                 return "TileKey(" + std::to_string(k.level) + ", " + std::to_string(k.x) + ", " +
                        std::to_string(k.y) + ")";
             })
        .def_prop_ro("parent", &TileScheme::Parent)
        .def_prop_ro("children", [](const TileKey& key) {
            const auto c = TileScheme::Children(key);
            return std::vector<TileKey>(c.begin(), c.end());
        });
}

void BindTileScheme(nb::module_& m) {
    nb::class_<TileScheme>(
        m, "TileScheme",
        "A quadtree of north-up tiles: level 0 has tiles `root_size` across from `origin` (the "
        "top-left corner of tile (0, 0)), each level halves them, and every tile has `cells` x "
        "`cells` samples.")
        .def(
            "__init__",
            [](TileScheme* self, const Vec2d& origin, double rootSize, std::size_t cells) {
                new (self) TileScheme{origin, rootSize, cells};
                self->Validate();
            },
            "origin"_a, "root_size"_a, "cells"_a = 256)
        .def_static("covering", &TileScheme::Covering, "grid"_a, "cells"_a = 256,
                    "A scheme one of whose levels has the cells of `grid`.")
        .def_static("whole_body", &TileScheme::WholeBody, "cells"_a = 256,
                    "A longitude-latitude pyramid over a whole sphere.")
        .def_static("body_tiles", &TileScheme::BodyTiles, "level"_a,
                    "Every tile of a level of a whole-body scheme.")
        .def_static("wrap_body_tile", &TileScheme::WrapBodyTile, "key"_a,
                    "The whole-body tile a neighbour across the antimeridian is.")
        .def_ro("origin", &TileScheme::origin)
        .def_ro("root_size", &TileScheme::rootSize)
        .def_ro("cells", &TileScheme::cells)
        .def("tile_size", &TileScheme::TileSize, "level"_a)
        .def("spacing", &TileScheme::Spacing, "level"_a)
        .def("level_for", &TileScheme::LevelFor, "spacing"_a,
             "The coarsest level whose spacing is at most `spacing`.")
        .def("tile_at", &TileScheme::TileAt, "x"_a, "y"_a, "level"_a)
        .def("tiles_covering", &TileScheme::TilesCovering, "bounds"_a, "level"_a)
        .def("grid", &TileScheme::Grid, "key"_a, "The tile's grid of samples.");
}

void BindTiles(nb::module_& m) {
    BindTileKey(m);
    BindTileScheme(m);
    m.def(
        "evaluate_tile",
        [](const PyTerrain& terrain, const TileScheme& scheme, const TileKey& key,
           unsigned threads) { return EvaluateTile(terrain.Get(), scheme, key, threads); },
        "terrain"_a, "scheme"_a, "key"_a, "threads"_a = 1, nb::call_guard<nb::gil_scoped_release>(),
        "One tile of `scheme` at its level's spacing, equal to the same cells of any larger grid.");
    m.def(
        "evaluate_tiles",
        [](const PyTerrain& terrain, const TileScheme& scheme, const std::vector<TileKey>& keys,
           unsigned threads) { return EvaluateTiles(terrain.Get(), scheme, keys, threads); },
        "terrain"_a, "scheme"_a, "keys"_a, "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "`evaluate_tile` for each of `keys`, split over threads.");
    nb::class_<TileCache>(
        m, "TileCache",
        "Tiles of one terrain and scheme, evaluated on demand and kept up to about "
        "`max_bytes`, least recently used first out; thread-safe. The budget is "
        "split over 16 shards that each keep at least one tile, so a budget "
        "smaller than 16 tiles is exceeded.")
        .def(
            "__init__",
            [](TileCache* self, const PyTerrain& terrain, const TileScheme& scheme,
               std::size_t maxBytes) { new (self) TileCache(terrain.Get(), scheme, maxBytes); },
            "terrain"_a, "scheme"_a, "max_bytes"_a = std::size_t{1} << 30)
        .def(
            "get",
            [](TileCache& cache, const TileKey& key) { return Raster<float>(*cache.Get(key)); },
            "key"_a, nb::call_guard<nb::gil_scoped_release>(),
            "The tile at `key`, evaluated if it is not held.")
        .def_prop_ro("size", &TileCache::Size, "Bytes held.")
        .def_prop_ro("max_bytes", &TileCache::MaxBytes)
        .def_prop_ro("evictions", &TileCache::Evictions, "Tiles evicted so far.");
}

/// @brief Registers ConfigError, a ValueError with the JSON `path` of the offending value.
void BindConfigError(nb::module_& m) {
    // Named within the extension module, as nanobind names its own types, so its stubs list it.
    const std::string name = nb::cast<std::string>(m.attr("__name__")) + ".ConfigError";
    static nb::object configError =
        nb::steal(PyErr_NewExceptionWithDoc(name.c_str(),
                                            "An invalid configuration; `path` is the JSON path of "
                                            "the offending value, empty for the whole document.",
                                            PyExc_ValueError, nullptr));
    m.attr("ConfigError") = configError;
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

void BindConfigIo(nb::class_<TerrainConfig>& config) {
    config.def(nb::init<>(), "The default configuration.")
        .def_static("from_json", &ParseConfig, "text"_a,
                    "Parses a configuration from JSON text. Raises ConfigError if it does not fit "
                    "the format.")
        .def_static(
            "from_dict",
            [](nb::handle data) {
                const nb::object json = nb::module_::import_("json");
                return ParseConfig(nb::cast<std::string>(json.attr("dumps")(data)));
            },
            "data"_a, "Parses a configuration from a dict of JSON values.")
        .def_static("load", &LoadConfig, "path"_a,
                    "Reads the configuration in the JSON file at `path`.")
        .def("to_json", &ConfigToJson, "indent"_a = 2,
             "The configuration as JSON with every value written out.")
        .def(
            "to_dict", [](const TerrainConfig& c) { return FromJson(ConfigToJson(c, -1)); },
            "The configuration as a dict of JSON values, every value written out.");
}

void BindConfigQueries(nb::class_<TerrainConfig>& config) {
    config
        .def(
            "validate",
            [](const TerrainConfig& c, const OptionalPath& directory) {
                ValidateConfig(c, Directory(directory));
            },
            "directory"_a = nb::none(),
            "Checks everything parsing cannot (grid limits, raster files, model parameters, "
            "expected crater counts); relative input paths resolve against `directory`. Raises "
            "ConfigError naming the first invalid value.")
        .def("__eq__", [](const TerrainConfig& a, const TerrainConfig& b) { return a == b; })
        .def(
            "body",
            [](const TerrainConfig& c, const OptionalPath& directory) {
                return BuildSurface(c.body, Directory(directory));
            },
            "directory"_a = nb::none(),
            "The Surface the configuration's terrain lies on; a shape model's path resolves "
            "against `directory`.")
        .def(
            "base_grid",
            [](const TerrainConfig& c, const OptionalPath& directory) -> std::optional<RasterGrid> {
                if (!c.base) {
                    return std::nullopt;
                }
                return DescribeSource(*c.base, Directory(directory),
                                      BuildSurface(c.body, Directory(directory)));
            },
            "directory"_a = nb::none(),
            "The grid the configuration's base covers, reading only a raster's header; None "
            "without a base.")
        .def(
            "output_grids",
            [](const TerrainConfig& c, const OptionalPath& directory) {
                return ValidateConfig(c, Directory(directory)).outputGrids;
            },
            "directory"_a = nb::none(),
            "The grid each output covers, in order (None for a mesh), after validating the "
            "configuration.")
        .def("__repr__", [](const TerrainConfig& c) {
            return "Config(" + std::to_string(c.layers.size()) + " layers, " +
                   std::to_string(c.outputs.size()) + " outputs)";
        });
}

void BindConfig(nb::module_& m) {
    BindConfigError(m);
    nb::class_<TerrainConfig> config(m, "Config",
                                     "A terrain configuration: a body, optionally a base, layers, "
                                     "outputs and optionally an albedo, as the JSON configs of the "
                                     "terraingen command describe.");
    BindConfigIo(config);
    BindConfigQueries(config);
    m.def(
        "config_catalog", [] { return FromJson(ConfigCatalog(-1)); },
        "Every configuration type with its defaults, as a dict: the lists 'body', 'base', "
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

/// @brief `grid`, or else the first grid `terrain` generated.
/// @throws std::invalid_argument when there is neither.
RasterGrid GeneratedGridOr(const GeneratedTerrain& terrain, const std::optional<RasterGrid>& grid) {
    if (grid) {
        return *grid;
    }
    if (terrain.grids.empty()) {
        throw std::invalid_argument("The terrain generated no grid; give one");
    }
    return RasterGrid::Of(terrain.grids.front().heights);
}

void BindGeneratedTerrain(nb::module_& m) {
    nb::class_<GeneratedGrid>(m, "GeneratedGrid",
                              "A terrain's heights, and its albedo bands, over one grid.")
        .def_prop_ro(
            "heights", [](GeneratedGrid& g) -> Raster<float>& { return g.heights; },
            nb::rv_policy::reference_internal, "Heights in metres over the grid.")
        .def_prop_ro(
            "albedo",
            [](nb::handle self) {
                nb::list bands;
                for (Raster<float>& band : nb::cast<GeneratedGrid&>(self).albedo) {
                    bands.append(nb::cast(&band, nb::rv_policy::reference_internal, self));
                }
                return bands;
            },
            "Albedo bands, empty unless an albedo output covers the grid.")
        .def_prop_ro(
            "materials",
            [](nb::handle self) {
                nb::list bands;
                for (Raster<float>& band : nb::cast<GeneratedGrid&>(self).materials) {
                    bands.append(nb::cast(&band, nb::rv_policy::reference_internal, self));
                }
                return bands;
            },
            "The share of each MaterialClass, one raster per class; empty unless a materials or "
            "photometry output covers the grid.")
        .def_prop_ro(
            "grid", [](const GeneratedGrid& g) { return RasterGrid::Of(g.heights); },
            "The grid the heights cover.");
    nb::class_<GeneratedTerrain> terrain(m, "GeneratedTerrain",
                                         "A generated terrain: the heights and albedo over each "
                                         "grid its outputs cover, and the craters and boulders it "
                                         "places on request.");
    terrain.def_prop_ro(
        "grids",
        [](nb::handle self) {
            nb::list grids;
            for (GeneratedGrid& grid : nb::cast<GeneratedTerrain&>(self).grids) {
                grids.append(nb::cast(&grid, nb::rv_policy::reference_internal, self));
            }
            return grids;
        },
        "The GeneratedGrid of each distinct grid a heightmap, albedo or terrain mesh output "
        "covers, in output order.");
    terrain
        .def(
            "placed_craters",
            [](const GeneratedTerrain& t, std::optional<RasterGrid> grid, double spacing) {
                return PlacedCraters(t.terrain, GeneratedGridOr(t, grid), spacing);
            },
            "grid"_a = nb::none(), "spacing"_a = 0.0,
            "The craters the terrain's layers apply over `grid` (by default its first generated "
            "grid) at `spacing` metres (0 for the grid's).")
        .def(
            "placed_boulders",
            [](const GeneratedTerrain& t, std::optional<RasterGrid> grid, double spacing) {
                return PlacedBoulders(t.terrain, GeneratedGridOr(t, grid), spacing);
            },
            "grid"_a = nb::none(), "spacing"_a = 0.0,
            "The boulders the terrain's layers place over `grid` (by default its first generated "
            "grid) at `spacing` metres (0 for the grid's), and the models among them.")
        .def_ro("seconds", &GeneratedTerrain::seconds,
                "Wall-clock time the heights and albedo took to evaluate, in seconds.")
        .def_ro("backend", &GeneratedTerrain::backend,
                "Where the heights were evaluated: CPU or a GPU backend.")
        .def("summary", &Summary, "One line describing the terrain: its size and time.");
}

void BindRun(nb::module_& m) {
    m.def(
        "generate_terrain",
        [](const TerrainConfig& config, const OptionalPath& directory, unsigned threads,
           std::optional<Backend> backend) {
            return GenerateTerrain(config, Directory(directory), threads, backend);
        },
        "config"_a, "directory"_a = nb::none(), "threads"_a = 0, "backend"_a = nb::none(),
        nb::call_guard<nb::gil_scoped_release>(),
        "Generates the terrain `config` describes over each grid its outputs cover, on up to "
        "`threads` threads (0 for every hardware thread; the result does not depend on it), "
        "evaluating the heights on `backend` (None for default_backend()). "
        "Relative input "
        "paths resolve against `directory`. Raises ConfigError for an invalid configuration.");
    m.def(
        "build_terrain",
        [](const TerrainConfig& config, const OptionalPath& directory) {
            return FromTerrain(BuildTerrain(config, Directory(directory)));
        },
        "config"_a, "directory"_a = nb::none(),
        "The Terrain a configuration describes, as library objects to evaluate, tile, query at "
        "points or change; relative input paths resolve against `directory`.");
    m.def(
        "build_albedo",
        [](const TerrainConfig& config, const PyTerrain& terrain, const OptionalPath& directory) {
            return FromAlbedo(BuildAlbedo(config, terrain.Get(), Directory(directory)));
        },
        "config"_a, "terrain"_a, "directory"_a = nb::none(),
        "The Albedo a configuration describes for `terrain`, built from the same configuration "
        "by `build_terrain`.");
    m.def(
        "write_outputs",
        [](const TerrainConfig& config, const GeneratedTerrain& terrain,
           const std::filesystem::path& directory) {
            std::vector<std::string> paths;
            for (const auto& path : WriteOutputs(config, terrain, directory)) {
                paths.push_back(path.string());
            }
            return paths;
        },
        "config"_a, "terrain"_a, "directory"_a, nb::call_guard<nb::gil_scoped_release>(),
        "Writes `config`'s outputs for `terrain`, resolving relative paths against `directory`, "
        "and returns the paths written.");
}

}  // namespace

void BindPipeline(nb::module_& m) {
    BindTerrainClass(m);
    BindFeatures(m);
    BindEvaluation(m);
    BindTiles(m);
    BindConfig(m);
    BindGeneratedTerrain(m);
    BindRun(m);
}

}  // namespace terraingen::python
