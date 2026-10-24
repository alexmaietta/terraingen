// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_clusters.h"

#include "terraingen/noise/worley.h"
#include "terraingen/util/bits.h"

#include <cmath>

namespace terraingen {

double ClusterBump(double distance, const BoulderClusters& clusters) {
    const double r = distance / clusters.radius;
    return std::exp(-0.5 * r * r);
}

double MeanClusterBump(const BoulderClusters& clusters) {
    return LatticeMean(16.0, [&](double u, double v) {
        const WorleyDistances d =
            Worley2(static_cast<float>(u), static_cast<float>(v), kBoulderClusterStream);
        return ClusterBump(static_cast<double>(d.f1) * clusters.centerSpacing, clusters);
    });
}

double MeanThomasField(const BoulderClusters& clusters, bool body) {
    const double sigma = clusters.radius;
    const double reach = kThomasReachRadii;
    const double perArea = 1.0 / (clusters.centerSpacing * clusters.centerSpacing);
    if (body) {
        const double perVolume = perArea / (std::sqrt(2.0 * kPi) * sigma);
        const double inside = std::erf(reach / std::sqrt(2.0)) -
                              std::sqrt(2.0 / kPi) * reach * std::exp(-0.5 * reach * reach);
        return perVolume * std::pow(2.0 * kPi * sigma * sigma, 1.5) * inside;
    }
    return perArea * 2.0 * kPi * sigma * sigma * (1.0 - std::exp(-0.5 * reach * reach));
}

double MaxClusterAbundance(const BoulderClusters& clusters) {
    if (clusters.share <= 0.0) {
        return 1.0;
    }
    if (clusters.model == BoulderClusterModel::Nearest) {
        return 1.0 - clusters.share + clusters.share / MeanClusterBump(clusters);
    }
    constexpr double kChance = 1e-6;
    const double ratio = clusters.radius / clusters.centerSpacing;
    const double mean = 9.0 * kPi * ratio * ratio;
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
