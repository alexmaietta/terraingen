#include "terraingen/layers/craters/apply.h"

#include "terraingen/layers/craters/degradation/none.h"
#include "terraingen/noise/kernels/hash.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

using Matrix3 = std::array<std::array<double, 3>, 3>;

/// Points per side of the lattice a crater's datum is fitted over.
constexpr int kDatumSamples = 7;

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

void ValidateResolutionFade(const ResolutionFade& fade) {
    if (!(std::isfinite(fade.startSamples) && std::isfinite(fade.endSamples) &&
          fade.startSamples >= 0.0 && fade.endSamples > fade.startSamples)) {
        throw std::invalid_argument("Resolution fade must satisfy 0 <= startSamples < endSamples");
    }
}

double ResolutionWeight(double diameter, double spacing, const ResolutionFade& fade) {
    const double samples = diameter / spacing;
    return 1.0 - CraterBlendWeight(samples, {fade.startSamples, fade.endSamples, fade.shape});
}

void ValidateCraterApplyOptions(const CraterApplyOptions& options) {
    ValidateResolutionFade(options.fade);
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

std::optional<std::array<double, 3>> FitCraterDatum(const Crater& crater,
                                                    const CraterApplyOptions& options,
                                                    const HeightSampler& ground) {
    if (options.datum == CraterDatum::None) {
        return std::nullopt;
    }
    if (options.datum == CraterDatum::CentreHeight) {
        const float height = ground(crater.x, crater.y);
        if (std::isnan(height)) {
            return std::nullopt;
        }
        return std::array<double, 3>{static_cast<double>(height), 0.0, 0.0};
    }
    const double fitRadius = options.datumFitRadii * 0.5 * crater.diameter;
    PlaneFit fit;
    for (int j = 0; j < kDatumSamples; ++j) {
        const double dy = fitRadius * ((2.0 * j + 1.0) / kDatumSamples - 1.0);
        for (int i = 0; i < kDatumSamples; ++i) {
            const double dx = fitRadius * ((2.0 * i + 1.0) / kDatumSamples - 1.0);
            if (dx * dx + dy * dy >= fitRadius * fitRadius) {
                continue;
            }
            const float height = ground(crater.x + dx, crater.y + dy);
            if (!std::isnan(height)) {
                fit.Add(dx, dy, height);
            }
        }
    }
    if (fit.Empty()) {
        return std::nullopt;
    }
    return options.datum == CraterDatum::Plane ? fit.Solve()
                                               : std::array<double, 3>{fit.Mean(), 0.0, 0.0};
}

std::size_t CraterInfluence::KeyHash::operator()(const Key& key) const {
    const UVec3 hash =
        Pcg3dHash({static_cast<std::uint32_t>(key.band),
                   static_cast<std::uint32_t>(key.i) ^ static_cast<std::uint32_t>(key.i >> 32),
                   static_cast<std::uint32_t>(key.j) ^ static_cast<std::uint32_t>(key.j >> 32)});
    return (static_cast<std::size_t>(hash.x) << 32) ^ hash.y;
}

namespace {

/// Side of the buckets of a band: twice the largest reach of the band's craters.
double BucketSize(int band, const CraterProfile& profile) {
    return profile.ReachRadii() * std::ldexp(1.0, band + 1);
}

}  // namespace

CraterInfluence::CraterInfluence(std::vector<Entry> entries, const CraterProfile& profile)
    : entries_(std::move(entries)), profile_(profile) {
    for (std::size_t n = 0; n < entries_.size(); ++n) {
        const Crater& crater = entries_[n].crater;
        const int band = std::ilogb(crater.diameter);
        const double size = BucketSize(band, profile_);
        buckets_[{band, static_cast<std::int64_t>(std::floor(crater.x / size)),
                  static_cast<std::int64_t>(std::floor(crater.y / size))}]
            .push_back(n);
        if (std::find(bands_.begin(), bands_.end(), band) == bands_.end()) {
            bands_.push_back(band);
        }
    }
    std::sort(bands_.begin(), bands_.end());
}

double CraterInfluence::ReliefBefore(double x, double y, std::size_t k) const {
    double relief = 0.0;
    for (const int band : bands_) {
        const double size = BucketSize(band, profile_);
        const auto i0 = static_cast<std::int64_t>(std::floor(x / size));
        const auto j0 = static_cast<std::int64_t>(std::floor(y / size));
        for (std::int64_t j = j0 - 1; j <= j0 + 1; ++j) {
            for (std::int64_t i = i0 - 1; i <= i0 + 1; ++i) {
                const auto found = buckets_.find({band, i, j});
                if (found == buckets_.end()) {
                    continue;
                }
                for (const std::size_t n : found->second) {
                    if (n >= k) {
                        break;
                    }
                    const Entry& entry = entries_[n];
                    const double dx = x - entry.crater.x;
                    const double dy = y - entry.crater.y;
                    const double distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < profile_.ReachRadii() * 0.5 * entry.crater.diameter) {
                        relief += entry.resolution *
                                  profile_.Height(distance, entry.crater.diameter, entry.state);
                    }
                }
            }
        }
    }
    return relief;
}

HeightSampler DatumGround(const HeightSampler& ground, const CraterInfluence& influence,
                          std::size_t k) {
    return [&ground, &influence, k](double x, double y) {
        const float height = ground(x, y);
        if (std::isnan(height)) {
            return height;
        }
        return static_cast<float>(static_cast<double>(height) + influence.ReliefBefore(x, y, k));
    };
}

double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter) {
    if (options.datum == CraterDatum::None) {
        return 0.0;
    }
    return (options.datumFitRadii + profile.ReachRadii()) * 0.5 * maxDiameter;
}

float ApplyCraterTo(float height, double x, double y, const Crater& crater,
                    const CraterState& state, double resolution,
                    const std::optional<std::array<double, 3>>& datum, const CraterProfile& profile,
                    const CraterApplyOptions& options) {
    const double radius = 0.5 * crater.diameter;
    const double dx = x - crater.x;
    const double dy = y - crater.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance >= profile.ReachRadii() * radius) {
        return height;
    }
    const double current = height;
    double replaced = 0.0;
    if (datum) {
        const double weight =
            options.interiorReplacement * CraterBlendWeight(distance / radius, options.blend);
        const double base = (*datum)[0] + (*datum)[1] * dx + (*datum)[2] * dy;
        replaced = weight * (base - current);
    }
    return static_cast<float>(current +
                              resolution * profile.Height(distance, crater.diameter, state) +
                              resolution * replaced);
}

std::vector<std::size_t> CraterApplyOrder(const CraterList& craters, CraterOrder order) {
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

void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const CraterApplyOptions& options, double spacing, unsigned threads,
                  const HeightSampler& ground) {
    ApplyCraters(raster, craters, profile, NoDegradation(), options, spacing, threads, ground);
}

void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const DegradationModel& degradation, const CraterApplyOptions& options,
                  double spacing, unsigned threads, const HeightSampler& ground) {
    ValidateCraterApplyOptions(options);
    if (!(std::isfinite(spacing) && spacing >= 0.0)) {
        throw std::invalid_argument("Sample spacing must be finite and not negative");
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
    const double resolved = std::max(spacing, std::sqrt(pixelArea));
    const auto isData = [&](float value) { return !raster.IsNoData(value); };

    struct Entry {
        Crater crater;
        double resolution = 0.0;
        CraterState state;
        PixelWindow window;
        std::size_t order = 0;
        std::optional<std::array<double, 3>> datum;
    };
    std::vector<Entry> entries;
    std::vector<CraterInfluence::Entry> resolvedCraters;
    std::size_t applied = 0;
    std::size_t unresolved = 0;
    std::size_t offRaster = 0;
    for (const std::size_t index : CraterApplyOrder(craters, options.order)) {
        const Crater crater = craters.At(index);
        const double resolution = ResolutionWeight(crater.diameter, resolved, options.fade);
        if (resolution == 0.0) {
            ++unresolved;
            continue;
        }
        const double radius = 0.5 * crater.diameter;
        const CraterState state = degradation.State(crater);
        resolvedCraters.push_back({crater, resolution, state});
        const PixelWindow window =
            Window(raster, crater.x, crater.y, profile.ReachRadii() * radius);
        if (window.Empty()) {
            ++offRaster;
            continue;
        }
        ++applied;
        entries.push_back(
            {crater, resolution, state, window, resolvedCraters.size() - 1, std::nullopt});
    }

    if (options.datum != CraterDatum::None && !entries.empty()) {
        std::optional<Raster<float>> input;
        HeightSampler sampler = ground;
        if (!sampler) {
            input.emplace(raster);
            sampler = [&input](double x, double y) {
                return static_cast<float>(
                    Sample(*input, x, y).value_or(std::numeric_limits<double>::quiet_NaN()));
            };
        }
        const CraterInfluence influence(std::move(resolvedCraters), profile);
        ParallelFor(entries.size(), threads, [&](std::size_t n) {
            entries[n].datum = FitCraterDatum(entries[n].crater, options,
                                              DatumGround(sampler, influence, entries[n].order));
        });
    }

    constexpr std::size_t kBandRows = 32;
    const std::size_t bands = (raster.Height() + kBandRows - 1) / kBandRows;
    ParallelFor(bands, threads, [&](std::size_t band) {
        const std::size_t bandBegin = band * kBandRows;
        const std::size_t bandEnd = std::min(raster.Height(), bandBegin + kBandRows);
        for (const Entry& entry : entries) {
            const Crater& crater = entry.crater;
            const std::size_t rowBegin = std::max(bandBegin, entry.window.rowBegin);
            const std::size_t rowEnd = std::min(bandEnd, entry.window.rowEnd);
            for (std::size_t row = rowBegin; row < rowEnd; ++row) {
                for (std::size_t col = entry.window.colBegin; col < entry.window.colEnd; ++col) {
                    float& cell = raster(col, row);
                    if (!isData(cell)) {
                        continue;
                    }
                    const Vec2d centre = raster.CellCenter(col, row);
                    cell = ApplyCraterTo(cell, centre.x, centre.y, crater, entry.state,
                                         entry.resolution, entry.datum, profile, options);
                }
            }
        }
    });
    LogInfo("craters", "Applied ", applied, " of ", craters.Size(), " craters (", unresolved,
            " unresolved at ", resolved, " m, ", offRaster, " off the raster) in ",
            FormatDuration(stopwatch.Milliseconds()));
}

}  // namespace terraingen
