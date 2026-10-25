// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief How abundant a `BoulderLayer`'s boulders are: density maps, patches, crater blocks and
/// the candidates thinning starts from.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/boulders/boulder_options.h"

#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// How a density map sets a boulder layer's abundance between craters: the abundance at which the
/// boulder sizes measure what the map does, once the crater blocks there are taken out of it for a
/// `total` map. With Golombek-Rapp sizes it solves for the coverage, as rock abundance maps give.
class BoulderMapAbundance {
  public:
    /// @throws std::invalid_argument for no map, an invalid one, or one measuring none of the
    /// sizes.
    /// @throws std::domain_error as `MapMeasure`.
    explicit BoulderMapAbundance(const BoulderLayerOptions& options);

    /// The abundance where the map reads `value` (`MapValueAt`) and the crater blocks hold `ejecta`
    /// of their full abundance: 1 where `value` is NaN, 0 where the blocks make up the whole map.
    double At(double value, double ejecta) const;

    /// The largest abundance the map gives anywhere, at least 1.
    double Max() const {
        return max_;
    }

  private:
    /// The coverage at which Golombek-Rapp boulders measure `target`.
    double Coverage(double target) const;

    DensityMap map_;
    double coverage_ = 0.0;
    double measure_ = 0.0;
    double ejectaMeasure_ = 0.0;
    std::vector<double> coverages_;
    std::vector<double> measures_;
    double max_ = 1.0;
};

/// The mean over the plane of the patches' abundance, `spread` to the power of `PatchNoise`,
/// which is above 1 for a spread above 1; under a density map the patches are divided by it so
/// they keep the map's mean. 1 without patches.
double MeanPatchAbundance(const BoulderPatches& patches);

/// The models of a layer's abundance between craters, each built once from valid options.
struct BoulderBackground {
    /// @throws std::invalid_argument as `BoulderMapAbundance` for an invalid density map.
    explicit BoulderBackground(const BoulderLayerOptions& options);

    double patchMean = 1.0;                  ///< Under a map, what the patches are divided by.
    std::optional<BoulderMapAbundance> map;  ///< The density map's abundance, when there is one.
    /// The largest background abundance (`MaxBackgroundAbundance`).
    double max = 1.0;
};

/// The largest background abundance, on the richest patches on bedrock: the patch spread, times
/// the bedrock abundance (at least 1) when bedrock is enabled.
double MaxBackgroundAbundance(const BoulderLayerOptions& options);

/// The sizes of the crater blocks at a fresh crater's rim: the ejecta distribution, or
/// Golombek-Rapp blocks of the ejecta coverage.
std::shared_ptr<const SizeFrequencyDistribution> EjectaSizes(const BoulderLayerOptions& options);

/// The distribution per m^2 candidate boulders are drawn from before thinning: the boulders
/// between craters at their richest, with a size slope map the power laws at its bounding
/// exponents too, plus fresh crater blocks when enabled.
/// @throws std::invalid_argument as `ValidateBoulderLayerOptions`, or without `generateRandom`,
/// when no candidates are drawn.
std::shared_ptr<const SizeFrequencyDistribution>
BoulderCandidates(const BoulderLayerOptions& options);

/// `BoulderCandidates` of options already checked, whose `MaxBackgroundAbundance` is `richest`.
std::shared_ptr<const SizeFrequencyDistribution>
BoulderCandidates(const BoulderLayerOptions& options, double richest);

}  // namespace terraingen
