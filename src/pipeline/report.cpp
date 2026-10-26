// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/report.h"

#include "terraingen/raster/raster_stats.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/geometry.h"
#include "terraingen/util/parallel.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

using Json = nlohmann::ordered_json;

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

/// The interpolated `q` quantile of `values`, reordering them; NaN when empty.
/// @param q The quantile, from 0 to 1.
double Quantile(std::vector<double>& values, double q) {
    if (values.empty()) {
        return kNan;
    }
    const double position = q * static_cast<double>(values.size() - 1);
    const auto rank = static_cast<std::size_t>(std::floor(position));
    const auto at = values.begin() + static_cast<std::ptrdiff_t>(rank);
    std::nth_element(values.begin(), at, values.end());
    const double low = *at;
    if (rank + 1 >= values.size()) {
        return low;
    }
    const double high = *std::min_element(at + 1, values.end());
    return low + (position - static_cast<double>(rank)) * (high - low);
}

/// The neighbors along one grid axis a difference at `index` spans: both sides when they have
/// data, else the side that has and `index` itself; nothing when neither has.
/// @param valid Whether the cell `step` cells from `index` has data, for a step of -1 or 1.
template <typename Valid>
bool DifferenceSpan(std::size_t index, std::size_t size, const Valid& valid, std::size_t& first,
                    std::size_t& second) {
    const bool before = index > 0 && valid(index - 1);
    const bool after = index + 1 < size && valid(index + 1);
    first = before ? index - 1 : index;
    second = after ? index + 1 : index;
    return before || after;
}

/// The slope in degrees of the heights at cell (`col`, `row`), or NaN where it cannot be taken.
double CellSlope(const Raster<float>& heights, const Surface& surface, std::size_t col,
                 std::size_t row) {
    const auto valid = [&heights](std::size_t c, std::size_t r) {
        return !heights.IsMissing(heights(c, r));
    };
    if (!valid(col, row)) {
        return kNan;
    }
    std::size_t west = 0;
    std::size_t east = 0;
    std::size_t north = 0;
    std::size_t south = 0;
    const bool across = DifferenceSpan(
        col, heights.Width(), [&](std::size_t c) { return valid(c, row); }, west, east);
    const bool down = DifferenceSpan(
        row, heights.Height(), [&](std::size_t r) { return valid(col, r); }, north, south);
    if (!across || !down) {
        return kNan;
    }
    const Vec2d center = heights.CellCenter(col, row);
    const Vec2d a = surface.Offset(center, heights.CellCenter(east, row)) -
                    surface.Offset(center, heights.CellCenter(west, row));
    const Vec2d b = surface.Offset(center, heights.CellCenter(col, south)) -
                    surface.Offset(center, heights.CellCenter(col, north));
    const double riseA =
        static_cast<double>(heights(east, row)) - static_cast<double>(heights(west, row));
    const double riseB =
        static_cast<double>(heights(col, south)) - static_cast<double>(heights(col, north));
    const double det = a.x * b.y - a.y * b.x;
    if (det == 0.0 || !std::isfinite(det)) {
        return kNan;
    }
    const double dx = (riseA * b.y - riseB * a.y) / det;
    const double dy = (a.x * riseB - b.x * riseA) / det;
    return GradientSlopeDegrees(dx, dy);
}

void AddHeightStatistics(const Raster<float>& heights, GridReport& report) {
    const RasterStats stats = ComputeStats(heights);
    if (stats.validCount == 0) {
        return;
    }
    report.heightMin = stats.min;
    report.heightMax = stats.max;
    report.heightMean = stats.mean;
    report.heightStd = stats.stddev;
}

void AddSlopeStatistics(const Raster<float>& heights, const Surface& surface, unsigned threads,
                        GridReport& report) {
    const std::size_t width = heights.Width();
    std::vector<double> cells(heights.Size(), kNan);
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < width; ++col) {
            cells[row * width + col] = CellSlope(heights, surface, col, row);
        }
    });
    std::vector<double> slopes;
    double sum = 0.0;
    for (const double slope : cells) {
        if (!std::isnan(slope)) {
            slopes.push_back(slope);
            sum += slope;
        }
    }
    if (slopes.empty()) {
        return;
    }
    report.slopeMeanDeg = sum / static_cast<double>(slopes.size());
    report.slopeMedianDeg = Quantile(slopes, 0.5);
    report.slopeP95Deg = Quantile(slopes, 0.95);
}

/// The power-of-2 bin a positive, finite diameter falls in: floor(log2(`diameter`)).
int DiameterBin(double diameter) {
    int exponent = 0;
    std::frexp(diameter, &exponent);
    return exponent - 1;
}

/// The count `target` expects over `area` square meters in [`low`, `high`) meters, or NaN.
double ExpectedCount(const SizeFrequencyDistribution* target, double area, double low,
                     double high) {
    if (target == nullptr || target->Kind() != SfdKind::Density || !(area >= 0.0) ||
        !std::isfinite(area)) {
        return kNan;
    }
    try {
        return target->ExpectedCount(area, low, high);
    } catch (const std::domain_error&) {
        return kNan;
    }
}

void AddCraterBins(const CraterFeatures& craters, const SizeFrequencyDistribution* target,
                   double area, GridReport& report) {
    std::vector<int> bins;
    for (const double diameter : craters.craters.Diameters()) {
        if (diameter > 0.0 && std::isfinite(diameter)) {
            bins.push_back(DiameterBin(diameter));
        }
    }
    if (bins.empty()) {
        return;
    }
    const auto [smallest, largest] = std::minmax_element(bins.begin(), bins.end());
    const int first = *smallest;
    for (int bin = first; bin <= *largest; ++bin) {
        CraterBin entry;
        entry.minDiameter = std::ldexp(1.0, bin);
        entry.maxDiameter = std::ldexp(1.0, bin + 1);
        entry.expected = ExpectedCount(target, area, entry.minDiameter, entry.maxDiameter);
        report.craterBins.push_back(entry);
    }
    for (const int bin : bins) {
        ++report.craterBins[static_cast<std::size_t>(bin - first)].count;
    }
}

void AddBoulders(const BoulderFeatures& boulders, double area, GridReport& report) {
    report.boulderCount = boulders.boulders.Size();
    report.boulderModelCount = boulders.models.size();
    double footprints = 0.0;
    for (const double diameter : boulders.boulders.Diameters()) {
        footprints += 0.25 * kPi * diameter * diameter;
    }
    report.boulderCover = area > 0.0 ? footprints / area : kNan;
}

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

GridReport ReportGrid(const Raster<float>& heights, const Surface& surface,
                      const CraterFeatures* craters, const BoulderFeatures* boulders,
                      const SizeFrequencyDistribution* craterTarget, unsigned threads) {
    GridReport report;
    report.grid = RasterGrid::FromRaster(heights);
    const double area = report.grid.SurfaceArea(surface);
    AddHeightStatistics(heights, report);
    AddSlopeStatistics(heights, surface, threads, report);
    if (craters != nullptr) {
        AddCraterBins(*craters, craterTarget, area, report);
    }
    if (boulders != nullptr) {
        AddBoulders(*boulders, area, report);
    }
    return report;
}

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
