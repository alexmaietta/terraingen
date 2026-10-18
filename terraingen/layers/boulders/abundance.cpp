#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/noise/worley.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace terraingen {

namespace {

constexpr std::uint32_t kBoulderPatches = 0x50415443u;
constexpr std::uint32_t kBoulderClusters = 0x434C5553u;
constexpr std::uint32_t kThomasCentres2 = 0x54484D32u;
constexpr std::uint32_t kThomasCentres3 = 0x54484D33u;
constexpr std::uint32_t kBedrockMeans = 0x4245444Du;

/// Golombek-Rapp boulders per m^2 per metre of diameter at `diameter` for coverage `k`.
double GolombekRappDensity(double diameter, double k, double qa, double qb) {
    const double q = qa + qb / k;
    return k * q * std::exp(-q * diameter) / (std::numbers::pi * diameter * diameter / 4.0);
}

/// How far, in radii from its centre, a crater's blocks reach.
double EjectaReach(const BoulderEjecta& ejecta, double diameter) {
    if (!(ejecta.gravity && ejecta.regolithThickness)) {
        return ejecta.reach;
    }
    const double radius = 0.5 * diameter;
    const double thrown =
        BoulderThrowDistance(diameter, *ejecta.regolithThickness, *ejecta.gravity) / radius;
    return std::fmin(ejecta.reach, std::fmax(1.0, thrown));
}

/// A crater's share of blocks at `rho` radii from its centre, within its `reach`.
double RadialShare(const BoulderEjecta& ejecta, double rho, double reach) {
    if (ejecta.profile == EjectaProfile::Smoothstep) {
        return rho <= 1.0 ? 1.0 : Smoothstep((reach - rho) / (reach - 1.0));
    }
    return rho <= 1.0 ? ejecta.floorShare : std::exp(-(rho - 1.0) / ejecta.decayRadii);
}

/// The share of its blocks `crater` keeps at its age.
double Retention(const BoulderEjecta& ejecta, const Crater& crater,
                 const DegradationModel& degradation) {
    if (ejecta.aging == EjectaAging::Freshness) {
        const CraterState state = degradation.State(crater);
        const double freshness = state.fresh.depthRatio > 0.0 ? state.Freshness() : 0.0;
        return Smoothstep((freshness - ejecta.minFreshness) / (1.0 - ejecta.minFreshness));
    }
    return std::pow(std::fmax(0.0, 1.0 - crater.ageGyr / ejecta.lifetimeGyr), ejecta.agingExponent);
}

}  // namespace

CraterList BoulderLayer::EjectaCraters(const Bounds2d& region) const {
    if (minCraterDiameter_ <= 0.0) {
        return {};
    }
    const double margin = options_.ejecta.reach * 0.5 * craters_->MaxFeatureDiameter();
    const double spacing = minCraterDiameter_ / craters_->Options().apply.fade.startSamples;
    const CraterList all = craters_->Craters(surface_.Expand(region, margin), spacing);
    return FilterCraters(all, [&](std::size_t, const Crater& crater) {
        return crater.diameter >= minCraterDiameter_;
    });
}

double BoulderLayer::NearestClusterDistance(double x, double y) const {
    const double spacing = options_.clusters.spacing;
    const std::uint32_t seed = options_.seed ^ kBoulderClusters;
    if (surface_.IsBody()) {
        const Vec3d p = surface_.Position({x, y});
        const double s = 1.0 / spacing;
        return spacing * static_cast<double>(
                             Worley3(SplitLatticePrecise3(s * p.x, s * p.y, s * p.z), seed).f1);
    }
    return spacing *
           static_cast<double>(
               Worley2(static_cast<float>(x / spacing), static_cast<float>(y / spacing), seed).f1);
}

double BoulderLayer::ThomasClusterField(double x, double y) const {
    const BoulderClusters& clusters = options_.clusters;
    const double sigma = clusters.radius;
    const double side = kThomasReachRadii * sigma;
    const double perArea = 1.0 / (clusters.spacing * clusters.spacing);
    const std::uint32_t seed = options_.seed ^ kBoulderClusters;
    const auto word = [](double cell) {
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(cell));
    };
    const auto bump = [&](double squared) {
        return squared < side * side ? std::exp(-0.5 * squared / (sigma * sigma)) : 0.0;
    };
    double sum = 0.0;
    if (surface_.IsBody()) {
        const Vec3d p = surface_.Position({x, y});
        const double perCell =
            perArea / (std::sqrt(2.0 * std::numbers::pi) * sigma) * side * side * side;
        const double cx = std::floor(p.x / side);
        const double cy = std::floor(p.y / side);
        const double cz = std::floor(p.z / side);
        for (double k = cz - 1.0; k <= cz + 1.0; k += 1.0) {
            for (double j = cy - 1.0; j <= cy + 1.0; j += 1.0) {
                for (double i = cx - 1.0; i <= cx + 1.0; i += 1.0) {
                    RandomStream random(seed, kThomasCentres3, word(i), word(j), word(k));
                    const std::uint64_t count = SamplePoisson(random, perCell);
                    for (std::uint64_t n = 0; n < count; ++n) {
                        const double dx = (i + random.NextDouble()) * side - p.x;
                        const double dy = (j + random.NextDouble()) * side - p.y;
                        const double dz = (k + random.NextDouble()) * side - p.z;
                        sum += bump(dx * dx + dy * dy + dz * dz);
                    }
                }
            }
        }
        return sum;
    }
    const double perCell = perArea * side * side;
    const double cx = std::floor(x / side);
    const double cy = std::floor(y / side);
    for (double j = cy - 1.0; j <= cy + 1.0; j += 1.0) {
        for (double i = cx - 1.0; i <= cx + 1.0; i += 1.0) {
            RandomStream random(seed, kThomasCentres2, word(i), word(j));
            const std::uint64_t count = SamplePoisson(random, perCell);
            for (std::uint64_t n = 0; n < count; ++n) {
                const double dx = (i + random.NextDouble()) * side - x;
                const double dy = (j + random.NextDouble()) * side - y;
                sum += bump(dx * dx + dy * dy);
            }
        }
    }
    return sum;
}

double BoulderLayer::BackgroundAbundance(double x, double y, const Ground* ground,
                                         double ejecta) const {
    double background =
        mapAbundance_ ? mapAbundance_->At(MapValueAt(*options_.densityMap, x, y), ejecta) : 1.0;
    const BoulderPatches& patches = options_.patches;
    if (patches.spread > 1.0) {
        const double noise =
            PatchNoise(surface_, x, y, patches.size, options_.seed ^ kBoulderPatches);
        background *= std::pow(patches.spread, noise) / patchMean_;
    }
    const BoulderClusters& clusters = options_.clusters;
    if (clusters.share > 0.0) {
        const double field = clusters.model == BoulderClusterModel::Thomas
                                 ? ThomasClusterField(x, y)
                                 : ClusterBump(NearestClusterDistance(x, y), clusters);
        background *= 1.0 - clusters.share + clusters.share * field / clusterMean_;
    }
    if (exposure_) {
        RequireGround(ground, "Boulders that read the exposure field");
        const double bedrock = exposure_->Exposure(*ground, x, y, options_.bedrock.exposureSpacing);
        background *= 1.0 + bedrock * (options_.bedrock.abundance - 1.0);
        if (bedrockMeans_) {
            background /= BedrockMean(x, y, *ground);
        }
    }
    return std::min(background, maxBackground_);
}

std::size_t BoulderLayer::MeanCellHash::operator()(const MeanCell& cell) const {
    const auto words = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(cell.ground));
    const U32x4 hash =
        Philox4x32({static_cast<std::uint32_t>(cell.i), static_cast<std::uint32_t>(cell.j),
                    static_cast<std::uint32_t>(words), static_cast<std::uint32_t>(words >> 32)},
                   {kBedrockMeans, 0u});
    return (static_cast<std::size_t>(hash.x) << 32) ^ hash.y;
}

double BoulderLayer::BedrockMean(double x, double y, const Ground& ground) const {
    constexpr int kSide = 8;
    const double gain = options_.bedrock.abundance - 1.0;
    const auto cellMean = [&](std::int64_t i, std::int64_t j) {
        return *bedrockMeans_->Get({&ground, i, j}, [&] {
            double sum = 0.0;
            for (int b = 0; b < kSide; ++b) {
                for (int a = 0; a < kSide; ++a) {
                    const double px =
                        meanOrigin_.x + (static_cast<double>(i) + (a + 0.5) / kSide) * meanCell_.x;
                    const double py =
                        meanOrigin_.y + (static_cast<double>(j) + (b + 0.5) / kSide) * meanCell_.y;
                    sum += 1.0 + gain * exposure_->Exposure(ground, px, py,
                                                            options_.bedrock.exposureSpacing);
                }
            }
            return sum / (kSide * kSide);
        });
    };
    const double u = (x - meanOrigin_.x) / meanCell_.x - 0.5;
    const double v = (y - meanOrigin_.y) / meanCell_.y - 0.5;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const auto i = static_cast<std::int64_t>(fu);
    const auto j = static_cast<std::int64_t>(fv);
    const double tu = u - fu;
    const double tv = v - fv;
    return (1.0 - tu) * (1.0 - tv) * cellMean(i, j) + tu * (1.0 - tv) * cellMean(i + 1, j) +
           (1.0 - tu) * tv * cellMean(i, j + 1) + tu * tv * cellMean(i + 1, j + 1);
}

void BoulderLayer::EjectaAbundance(double x, double y, const CraterList& ejecta,
                                   BoulderAbundance& abundance) const {
    const BoulderEjecta& eject = options_.ejecta;
    const DegradationModel& degradation = *craters_->Models().degradation;
    double share = 0.0;
    double wear = 0.0;
    for (std::size_t n = 0; n < ejecta.Size(); ++n) {
        const Crater crater = ejecta.At(n);
        const double reach = EjectaReach(eject, crater.diameter);
        const double rho =
            surface_.Distance({crater.x, crater.y}, {x, y}) / (0.5 * crater.diameter);
        if (rho >= reach) {
            continue;
        }
        const double radial = RadialShare(eject, rho, reach);
        if (radial <= share) {
            continue;
        }
        const double retention = Retention(eject, crater, degradation);
        if (retention * radial > share) {
            share = retention * radial;
            wear = 1.0 - retention;
        }
    }
    abundance.ejecta = share;
    abundance.ejectaWear = wear;
}

BoulderAbundance BoulderLayer::Abundance(double x, double y, const Ground* ground,
                                         const CraterList& ejecta) const {
    BoulderAbundance abundance;
    if (!ejecta.Empty()) {
        EjectaAbundance(x, y, ejecta, abundance);
    }
    abundance.background = BackgroundAbundance(x, y, ground, abundance.ejecta);
    return abundance;
}

BoulderAbundance BoulderLayer::AbundanceAt(double x, double y, const Ground* ground) const {
    return Abundance(x, y, ground, EjectaCraters(PointBounds(x, y)));
}

double BoulderLayer::Density(double diameter, const BoulderAbundance& abundance) const {
    const BoulderSizes& sizes = options_.sizes;
    double density = sizes.distribution
                         ? abundance.background * sizes.distribution->Incremental(diameter)
                         : GolombekRappDensity(diameter, sizes.coverage * abundance.background,
                                               sizes.qConstant, sizes.qCoverageCoefficient);
    if (abundance.ejecta > 0.0) {
        density += abundance.ejecta * ejectaSizes_->Incremental(diameter);
    }
    return density;
}

double BoulderLayer::DensityAt(double diameter, double x, double y, const Ground* ground) const {
    return Density(diameter, AbundanceAt(x, y, ground));
}

}  // namespace terraingen
