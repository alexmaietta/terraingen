#include "terraingen/craters/apply.h"

#include "terraingen/craters/degradation/none.h"
#include "terraingen/util/log.h"

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

    double Mean() const {
        return sh_ / n_;
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

/// Height of the raster at world point (x, y) by bilinear interpolation of the cell centres, or
/// nothing if a contributing cell is outside the raster or holds no data.
std::optional<double> Sample(const Raster<float>& raster, double x, double y) {
    const Vec2d pixel = raster.Transform().WorldToPixel(x, y);
    const double u = pixel.x - 0.5;
    const double v = pixel.y - 0.5;
    const double col0 = std::floor(u);
    const double row0 = std::floor(v);
    const double maxCol = static_cast<double>(raster.Width()) - 1.0;
    const double maxRow = static_cast<double>(raster.Height()) - 1.0;
    if (col0 < 0.0 || row0 < 0.0 || col0 > maxCol || row0 > maxRow) {
        return std::nullopt;
    }
    const auto c0 = static_cast<std::size_t>(col0);
    const auto r0 = static_cast<std::size_t>(row0);
    const std::size_t c1 = std::min(c0 + 1, raster.Width() - 1);
    const std::size_t r1 = std::min(r0 + 1, raster.Height() - 1);
    const double fu = u - col0;
    const double fv = v - row0;
    const std::array<float, 4> cells{raster(c0, r0), raster(c1, r0), raster(c0, r1),
                                     raster(c1, r1)};
    std::array<double, 4> values{};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (raster.IsNoData(cells[i])) {
            return std::nullopt;
        }
        values[i] = cells[i];
    }
    return (1.0 - fv) * ((1.0 - fu) * values[0] + fu * values[1]) +
           fv * ((1.0 - fu) * values[2] + fu * values[3]);
}

}  // namespace

void ValidateCraterApplyOptions(const CraterApplyOptions& options) {
    if (!(std::isfinite(options.minDiameterPixels) && options.minDiameterPixels >= 0.0)) {
        throw std::invalid_argument("minDiameterPixels must be finite and not negative");
    }
    if (!(std::isfinite(options.datumFitRadii) && options.datumFitRadii > 0.0)) {
        throw std::invalid_argument("datumFitRadii must be positive and finite");
    }
    if (!(options.interiorReplacement >= 0.0 && options.interiorReplacement <= 1.0)) {
        throw std::invalid_argument("interiorReplacement must be between 0 and 1");
    }
    const CraterBlend& blend = options.blend;
    if (!(std::isfinite(blend.startRadii) && std::isfinite(blend.endRadii) &&
          blend.startRadii >= 0.0 && blend.endRadii > blend.startRadii)) {
        throw std::invalid_argument("Crater blend must satisfy 0 <= startRadii < endRadii");
    }
}

namespace {}  // namespace

double CraterBlendWeight(double radii, const CraterBlend& blend) {
    if (radii <= blend.startRadii) {
        return 1.0;
    }
    if (radii >= blend.endRadii) {
        return 0.0;
    }
    const double t = (radii - blend.startRadii) / (blend.endRadii - blend.startRadii);
    switch (blend.shape) {
    case BlendShape::Linear:
        return 1.0 - t;
    case BlendShape::Smoothstep:
        return 1.0 - t * t * (3.0 - 2.0 * t);
    case BlendShape::Smootherstep:
        return 1.0 - t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    case BlendShape::Gaussian: {
        constexpr double kSigmas = 3.0;
        const double tail = std::exp(-0.5 * kSigmas * kSigmas);
        const double z = kSigmas * t;
        return (std::exp(-0.5 * z * z) - tail) / (1.0 - tail);
    }
    }
    return 0.0;
}

namespace {

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
    ValidateCraterApplyOptions(options);
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
    const auto isData = [&](float value) { return !raster.IsNoData(value); };

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

        const CraterState state = degradation.State(crater);
        std::optional<std::array<double, 3>> datum;
        if (options.datum == CraterDatum::CentreHeight) {
            if (const auto height = Sample(raster, crater.x, crater.y)) {
                datum = std::array<double, 3>{*height, 0.0, 0.0};
            }
        } else if (options.datum != CraterDatum::None) {
            const double fitRadius = options.datumFitRadii * radius;
            const PixelWindow fitWindow = Window(raster, crater.x, crater.y, fitRadius);
            PlaneFit fit;
            for (std::size_t row = fitWindow.rowBegin; row < fitWindow.rowEnd; ++row) {
                for (std::size_t col = fitWindow.colBegin; col < fitWindow.colEnd; ++col) {
                    const Vec2d centre = raster.CellCenter(col, row);
                    const double dx = centre.x - crater.x;
                    const double dy = centre.y - crater.y;
                    if (dx * dx + dy * dy < fitRadius * fitRadius && isData(raster(col, row))) {
                        fit.Add(dx, dy, raster(col, row));
                    }
                }
            }
            if (!fit.Empty()) {
                datum = options.datum == CraterDatum::Plane
                            ? fit.Solve()
                            : std::array<double, 3>{fit.Mean(), 0.0, 0.0};
            }
        }

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
                double replaced = 0.0;
                if (datum) {
                    const double weight = options.interiorReplacement *
                                          CraterBlendWeight(distance / radius, options.blend);
                    const double base = (*datum)[0] + (*datum)[1] * dx + (*datum)[2] * dy;
                    replaced = weight * (base - height);
                }
                cell = static_cast<float>(
                    height + profile.Height(distance, crater.diameter, state) + replaced);
            }
        }
    }
    LogInfo("craters", "Applied ", applied, " of ", craters.Size(), " craters (", unresolved,
            " below ", options.minDiameterPixels, " px, ", offRaster, " off the raster) in ",
            FormatDuration(stopwatch.Milliseconds()));
}

}  // namespace terraingen
