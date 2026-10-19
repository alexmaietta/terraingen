#include "terraingen/layers/boulders/boulder_clusters.h"

#include "terraingen/noise/worley.h"

#include <cmath>
#include <numbers>

namespace terraingen {

double ClusterBump(double distance, const BoulderClusters& clusters) {
    const double r = distance / clusters.radius;
    return std::exp(-0.5 * r * r);
}

double MeanClusterBump(const BoulderClusters& clusters) {
    return LatticeMean(16.0, [&](double u, double v) {
        const WorleyDistances d =
            Worley2(static_cast<float>(u), static_cast<float>(v), kBoulderClusterStream);
        return ClusterBump(static_cast<double>(d.f1) * clusters.spacing, clusters);
    });
}

double MeanThomasField(const BoulderClusters& clusters, bool body) {
    const double sigma = clusters.radius;
    const double reach = kThomasReachRadii;
    const double perArea = 1.0 / (clusters.spacing * clusters.spacing);
    if (body) {
        const double perVolume = perArea / (std::sqrt(2.0 * std::numbers::pi) * sigma);
        const double inside =
            std::erf(reach / std::numbers::sqrt2) -
            std::sqrt(2.0 / std::numbers::pi) * reach * std::exp(-0.5 * reach * reach);
        return perVolume * std::pow(2.0 * std::numbers::pi * sigma * sigma, 1.5) * inside;
    }
    return perArea * 2.0 * std::numbers::pi * sigma * sigma *
           (1.0 - std::exp(-0.5 * reach * reach));
}

double MaxClusterAbundance(const BoulderClusters& clusters) {
    if (clusters.share <= 0.0) {
        return 1.0;
    }
    if (clusters.model == BoulderClusterModel::Nearest) {
        return 1.0 - clusters.share + clusters.share / MeanClusterBump(clusters);
    }
    constexpr double kChance = 1e-6;
    const double ratio = clusters.radius / clusters.spacing;
    const double mean = 9.0 * std::numbers::pi * ratio * ratio;
    double term = std::exp(-mean);
    double above = 1.0 - term;
    int crowd = 0;
    while (above >= kChance) {
        ++crowd;
        term *= mean / crowd;
        above -= term;
    }
    return 1.0 - clusters.share +
           clusters.share * std::fmax(1.0, crowd) / MeanThomasField(clusters);
}

}  // namespace terraingen
