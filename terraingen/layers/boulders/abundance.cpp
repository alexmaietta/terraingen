#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/layers/boulders/boulder_clusters.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/noise/hash.h"
#include "terraingen/noise/worley.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace terraingen {

namespace {

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

EjectaIndex BoulderLayer::EjectaCraters(const Bounds2d& region) const {
    if (minCraterDiameter_ <= 0.0) {
        return {};
    }
    CraterList ejecta;
    std::vector<std::uint32_t> sources;
    for (std::size_t k = 0; k < craters_.size(); ++k) {
        const CraterLayer& layer = *craters_[k];
        const double margin = options_.ejecta.reach * 0.5 * layer.MaxFeatureDiameter();
        const double spacing = minCraterDiameter_ / layer.Options().apply.fade.startSamples;
        const CraterList all = layer.Craters(surface_.Expand(region, margin), spacing);
        const CraterList thrown = FilterCraters(all, [&](std::size_t, const Crater& crater) {
            return crater.diameter >= minCraterDiameter_;
        });
        ejecta.Append(thrown);
        sources.resize(ejecta.Size(), static_cast<std::uint32_t>(k));
    }
    const auto reach = [&](const Crater& crater) {
        return EjectaReach(options_.ejecta, crater.diameter) * 0.5 * crater.diameter;
    };
    return {std::move(ejecta), surface_, reach, std::move(sources)};
}

double BoulderLayer::NearestClusterDistance(double x, double y) const {
    const double spacing = options_.clusters.spacing;
    if (surface_.IsBody()) {
        const Vec3d p = surface_.Position({x, y});
        const double s = 1.0 / spacing;
        return spacing *
               static_cast<double>(
                   Worley3(SplitLatticePrecise3(s * p.x, s * p.y, s * p.z), clusterSeed_).f1);
    }
    return spacing * static_cast<double>(Worley2(static_cast<float>(x / spacing),
                                                 static_cast<float>(y / spacing), clusterSeed_)
                                             .f1);
}

namespace {

/// The cells and Gaussians of a `Thomas` cluster field: centres scattered `perCell` to a cell
/// `side` across, each adding a Gaussian of `sigma` cut at `side`.
struct ThomasLattice {
    double sigma = 1.0;
    double side = 1.0;
    double perCell = 0.0;
    std::uint32_t seed = 0;

    /// The word of the lattice cell index `cell`.
    static std::uint32_t Word(double cell) {
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(cell));
    }

    /// The Gaussian of a centre `squared` square metres away.
    double Bump(double squared) const {
        return squared < side * side ? std::exp(-0.5 * squared / (sigma * sigma)) : 0.0;
    }

    /// The field at `p` from centres scattered through space.
    double Sum3(const Vec3d& p) const {
        double sum = 0.0;
        const double cx = std::floor(p.x / side);
        const double cy = std::floor(p.y / side);
        const double cz = std::floor(p.z / side);
        for (double k = cz - 1.0; k <= cz + 1.0; k += 1.0) {
            for (double j = cy - 1.0; j <= cy + 1.0; j += 1.0) {
                for (double i = cx - 1.0; i <= cx + 1.0; i += 1.0) {
                    RandomStream random(seed, kThomasSpaceStream, Word(i), Word(j), Word(k));
                    const std::uint64_t count = SamplePoisson(random, perCell);
                    for (std::uint64_t n = 0; n < count; ++n) {
                        const double dx = (i + random.NextDouble()) * side - p.x;
                        const double dy = (j + random.NextDouble()) * side - p.y;
                        const double dz = (k + random.NextDouble()) * side - p.z;
                        sum += Bump(dx * dx + dy * dy + dz * dz);
                    }
                }
            }
        }
        return sum;
    }

    /// The field at (`x`, `y`) from centres scattered over the plane.
    double Sum2(double x, double y) const {
        double sum = 0.0;
        const double cx = std::floor(x / side);
        const double cy = std::floor(y / side);
        for (double j = cy - 1.0; j <= cy + 1.0; j += 1.0) {
            for (double i = cx - 1.0; i <= cx + 1.0; i += 1.0) {
                RandomStream random(seed, kThomasPlaneStream, Word(i), Word(j));
                const std::uint64_t count = SamplePoisson(random, perCell);
                for (std::uint64_t n = 0; n < count; ++n) {
                    const double dx = (i + random.NextDouble()) * side - x;
                    const double dy = (j + random.NextDouble()) * side - y;
                    sum += Bump(dx * dx + dy * dy);
                }
            }
        }
        return sum;
    }
};

}  // namespace

double BoulderLayer::ThomasClusterField(double x, double y) const {
    const BoulderClusters& clusters = options_.clusters;
    ThomasLattice lattice;
    lattice.sigma = clusters.radius;
    lattice.side = kThomasReachRadii * lattice.sigma;
    lattice.seed = options_.seed;
    const double perArea = 1.0 / (clusters.spacing * clusters.spacing);
    const double side = lattice.side;
    if (surface_.IsBody()) {
        lattice.perCell =
            perArea / (std::sqrt(2.0 * std::numbers::pi) * lattice.sigma) * side * side * side;
        return lattice.Sum3(surface_.Position({x, y}));
    }
    lattice.perCell = perArea * side * side;
    return lattice.Sum2(x, y);
}

double BoulderLayer::BackgroundAbundance(double x, double y, const Ground* ground,
                                         double ejecta) const {
    double background =
        mapAbundance_ ? mapAbundance_->At(MapValueAt(*options_.densityMap, x, y), ejecta) : 1.0;
    const BoulderPatches& patches = options_.patches;
    if (patches.spread > 1.0) {
        const double noise = PatchNoise(surface_, x, y, patches.size, patchSeed_);
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
        const double bedrock = exposure_->Exposure(*ground, x, y, options_.groundSpacing);
        background *= 1.0 + bedrock * (options_.bedrock.abundance - 1.0);
        if (bedrockMeans_) {
            background /= BedrockMean(x, y, *ground);
        }
    }
    return std::min(background, maxBackground_);
}

std::size_t BoulderLayer::BedrockCellHash::operator()(const BedrockCell& cell) const {
    return HashWords({static_cast<std::uint64_t>(cell.i), static_cast<std::uint64_t>(cell.j),
                      static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(cell.ground))});
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
                        bedrockOrigin_.x +
                        (static_cast<double>(i) + (a + 0.5) / kSide) * bedrockCellSize_.x;
                    const double py =
                        bedrockOrigin_.y +
                        (static_cast<double>(j) + (b + 0.5) / kSide) * bedrockCellSize_.y;
                    sum += 1.0 + gain * exposure_->Exposure(ground, px, py, options_.groundSpacing);
                }
            }
            return sum / (kSide * kSide);
        });
    };
    const double u = (x - bedrockOrigin_.x) / bedrockCellSize_.x - 0.5;
    const double v = (y - bedrockOrigin_.y) / bedrockCellSize_.y - 0.5;
    const double fu = std::floor(u);
    const double fv = std::floor(v);
    const auto i = static_cast<std::int64_t>(fu);
    const auto j = static_cast<std::int64_t>(fv);
    const double tu = u - fu;
    const double tv = v - fv;
    return (1.0 - tu) * (1.0 - tv) * cellMean(i, j) + tu * (1.0 - tv) * cellMean(i + 1, j) +
           (1.0 - tu) * tv * cellMean(i, j + 1) + tu * tv * cellMean(i + 1, j + 1);
}

void BoulderLayer::EjectaAbundance(double x, double y, const EjectaIndex& ejecta,
                                   BoulderAbundance& abundance) const {
    const BoulderEjecta& eject = options_.ejecta;
    double share = 0.0;
    double wear = 0.0;
    for (const std::uint32_t n : ejecta.Near(x, y)) {
        const DegradationModel& degradation = *craters_[ejecta.Source(n)]->Models().degradation;
        const Crater crater = ejecta.Craters().At(n);
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
                                         const EjectaIndex& ejecta) const {
    BoulderAbundance abundance;
    if (!ejecta.Empty()) {
        EjectaAbundance(x, y, ejecta, abundance);
    }
    abundance.background = BackgroundAbundance(x, y, ground, abundance.ejecta);
    if (options_.slopeMap) {
        abundance.exponent = SizeSlopeAt(*options_.slopeMap, x, y);
    }
    return abundance;
}

BoulderAbundance BoulderLayer::AbundanceAt(double x, double y, const Ground* ground) const {
    return Abundance(x, y, ground, EjectaCraters(PointBounds(x, y)));
}

double BoulderLayer::Density(double diameter, const BoulderAbundance& abundance) const {
    const BoulderSizes& sizes = options_.sizes;
    double density = 0.0;
    if (abundance.exponent) {
        const double alpha = *abundance.exponent;
        const double d0 = options_.slopeMap->referenceDiameter;
        density = abundance.background * referenceCount_ * alpha * std::pow(d0, alpha) *
                  std::pow(diameter, -alpha - 1.0);
    } else {
        density = sizes.distribution
                      ? abundance.background * sizes.distribution->Incremental(diameter)
                      : GolombekRappDensity(diameter, sizes.coverage * abundance.background,
                                            sizes.qConstant, sizes.qCoverageCoefficient);
    }
    if (abundance.ejecta > 0.0) {
        density += abundance.ejecta * ejectaSizes_->Incremental(diameter);
    }
    return density;
}

double BoulderLayer::DensityAt(double diameter, double x, double y, const Ground* ground) const {
    return Density(diameter, AbundanceAt(x, y, ground));
}

std::vector<BoulderLayer::UnresolvedNode> BoulderLayer::UnresolvedNodes(double spacing) const {
    const BoulderSizes& sizes = options_.sizes;
    const double start = options_.fade.startSamples * spacing;
    const double end = std::fmin(options_.fade.endSamples * spacing, sizes.maxDiameter);
    std::vector<UnresolvedNode> nodes;
    const auto add = [&](double low, double high) {
        if (!(high > low)) {
            return;
        }
        constexpr int kIntervals = 16;
        const double a = std::log(low);
        const double h = (std::log(high) - a) / kIntervals;
        for (int i = 0; i <= kIntervals; ++i) {
            const double d = std::exp(a + h * i);
            const double simpson = (i == 0 || i == kIntervals) ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
            const double unresolved = 1.0 - ResolutionWeight(d, spacing, options_.fade);
            if (unresolved > 0.0) {
                nodes.push_back(
                    {d, 0.25 * std::numbers::pi * d * d, unresolved * d * simpson * h / 3.0});
            }
        }
    };
    add(sizes.minDiameter, std::fmin(start, end));
    add(std::fmax(sizes.minDiameter, start), end);
    return nodes;
}

UnresolvedCover BoulderLayer::UnresolvedCoverWith(const BoulderAbundance& abundance,
                                                  const std::vector<UnresolvedNode>& nodes,
                                                  double spacing) const {
    UnresolvedCover cover;
    for (const UnresolvedNode& node : nodes) {
        const double n = Density(node.diameter, abundance) * node.weight;
        cover.mean += node.footprint * n;
        cover.variance += node.footprint * node.footprint * n;
    }
    cover.variance /= spacing * spacing;
    return cover;
}

UnresolvedCover BoulderLayer::UnresolvedCoverAt(double x, double y, double spacing,
                                                const Ground* ground) const {
    RequirePositive(spacing, "Unresolved cover spacing");
    if (options_.sizes.minDiameter >= options_.fade.endSamples * spacing) {
        return {};
    }
    return UnresolvedCoverWith(AbundanceAt(x, y, ground), UnresolvedNodes(spacing), spacing);
}

std::vector<UnresolvedCover> BoulderLayer::UnresolvedCoverOver(const RasterGrid& grid,
                                                               double spacing, const Ground* ground,
                                                               unsigned threads) const {
    RequirePositive(spacing, "Unresolved cover spacing");
    std::vector<UnresolvedCover> cover(grid.width * grid.height);
    if (options_.sizes.minDiameter >= options_.fade.endSamples * spacing) {
        return cover;
    }
    const EjectaIndex ejecta = EjectaCraters(grid.Bounds());
    const std::vector<UnresolvedNode> nodes = UnresolvedNodes(spacing);
    ParallelFor(grid.height, threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const Vec2d centre = grid.transform.PixelToWorld(static_cast<double>(col) + 0.5,
                                                             static_cast<double>(row) + 0.5);
            cover[row * grid.width + col] =
                UnresolvedCoverWith(Abundance(centre.x, centre.y, ground, ejecta), nodes, spacing);
        }
    });
    return cover;
}

}  // namespace terraingen
