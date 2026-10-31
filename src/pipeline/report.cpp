// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/pipeline/report.h"

#include "terraingen/analysis/raster_stats.h"
#include "terraingen/geometry/geometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

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
    const SlopeStats slopes = ComputeSlopeStats(heights, surface, threads);
    report.slopeMeanDeg = slopes.mean;
    report.slopeMedianDeg = slopes.median;
    report.slopeP95Deg = slopes.p95;
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

}  // namespace

GridReport ReportGrid(const Raster<float>& heights, const Surface& surface,
                      const CraterFeatures* craters, const BoulderFeatures* boulders,
                      const SizeFrequencyDistribution* craterTarget, unsigned threads) {
    GridReport report;
    report.grid = heights.Grid();
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

}  // namespace terraingen
