#include "bindings.h"
#include "common.h"

#include "terraingen/pipeline/report.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::python {

namespace {

void BindReportParts(nb::module_& m) {
    nb::class_<CraterBin>(m, "CraterBin",
                          "The craters listed in one diameter bin [min_diameter, max_diameter) "
                          "and the count a target distribution expects there.")
        .def(nb::init<>())
        .def_rw("min_diameter", &CraterBin::minDiameter, "Smallest diameter in meters.")
        .def_rw("max_diameter", &CraterBin::maxDiameter, "Largest diameter in meters.")
        .def_rw("count", &CraterBin::count, "Craters listed in the bin.")
        .def_rw("expected", &CraterBin::expected,
                "Craters the target expects over the grid's area in the bin; NaN without a "
                "density target or outside its valid range.");
    nb::class_<LayerTiming>(m, "LayerTiming", "The time one layer took over every grid.")
        .def(
            "__init__",
            [](LayerTiming* self, std::string type, double seconds) {
                new (self) LayerTiming{std::move(type), seconds};
            },
            "type"_a = std::string{}, "seconds"_a = 0.0)
        .def_rw("type", &LayerTiming::type, "The layer's type, such as 'craters'.")
        .def_rw("seconds", &LayerTiming::seconds, "Wall-clock seconds.");
    nb::class_<GridReport>(m, "GridReport",
                           "Statistics of one evaluated grid; heights and slopes over cells with "
                           "data, NaN without any.")
        .def(nb::init<>())
        .def_rw("grid", &GridReport::grid, "The grid: its size, pixel size and extent.")
        .def_rw("height_min", &GridReport::heightMin, "Lowest height in meters.")
        .def_rw("height_max", &GridReport::heightMax, "Highest height in meters.")
        .def_rw("height_mean", &GridReport::heightMean, "Mean height in meters.")
        .def_rw("height_std", &GridReport::heightStd,
                "Population standard deviation of the heights in meters.")
        .def_rw("slope_mean_deg", &GridReport::slopeMeanDeg, "Mean slope in degrees.")
        .def_rw("slope_median_deg", &GridReport::slopeMedianDeg, "Median slope in degrees.")
        .def_rw("slope_p95_deg", &GridReport::slopeP95Deg, "95th percentile slope in degrees.")
        .def_rw("crater_bins", &GridReport::craterBins,
                "The listed craters per power-of-2 diameter bin, smallest first.")
        .def_rw("boulder_count", &GridReport::boulderCount, "Boulders listed.")
        .def_rw("boulder_model_count", &GridReport::boulderModelCount,
                "Boulders placed as their own 3D models.")
        .def_rw("boulder_cover", &GridReport::boulderCover,
                "Summed boulder footprints, pi d^2 / 4 each, over the grid's area.");
}

void BindTerrainReport(nb::module_& m) {
    nb::class_<TerrainReport>(m, "TerrainReport",
                              "A generated terrain's report: its grids' statistics, layer "
                              "timings and how it was run.")
        .def(nb::init<>())
        .def_rw("grids", &TerrainReport::grids, "A GridReport per evaluated grid.")
        .def_rw("layers", &TerrainReport::layers, "A LayerTiming per layer, in order.")
        .def_rw("backend", &TerrainReport::backend,
                "Where the heights were evaluated, such as 'cpu'.")
        .def_rw("threads", &TerrainReport::threads,
                "Threads requested; 0 for every hardware thread.")
        .def_rw("seeds", &TerrainReport::seeds, "The seeds the terrain was generated with.")
        .def_rw("seconds", &TerrainReport::seconds, "Wall-clock seconds the generation took.")
        .def_rw("version", &TerrainReport::version, "The library version.")
        .def(
            "to_dict",
            [](const TerrainReport& report) {
                return nb::module_::import_("json").attr("loads")(ReportToJson(report, -1));
            },
            "The report as `report_to_json` writes it, parsed into dicts and lists, with NaN as "
            "None.");
    m.def(
        "report_grid",
        [](const Raster<float>& heights, const Surface& surface, const CraterFeatures* craters,
           const BoulderFeatures* boulders, const SizeFrequencyDistribution* craterTarget,
           unsigned threads) {
            nb::gil_scoped_release release;
            return ReportGrid(heights, surface, craters, boulders, craterTarget, threads);
        },
        "heights"_a, "surface"_a = Surface{}, "craters"_a.none() = nb::none(),
        "boulders"_a.none() = nb::none(), "crater_target"_a.none() = nb::none(), "threads"_a = 0u,
        "Statistics of `heights` on `surface` and of the craters and boulders listed over it: "
        "heights, slopes from central differences in meters on the surface (cells without data "
        "skipped), craters per power-of-2 diameter bin with the counts `crater_target` expects, "
        "and boulder counts and cover.");
    m.def("report_to_json", &ReportToJson, "report"_a, "indent"_a = 2,
          "The report as JSON text, NaN as null; a negative indent writes one line.");
}

}  // namespace

void BindReport(nb::module_& m) {
    BindReportParts(m);
    BindTerrainReport(m);
}

}  // namespace terraingen::python
