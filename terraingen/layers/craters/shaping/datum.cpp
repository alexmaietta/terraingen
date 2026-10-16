#include "terraingen/layers/craters/shaping/datum.h"

#include "terraingen/noise/hash.h"

#include <algorithm>
#include <array>
#include <cmath>

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

/// Side of the buckets of a band: twice the largest reach of the band's craters.
double BucketSize(int band, const CraterProfile& profile, double outlineScale) {
    return outlineScale * profile.ReachRadii() * std::ldexp(1.0, band + 1);
}

/// The ground under the lattice points inside a crater's fit footprint, fitted by least squares.
PlaneFit FitFootprint(const Crater& crater, double fitRadii, const Surface& surface,
                      const HeightSampler& ground) {
    const double fitRadius = fitRadii * 0.5 * crater.diameter;
    PlaneFit fit;
    for (int j = 0; j < kDatumSamples; ++j) {
        const double dy = fitRadius * ((2.0 * j + 1.0) / kDatumSamples - 1.0);
        for (int i = 0; i < kDatumSamples; ++i) {
            const double dx = fitRadius * ((2.0 * i + 1.0) / kDatumSamples - 1.0);
            if (dx * dx + dy * dy >= fitRadius * fitRadius) {
                continue;
            }
            const Vec2d at = surface.Locate({crater.x, crater.y}, {dx, dy});
            const float height = ground(at.x, at.y);
            if (!std::isnan(height)) {
                fit.Add(dx, dy, height);
            }
        }
    }
    return fit;
}

}  // namespace

void PlaneFit::Add(double dx, double dy, double h) {
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

Plane PlaneFit::Solve() const {
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
    return {solution[0], solution[1], solution[2]};
}

std::optional<Plane> FitCraterDatum(const Crater& crater, const CraterApplyOptions& options,
                                    const HeightSampler& ground) {
    if (options.datum == CraterDatum::None) {
        return std::nullopt;
    }
    if (options.datum == CraterDatum::CentreHeight) {
        const float height = ground(crater.x, crater.y);
        if (std::isnan(height)) {
            return std::nullopt;
        }
        return Plane{static_cast<double>(height), 0.0, 0.0};
    }
    const PlaneFit fit = FitFootprint(crater, options.datumFitRadii, options.surface, ground);
    if (fit.Empty()) {
        return std::nullopt;
    }
    return options.datum == CraterDatum::Plane ? fit.Solve() : Plane{fit.Mean(), 0.0, 0.0};
}

std::size_t CraterReliefIndex::KeyHash::operator()(const Key& key) const {
    const UVec3 hash =
        Pcg3dHash({static_cast<std::uint32_t>(key.band) ^ static_cast<std::uint32_t>(key.k),
                   static_cast<std::uint32_t>(key.i) ^ static_cast<std::uint32_t>(key.i >> 32),
                   static_cast<std::uint32_t>(key.j) ^ static_cast<std::uint32_t>(key.j >> 32)});
    return (static_cast<std::size_t>(hash.x) << 32) ^ hash.y;
}

CraterReliefIndex::CraterReliefIndex(const std::vector<PreparedCrater>& craters,
                                     const CraterProfile& profile, double outlineScale,
                                     const Surface& surface)
    : craters_(&craters), profile_(profile), outlineScale_(outlineScale), surface_(surface) {
    for (const PreparedCrater& prepared : craters) {
        outlineScale_ = std::fmax(outlineScale_, prepared.outline.MaxScale());
    }
    for (std::size_t n = 0; n < craters.size(); ++n) {
        const Crater& crater = craters[n].crater;
        const int band = std::ilogb(crater.diameter);
        const double size = BucketSize(band, profile_, outlineScale_);
        const BucketIndex bucket = surface_.Bucket({crater.x, crater.y}, size);
        buckets_[{band, bucket.i, bucket.j, bucket.k}].push_back(n);
        if (std::find(bands_.begin(), bands_.end(), band) == bands_.end()) {
            bands_.push_back(band);
        }
    }
    std::sort(bands_.begin(), bands_.end());
}

void CraterReliefIndex::AddBucket(const std::vector<std::size_t>& bucket, double x, double y,
                                  std::size_t k, double& relief) const {
    for (const std::size_t n : bucket) {
        if (n >= k) {
            break;
        }
        const PreparedCrater& prepared = (*craters_)[n];
        const Crater& crater = prepared.crater;
        const double distance = CraterProfileDistance(crater, prepared.outline, x, y, surface_);
        if (distance < profile_.ReachRadii() * 0.5 * crater.diameter) {
            relief +=
                prepared.resolution * profile_.Height(distance, crater.diameter, prepared.state);
        }
    }
}

double CraterReliefIndex::ReliefBefore(double x, double y, std::size_t k) const {
    double relief = 0.0;
    const long long depth = surface_.IsSphere() ? 1 : 0;
    for (const int band : bands_) {
        const double size = BucketSize(band, profile_, outlineScale_);
        const BucketIndex centre = surface_.Bucket({x, y}, size);
        for (std::int64_t j = centre.j - 1; j <= centre.j + 1; ++j) {
            for (std::int64_t i = centre.i - 1; i <= centre.i + 1; ++i) {
                for (std::int64_t z = centre.k - depth; z <= centre.k + depth; ++z) {
                    const auto found = buckets_.find({band, i, j, z});
                    if (found != buckets_.end()) {
                        AddBucket(found->second, x, y, k, relief);
                    }
                }
            }
        }
    }
    return relief;
}

HeightSampler DatumGround(const HeightSampler& ground, const CraterReliefIndex& index,
                          std::size_t k) {
    return [&ground, &index, k](double x, double y) {
        const float height = ground(x, y);
        if (std::isnan(height)) {
            return height;
        }
        return static_cast<float>(static_cast<double>(height) + index.ReliefBefore(x, y, k));
    };
}

double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter) {
    if (options.datum == CraterDatum::None) {
        return 0.0;
    }
    return (options.datumFitRadii + MaxOutlineScale(options.irregularity) * profile.ReachRadii()) *
           0.5 * maxDiameter;
}

}  // namespace terraingen
