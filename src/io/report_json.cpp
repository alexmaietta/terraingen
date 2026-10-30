// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/io/report_json.h"

#include "terraingen/geometry/geometry.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace terraingen {

namespace {

using Json = nlohmann::ordered_json;

Json GridJson(const GridReport& report) {
    const RasterGrid& grid = report.grid;
    const Bounds2d bounds = grid.Bounds();
    Json bins = Json::array();
    for (const CraterBin& bin : report.craterBins) {
        bins.push_back({{"min_diameter", bin.minDiameter},
                        {"max_diameter", bin.maxDiameter},
                        {"count", bin.count},
                        {"expected", bin.expected}});
    }
    return {{"grid",
             {{"width", grid.width},
              {"height", grid.height},
              {"pixel_size", grid.PixelSize()},
              {"transform", grid.transform.ToGdal()},
              {"extent", {bounds.minX, bounds.minY, bounds.maxX, bounds.maxY}}}},
            {"height",
             {{"min", report.heightMin},
              {"max", report.heightMax},
              {"mean", report.heightMean},
              {"std", report.heightStd}}},
            {"slope_deg",
             {{"mean", report.slopeMeanDeg},
              {"median", report.slopeMedianDeg},
              {"p95", report.slopeP95Deg}}},
            {"crater_bins", std::move(bins)},
            {"boulders",
             {{"count", report.boulderCount},
              {"models", report.boulderModelCount},
              {"cover", report.boulderCover}}}};
}

}  // namespace

std::string ReportToJson(const TerrainReport& report, int indent) {
    Json grids = Json::array();
    for (const GridReport& grid : report.grids) {
        grids.push_back(GridJson(grid));
    }
    Json layers = Json::array();
    for (const LayerTiming& layer : report.layers) {
        layers.push_back({{"type", layer.type}, {"seconds", layer.seconds}});
    }
    const Json json = {{"version", report.version}, {"backend", report.backend},
                       {"threads", report.threads}, {"seeds", report.seeds},
                       {"seconds", report.seconds}, {"layers", std::move(layers)},
                       {"grids", std::move(grids)}};
    return json.dump(indent < 0 ? -1 : indent);
}

}  // namespace terraingen
