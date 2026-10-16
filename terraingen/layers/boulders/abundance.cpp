/// @file
/// @brief `BoulderLayer` abundance and density: patches, bedrock and crater ejecta.
#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace terraingen {

namespace {

constexpr std::uint32_t kBoulderPatches = 0x50415443u;

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

double BoulderLayer::BackgroundAbundance(double x, double y, const Ground* ground) const {
    double background = 1.0;
    const BoulderPatches& patches = options_.patches;
    if (patches.spread > 1.0) {
        const double noise =
            PatchNoise(surface_, x, y, patches.size, options_.seed ^ kBoulderPatches);
        background = std::pow(patches.spread, noise);
    }
    if (exposure_) {
        RequireGround(ground, "Boulders that read the exposure field");
        const double bedrock = exposure_->Exposure(*ground, x, y, options_.bedrock.exposureSpacing);
        background *= 1.0 + bedrock * (options_.bedrock.abundance - 1.0);
    }
    return std::min(background, maxBackground_);
}

double BoulderLayer::EjectaAbundance(double x, double y, const CraterList& ejecta) const {
    const BoulderEjecta& eject = options_.ejecta;
    const DegradationModel& degradation = *craters_->Models().degradation;
    double share = 0.0;
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
        share = std::fmax(share, Retention(eject, crater, degradation) * radial);
    }
    return share;
}

BoulderAbundance BoulderLayer::Abundance(double x, double y, const Ground* ground,
                                         const CraterList& ejecta) const {
    BoulderAbundance abundance;
    abundance.background = BackgroundAbundance(x, y, ground);
    if (!ejecta.Empty()) {
        abundance.ejecta = EjectaAbundance(x, y, ejecta);
    }
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
