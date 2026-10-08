#include "bindings.h"
#include "tables.h"

#include "terraingen/pipeline/config_json.h"
#include "terraingen/pipeline/run.h"

#include <nanobind/ndarray.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace terraingen::python {

namespace {

std::filesystem::path Directory(const std::optional<std::filesystem::path>& directory) {
    return directory.value_or(std::filesystem::path{});
}

}  // namespace

void BindPipeline(nb::module_& m) {
    static nb::object configError =
        nb::steal(PyErr_NewExceptionWithDoc("terraingen.ConfigError",
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

    nb::class_<TerrainConfig>(m, "Config",
                              "A terrain configuration: a surface, a source, layers, filters, "
                              "outputs and optionally an albedo, as the JSON configs of the "
                              "terraingen command describe.")
        .def(nb::init<>(), "The default configuration.")
        .def_static(
            "from_json", [](const std::string& text) { return ParseConfig(text); }, "text"_a,
            "Parses a configuration from JSON text. Raises ConfigError if it does not fit the "
            "format.")
        .def_static(
            "from_dict",
            [](nb::handle data) {
                const nb::object json = nb::module_::import_("json");
                return ParseConfig(nb::cast<std::string>(json.attr("dumps")(data)));
            },
            "data"_a, "Parses a configuration from a dict of JSON values.")
        .def_static("load", &LoadConfig, "path"_a,
                    "Reads the configuration in the JSON file at `path`.")
        .def(
            "to_json",
            [](const TerrainConfig& config, int indent) { return ConfigToJson(config, indent); },
            "indent"_a = 2, "The configuration as JSON with every value written out.")
        .def(
            "to_dict",
            [](const TerrainConfig& config) {
                const nb::object json = nb::module_::import_("json");
                return json.attr("loads")(ConfigToJson(config, -1));
            },
            "The configuration as a dict of JSON values, every value written out.")
        .def(
            "validate",
            [](const TerrainConfig& config, const std::optional<std::filesystem::path>& directory) {
                ValidateConfig(config, Directory(directory));
            },
            "directory"_a = nb::none(),
            "Checks everything parsing cannot (grid limits, raster files, model parameters, "
            "expected crater counts); relative input paths resolve against `directory`. Raises "
            "ConfigError naming the first invalid value.")
        .def("__eq__", [](const TerrainConfig& a, const TerrainConfig& b) { return a == b; })
        .def("__repr__", [](const TerrainConfig& config) {
            return "Config(" + std::to_string(config.layers.size()) + " layers, " +
                   std::to_string(config.filters.size()) + " filters, " +
                   std::to_string(config.outputs.size()) + " outputs)";
        });

    m.def(
        "config_catalog",
        [] {
            const nb::object json = nb::module_::import_("json");
            return json.attr("loads")(ConfigCatalog(-1));
        },
        "Every configuration type with its defaults, as a dict: the lists 'surface', 'source', "
        "'layers', 'filters' and 'outputs', the 'albedo', and the 'models' (densities, ages, "
        "degradation, profiles, rock sizes, region shapes and albedo sources and layers).");

    nb::class_<GeneratedTerrain>(m, "GeneratedTerrain",
                                 "A generated terrain: heights, albedo bands and the craters and "
                                 "rocks its layers placed.")
        .def_prop_ro(
            "heights", [](GeneratedTerrain& terrain) -> Raster<float>& { return terrain.heights; },
            nb::rv_policy::reference_internal, "Heights in metres over the source's grid.")
        .def_prop_ro(
            "albedo",
            [](nb::handle self) {
                GeneratedTerrain& terrain = nb::cast<GeneratedTerrain&>(self);
                nb::list bands;
                for (Raster<float>& band : terrain.albedo) {
                    bands.append(nb::cast(&band, nb::rv_policy::reference_internal, self));
                }
                return bands;
            },
            "Albedo bands, empty without an albedo in the configuration.")
        .def_prop_ro(
            "craters",
            [](const GeneratedTerrain& terrain) {
                return CraterTable(terrain.features.craters, terrain.features.craterAttributes);
            },
            "The craters applied, as NumPy columns 'x', 'y', 'diameter', 'age_gyr', 'id' and "
            "'manual' (1 for manual craters), in the surface's coordinates, and their attributes "
            "'floor_depth' and 'rim_height' (m), 'depth_ratio', 'freshness' and 'complex'.")
        .def_prop_ro(
            "rocks",
            [](const GeneratedTerrain& terrain) { return RockTable(terrain.features.rocks); },
            "The rocks placed, as NumPy columns 'x', 'y', 'ground' (NaN when unknown), "
            "'diameter', 'height', 'burial', 'yaw', 'lean', 'id' and 'model' (1 for rocks placed "
            "as 3D models).")
        .def_prop_ro(
            "boulder_count",
            [](const GeneratedTerrain& terrain) { return terrain.features.boulders.size(); },
            "How many rocks were placed as 3D boulder models.");

    m.def(
        "generate_terrain",
        [](const TerrainConfig& config, const std::optional<std::filesystem::path>& directory,
           unsigned threads) { return GenerateTerrain(config, Directory(directory), threads); },
        "config"_a, "directory"_a = nb::none(), "threads"_a = 0,
        nb::call_guard<nb::gil_scoped_release>(),
        "Generates the terrain `config` describes over its source's grid on up to `threads` "
        "threads (0 for every hardware thread; the result does not depend on it). Relative input "
        "paths resolve against `directory`. Raises ConfigError for an invalid configuration.");

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

}  // namespace terraingen::python
