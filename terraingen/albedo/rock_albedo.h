/// @file
/// @brief Rock albedo: boulders and the rocks too small to resolve, brighter than the regolith.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/boulders/boulder_layer.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace terraingen {

/// How rocks brighten the albedo. Rock reflects more than mature regolith: Holt (1981) measured
/// normal albedos of 18 to 51% for rocks at the Apollo 16 site against 14 to 15% for its most
/// mature soils; Marshal et al. (2023) model rock about 2.5 times as reflective as regolith,
/// within the near 3 times of Apollo 16 fragmental breccias (Clark et al. 2002), and earlier
/// models took about 1.5 times. Boulders an albedo resolves brighten where their rock stands;
/// smaller ones brighten each sample by the share of it they cover, drawn per sample about its
/// expected share with the variance of Poisson-scattered rocks, which unresolved cobbles show as
/// bright pixels (Marshal et al. 2023).
struct RockAlbedoOptions {
    std::uint32_t seed = 0;         ///< Seed of the per-sample draws of unresolved rock.
    double reflectanceRatio = 2.5;  ///< Rock over regolith reflectance; positive.
    bool unresolved = true;         ///< Whether rocks too small to resolve brighten samples.

    bool operator==(const RockAlbedoOptions&) const = default;
};

/// Checks rock albedo options.
/// @throws std::invalid_argument for a reflectance ratio that is not positive and finite.
void ValidateRockAlbedoOptions(const RockAlbedoOptions& options);

/// A boulder layer whose rock brightens the albedo, and the ground beneath it.
struct RockSource {
    std::shared_ptr<const BoulderLayer> layer;
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
                 const AlbedoContext& context) const override;

    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// The share of the sample at (`x`, `y`) rock covers, over every layer.
    double ShareAt(double x, double y, const AlbedoContext& context) const;

  private:
    /// The standard normal drawn for unresolved rock of source `source` at (`x`, `y`).
    double UnresolvedNormal(double x, double y, std::size_t source) const;
    /// The share of each cell of the grid of `first` that no source's rock covers, row-major.
    std::vector<double> BareShares(const Raster<float>& first, const AlbedoContext& context) const;

    RockAlbedoOptions options_;
    std::vector<RockSource> sources_;
};

}  // namespace terraingen
