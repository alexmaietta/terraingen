#include "bindings.h"
#include "common.h"

#include "terraingen/util/build_info.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/log.h"
#include "terraingen/util/special_functions.h"
#include "terraingen/util/surface.h"
#include "terraingen/util/version.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>

namespace terraingen::python {

namespace {

void BindVersion(nb::module_& m) {
    m.attr("__version__") = TERRAINGEN_VERSION_STRING;
    m.def(
        "version",
        [] {
            const Version v = GetVersion();
            return nb::make_tuple(v.major, v.minor, v.patch);
        },
        "Library version as (major, minor, patch).");
    m.def("api_version", &GetApiVersion, "Library API version.");
    m.def(
        "build_info",
        [] {
            const BuildInfo info = GetBuildInfo();
            nb::dict d;
            d["git_sha"] = info.gitSha;
            d["git_dirty"] = info.gitDirty;
            d["build_date"] = info.buildDate;
            return d;
        },
        "Build metadata of the compiled library: git_sha, git_dirty and build_date.");
}

/// @brief A log sink that calls `callback(level, category, message)` with the GIL held.
LogSink PythonLogSink(nb::callable callback) {
    auto function = std::make_shared<nb::callable>(std::move(callback));
    return [function](const LogRecord& record) {
        nb::gil_scoped_acquire gil;
        try {
            (*function)(LogLevelName(record.level), record.category, record.message);
        } catch (nb::python_error& error) {
            error.discard_as_unraisable("TerrainGen log sink");
        }
    };
}

void BindLog(nb::module_& m) {
    m.def(
        "set_log_level",
        [](std::string_view name) {
            const std::optional<LogLevel> level = ParseLogLevel(name);
            if (!level) {
                throw nb::value_error(
                    "Log level must be trace, debug, info, warning, error or off");
            }
            SetLogLevel(*level);
        },
        "level"_a,
        "Sets the lowest level TerrainGen logs: 'trace', 'debug', 'info', 'warning' (the "
        "default), 'error' or 'off'.");
    m.def(
        "get_log_level", [] { return std::string(LogLevelName(GetLogLevel())); },
        "The lowest level TerrainGen currently logs.");
    m.def(
        "set_log_sink",
        [](std::optional<nb::callable> callback) {
            SetLogSink(callback ? PythonLogSink(std::move(*callback)) : nullptr);
        },
        "callback"_a.none(),
        "Sends every log record to callback(level, category, message), or restores standard "
        "error when callback is None. The package installs a sink that forwards to the logging "
        "module on import. The callback runs with the GIL held, on whichever thread logged.");
}

void BindSurfacePoints(nb::class_<Surface>& surface) {
    surface
        .def("distance", &Surface::Distance, "a"_a, "b"_a,
             "The distance in metres between two points, great-circle on a sphere.")
        .def("position", &Surface::Position, "point"_a,
             "The 3D position (m) of a point; on a sphere, body-fixed with z north.")
        .def("offset", &Surface::Offset, "centre"_a, "point"_a,
             "`point` as (east, north) metres from `centre` (azimuthal equidistant on a sphere).")
        .def("locate", &Surface::Locate, "centre"_a, "offset"_a,
             "The point (east, north) metres from `centre`.")
        .def(
            "frame",
            [](const Surface& s, const Vec2d& point) {
                const LocalFrame frame = s.Frame(point);
                return std::tuple{frame.east, frame.north, frame.up};
            },
            "point"_a, "The (east, north, up) unit vectors at a point.")
        .def("point_of", &Surface::PointOf, "direction"_a,
             "The surface point of a direction from the sphere's centre.")
        .def("expand", &Surface::Expand, "region"_a, "metres"_a,
             "A box holding every point within `metres` of `region`.")
        .def("contains", &Surface::Contains, "region"_a, "point"_a,
             "Whether `region` holds `point`, across the antimeridian and at the poles on a "
             "sphere.");
}

void BindSurface(nb::module_& m) {
    nb::class_<Surface> surface(m, "Surface",
                                "What a terrain lies on: a plane, with coordinates in metres, or "
                                "a sphere, with coordinates in degrees of longitude and latitude "
                                "and lengths in metres.");
    surface.def(nb::init<>(), "A plane.")
        .def_static("plane", &Surface::Plane)
        .def_static("sphere", &Surface::Sphere, "radius"_a, "A sphere of `radius` metres.")
        .def_prop_ro("is_sphere", &Surface::IsSphere)
        .def_prop_ro("radius", &Surface::Radius)
        .def_prop_ro("unit_length", &Surface::UnitLength,
                     "Metres per coordinate unit: 1 on a plane, metres per degree along a "
                     "meridian on a sphere.")
        .def("__eq__", [](const Surface& a, const Surface& b) { return a == b; });
    BindSurfacePoints(surface);
}

void BindCellIndex(nb::module_& m) {
    nb::class_<CellIndex>(m, "CellIndex", "Cell (i, j) of a surface's cells.")
        .def(
            "__init__",
            [](CellIndex* self, std::int32_t i, std::int32_t j) { new (self) CellIndex{i, j}; },
            "i"_a, "j"_a)
        .def_rw("i", &CellIndex::i)
        .def_rw("j", &CellIndex::j)
        .def("__eq__", [](const CellIndex& a, const CellIndex& b) { return a == b; })
        .def("__repr__", [](const CellIndex& c) {
            return "CellIndex(" + std::to_string(c.i) + ", " + std::to_string(c.j) + ")";
        });
}

void BindCells(nb::module_& m) {
    BindCellIndex(m);
    nb::class_<SurfaceCells>(m, "SurfaceCells",
                             "Cells that tile a surface, which crater and boulder generation draws "
                             "from one cell at a time.")
        .def("cells_covering", &SurfaceCells::CellsCovering, "region"_a, "cell_size"_a,
             "The cells of `cell_size` metres that may hold points of `region`.")
        .def("cell_area", &SurfaceCells::CellArea, "cell"_a, "cell_size"_a,
             "The cell's area in square metres.")
        .def("sample_point", &SurfaceCells::SamplePoint, "u"_a, "v"_a, "cell"_a, "cell_size"_a,
             "The point at (u, v) in [0, 1)^2 of the cell, uniform in area.");
    nb::class_<PlanarCells, SurfaceCells>(m, "PlanarCells", "Square cells of a plane.")
        .def(nb::init<>());
    nb::class_<CubeSphereCells, SurfaceCells>(
        m, "CubeSphereCells",
        "Equiangular cube-sphere cells of a sphere of `radius` metres, of nearly equal area.")
        .def(nb::init<double>(), "radius"_a)
        .def("cell_of", &CubeSphereCells::CellOf, "longitude"_a, "latitude"_a, "cell_size"_a,
             "The cell holding a point.")
        .def("neighbours", &CubeSphereCells::Neighbours, "cell"_a, "cell_size"_a, "rings"_a = 1,
             "The cells within `rings` of `cell`, across face edges too.")
        .def("cells_per_edge", &CubeSphereCells::CellsPerEdge, "cell_size"_a)
        .def_prop_ro("radius", &CubeSphereCells::Radius)
        .def_static("direction", &CubeSphereCells::Direction, "face"_a, "alpha"_a, "beta"_a,
                    "The unit direction of equiangular coordinates (alpha, beta) on cube face "
                    "`face`.");
}

}  // namespace

void BindUtil(nb::module_& m) {
    BindVersion(m);
    BindLog(m);
    BindSurface(m);
    BindCells(m);
    m.def("exponential_integral", &ExponentialIntegral, "n"_a, "x"_a,
          "The generalized exponential integral E_n(x), as Golombek and Rapp's model uses.");
}

}  // namespace terraingen::python
