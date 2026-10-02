#include "terraingen/features/craters/apply.h"

#include "terraingen/core/log.h"
#include "terraingen/features/craters/degradation/none.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <vector>

namespace terraingen {

namespace {

using Matrix3 = std::array<std::array<double, 3>, 3>;

double Determinant(const Matrix3& m) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

struct PixelWindow {
    std::size_t colBegin = 0;
    std::size_t colEnd = 0;
    std::size_t rowBegin = 0;
    std::size_t rowEnd = 0;

    bool Empty() const {
        return colBegin >= colEnd || rowBegin >= rowEnd;
    }
};

PixelWindow Window(const Raster<float>& raster, double x, double y, double reach) {
    const GeoTransform& transform = raster.Transform();
    constexpr double kInf = std::numeric_limits<double>::infinity();
    double colMin = kInf;
    double colMax = -kInf;
    double rowMin = kInf;
    double rowMax = -kInf;
    for (const double dx : {-reach, reach}) {
        for (const double dy : {-reach, reach}) {
            const Vec2d pixel = transform.WorldToPixel(x + dx, y + dy);
            colMin = std::fmin(colMin, pixel.x);
            colMax = std::fmax(colMax, pixel.x);
            rowMin = std::fmin(rowMin, pixel.y);
            rowMax = std::fmax(rowMax, pixel.y);
        }
    }
    const auto clampTo = [](double value, std::size_t limit) {
        return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
    };
    return {clampTo(std::floor(colMin), raster.Width()), clampTo(std::ceil(colMax), raster.Width()),
            clampTo(std::floor(rowMin), raster.Height()),
            clampTo(std::ceil(rowMax), raster.Height())};
}

/// Least-squares plane h = a + b dx + c dy over the cells inside a crater's rim, in coordinates
/// relative to its centre; the mean when fewer than three cells or a degenerate layout.
class PlaneFit {
  public:
    void Add(double dx, double dy, double h) {
        n_ += 1.0;
        sx_ += dx;
        sy_ += dy;
        sxx_ += dx * dx;
        sxy_ += dx * dy;
        syy_ += dy * dy;
        sh_ += h;
        sxh_ += dx * h;
        syh_ += dy * h;
    }

    bool Empty() const {
        return n_ == 0.0;
    }

    /// Plane coefficients (a, b, c) by Cramer's rule.
    std::array<double, 3> Solve() const {
        const Matrix3 normal{{{n_, sx_, sy_}, {sx_, sxx_, sxy_}, {sy_, sxy_, syy_}}};
        const double det = Determinant(normal);
        if (n_ < 3.0 || !(std::fabs(det) > 1e-9 * n_ * sxx_ * syy_)) {
            return {sh_ / n_, 0.0, 0.0};
        }
        const std::array<double, 3> rhs{sh_, sxh_, syh_};
        std::array<double, 3> solution{};
        for (std::size_t column = 0; column < 3; ++column) {
            Matrix3 replaced = normal;
            for (std::size_t row = 0; row < 3; ++row) {
                replaced[row][column] = rhs[row];
            }
            solution[column] = Determinant(replaced) / det;
        }
        return solution;
    }

  private:
    double n_ = 0.0;
    double sx_ = 0.0;
    double sy_ = 0.0;
    double sxx_ = 0.0;
    double sxy_ = 0.0;
    double syy_ = 0.0;
    double sh_ = 0.0;
    double sxh_ = 0.0;
    double syh_ = 0.0;
};

double Inheritance(double radii) {
    const double t = std::clamp((radii - 1.0) / 0.2, 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

std::vector<std::size_t> Order(const CraterList& craters, CraterOrder order) {
    std::vector<std::size_t> indices(craters.Size());
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    const auto& age = craters.AgesGyr();
    const auto& diameter = craters.Diameters();
    const auto& id = craters.Ids();
    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        if (order == CraterOrder::OldestFirst && age[a] != age[b]) {
            return age[a] > age[b];
        }
        if (diameter[a] != diameter[b]) {
            return diameter[a] > diameter[b];
        }
        if (age[a] != age[b]) {
            return age[a] > age[b];
        }
        return id[a] < id[b];
    });
    return indices;
}

}  // namespace

void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const CraterApplyOptions& options) {
    ApplyCraters(raster, craters, profile, NoDegradation(), options);
}

void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const DegradationModel& degradation, const CraterApplyOptions& options) {
    if (!(std::isfinite(options.minDiameterPixels) && options.minDiameterPixels >= 0.0)) {
        throw std::invalid_argument("minDiameterPixels must be finite and not negative");
    }
    if (raster.Empty() || craters.Empty()) {
        return;
    }
    const Stopwatch stopwatch;
    const GeoTransform& transform = raster.Transform();
    const double pixelArea = std::fabs(transform.pixelWidth * transform.pixelHeight -
                                       transform.rowRotation * transform.colRotation);
    if (!(pixelArea > 0.0)) {
        throw std::domain_error("The raster's transform is not invertible");
    }
    const double pixelSize = std::sqrt(pixelArea);
    const std::optional<float> noData = raster.NoData();
    const auto isData = [&](float value) { return !noData || value != *noData; };

    std::size_t applied = 0;
    std::size_t unresolved = 0;
    std::size_t offRaster = 0;
    for (const std::size_t index : Order(craters, options.order)) {
        const Crater crater = craters.At(index);
        if (crater.diameter < options.minDiameterPixels * pixelSize) {
            ++unresolved;
            continue;
        }
        const double radius = 0.5 * crater.diameter;
        const PixelWindow window =
            Window(raster, crater.x, crater.y, profile.ReachRadii() * radius);
        if (window.Empty()) {
            ++offRaster;
            continue;
        }
        ++applied;

        PlaneFit fit;
        for (std::size_t row = window.rowBegin; row < window.rowEnd; ++row) {
            for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
                const Vec2d centre = raster.CellCenter(col, row);
                const double dx = centre.x - crater.x;
                const double dy = centre.y - crater.y;
                if (dx * dx + dy * dy < radius * radius && isData(raster(col, row))) {
                    fit.Add(dx, dy, raster(col, row));
                }
            }
        }
        const CraterState state = degradation.State(crater);
        const std::array<double, 3> plane =
            fit.Empty() ? std::array<double, 3>{0.0, 0.0, 0.0} : fit.Solve();

        for (std::size_t row = window.rowBegin; row < window.rowEnd; ++row) {
            for (std::size_t col = window.colBegin; col < window.colEnd; ++col) {
                float& cell = raster(col, row);
                if (!isData(cell)) {
                    continue;
                }
                const Vec2d centre = raster.CellCenter(col, row);
                const double dx = centre.x - crater.x;
                const double dy = centre.y - crater.y;
                const double distance = std::sqrt(dx * dx + dy * dy);
                if (distance >= profile.ReachRadii() * radius) {
                    continue;
                }
                const double height = cell;
                const double weight = fit.Empty() ? 0.0 : Inheritance(distance / radius);
                const double base = plane[0] + plane[1] * dx + plane[2] * dy;
                cell =
                    static_cast<float>(height + profile.Height(distance, crater.diameter, state) +
                                       weight * (base - height));
            }
        }
    }
    LogInfo("craters", "Applied ", applied, " of ", craters.Size(), " craters (", unresolved,
            " below ", options.minDiameterPixels, " px, ", offRaster, " off the raster) in ",
            FormatDuration(stopwatch.Milliseconds()));
}

}  // namespace terraingen
