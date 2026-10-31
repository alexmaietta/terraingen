// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Rock albedo: boulders and the rocks too small to resolve, brighter than the regolith.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/boulders/boulder_layer.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace terraingen {

/// How rocks, brighter than mature regolith (Holt 1981; Clark et al. 2002; Marshal et al. 2023),
/// brighten the albedo: resolved boulders where they stand, unresolved rock by a per-sample draw
/// of the share it covers.
struct RockAlbedoOptions {
    std::uint32_t seed = 0;         ///< Seed of the per-sample draws of unresolved rock.
    double reflectanceRatio = 2.5;  ///< Rock over regolith reflectance; positive.
    bool unresolved = true;         ///< Whether rocks too small to resolve brighten samples.

    bool operator==(const RockAlbedoOptions& other) const {
        return seed == other.seed && reflectanceRatio == other.reflectanceRatio &&
               unresolved == other.unresolved;
    }
    bool operator!=(const RockAlbedoOptions& other) const {
        return !(*this == other);
    }
};

/// Checks rock albedo options.
/// @throws std::invalid_argument naming the first field out of its range.
void ValidateRockAlbedoOptions(const RockAlbedoOptions& options);

/// A boulder layer whose rock brightens the albedo, and the ground beneath it.
struct RockSource {
    std::shared_ptr<const BoulderLayer> layer;  ///< The boulders; must not be null.
    /// The ground the layer was applied to, read under its boulders; must not be null.
    std::shared_ptr<const Ground> ground;
};

/// The share of a sample rock covers, from the share its resolved boulders cover and a draw
/// `normal` (standard normal) of the share unresolved rock covers of the rest: log-normal with
/// the mean and variance of `unresolved`, at most 1.
double RockShare(double resolved, const UnresolvedCover& unresolved, double normal);

/// The albedo brightened by the rock of boulder layers: each band times 1 + `share`
/// (`reflectanceRatio` - 1), for the share of each sample rock covers over every layer.
class RockAlbedo final : public AlbedoLayer {
  public:
    /// @throws std::invalid_argument for options invalid as `ValidateRockAlbedoOptions`, or a
    /// source without a layer or ground.
    RockAlbedo(RockAlbedoOptions options, std::vector<RockSource> sources);

    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const LayerContext& context) const override;

    void Apply(std::vector<Raster<float>>& bands, const LayerContext& context) const override;

    /// The share of the sample at (`x`, `y`) rock covers, over every layer.
    double ShareAt(double x, double y, const LayerContext& context) const;

  private:
    /// The standard normal drawn for unresolved rock of source `source` at (`x`, `y`).
    double UnresolvedNormal(double x, double y, std::size_t source) const;
    /// The share of each cell of the grid of `first` that no source's rock covers, row-major.
    std::vector<double> BareShares(const Raster<float>& first, const LayerContext& context) const;

    RockAlbedoOptions options_;
    std::vector<RockSource> sources_;
};

}  // namespace terraingen
